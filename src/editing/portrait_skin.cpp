#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include "huelith/editing/image_storage.h"
#include <opencv2/imgproc.hpp>
#include <QJsonArray>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace huelith::editing {
namespace {
constexpr std::array<const char*,13> keys{"skin.smooth","skin.texture","skin.uniform","skin.oil","color.light","color.warmth","color.rosy","color.redness","color.balance","color.custom","hair.smooth","skin.bodySmooth","makeup.foundation"};
struct SkinPerson {std::array<double,13> values{};std::array<QImage,13> masks;std::array<double,3> reference{},custom{},foundation{};double hairDetail{},oilGloss{},sigma{},bodySigma{},noise{},smoothFine{.85},smoothRadius{1.5},smoothEpsilon{.0004},poreRetention{.92},bodyRetention{.92};bool hairSmoothV2{},balanceV2{},chromaV2{},rednessV2{},oilV2{},smoothV2{},textureV2{},lightV2{},uniformV2{},customV2{};QImage body,skinReference,balanceKeep;};
double luminance(const std::array<double,3>& c){return c[0]*.2627+c[1]*.6780+c[2]*.0593;}
cv::Mat guided_skin_base(const cv::Mat& input,int radius,double epsilon=.0004) {
    std::vector<cv::Mat> channels;cv::split(input,channels);
    cv::Mat guide=channels[0]*.2627+channels[1]*.6780+channels[2]*.0593;
    cv::max(guide,0.,guide);
    const cv::Size kernel(radius*2+1,radius*2+1);
    const auto mean=[&](const cv::Mat& image){cv::Mat result;cv::boxFilter(image,result,-1,kernel,{-1,-1},true,cv::BORDER_REFLECT_101);return result;};
    const cv::Mat meanGuide=mean(guide),variance=mean(guide.mul(guide))-meanGuide.mul(meanGuide);
    for(auto& channel:channels){
        const cv::Mat meanChannel=mean(channel),covariance=mean(guide.mul(channel))-meanGuide.mul(meanChannel);
        cv::Mat slope;cv::divide(covariance,variance+epsilon,slope);
        const cv::Mat intercept=meanChannel-slope.mul(meanGuide);
        channel=mean(slope).mul(guide)+mean(intercept);
    }
    cv::Mat result;cv::merge(channels,result);return result;
}
double coverage(const QImage& mask,int x,int y,QSize full) {
    if(mask.isNull())return 0;return mask.constScanLine(std::clamp(int(double(y)/full.height()*mask.height()),0,mask.height()-1))[std::clamp(int(double(x)/full.width()*mask.width()),0,mask.width()-1)]/255.;
}
// Finite tangent-only kernel: no cross-strand averaging or background samples.
cv::Mat directional_hair_base(const cv::Mat& input,const QImage& mask,QPoint origin,QSize full) {
    cv::Mat gray(input.rows,input.cols,CV_32F);
    for(int y=0;y<input.rows;++y)for(int x=0;x<input.cols;++x){const auto p=input.at<cv::Vec4f>(y,x);gray.at<float>(y,x)=(.2627F*p[0]+.678F*p[1]+.0593F*p[2])/std::max(.00001F,p[3]);}
    cv::Mat guide,gx,gy,xx,xy,yy;cv::GaussianBlur(gray,guide,{5,5},.8);cv::Sobel(guide,gx,CV_32F,1,0,3);cv::Sobel(guide,gy,CV_32F,0,1,3);
    cv::GaussianBlur(gx.mul(gx),xx,{9,9},2);cv::GaussianBlur(gx.mul(gy),xy,{9,9},2);cv::GaussianBlur(gy.mul(gy),yy,{9,9},2);
    cv::Mat result=input.clone();
    for(int y=0;y<input.rows;++y)for(int x=0;x<input.cols;++x){
        if(coverage(mask,x+origin.x(),y+origin.y(),full)<.8)continue;
        const double a=xx.at<float>(y,x),b=xy.at<float>(y,x),d=yy.at<float>(y,x),energy=a+d,coherence=std::hypot(a-d,2*b)/std::max(1e-12,energy);
        if(energy<1e-6 || coherence<.35)continue;
        const double theta=.5*std::atan2(2*b,a-d)+1.5707963267948966,dx=std::cos(theta),dy=std::sin(theta);
        const auto center=input.at<cv::Vec4f>(y,x);if(center[3]<.95F)continue;
        cv::Vec4d sum{};double weights=0;
        for(int step=-3;step<=3;++step){const double px=x+step*dx,py=y+step*dy;const int ix=int(std::floor(px)),iy=int(std::floor(py));if(ix<0 || iy<0 || ix+1>=input.cols || iy+1>=input.rows)continue;
            bool valid=true;for(int j=0;j<2;++j)for(int i=0;i<2;++i)if(coverage(mask,origin.x()+ix+i,origin.y()+iy+j,full)<.8 || input.at<cv::Vec4f>(iy+j,ix+i)[3]<.95F)valid=false;
            if(!valid)continue;
            const double fx=px-ix,fy=py-iy;cv::Vec4d sample{};
            for(int j=0;j<2;++j)for(int i=0;i<2;++i)sample+=cv::Vec4d(input.at<cv::Vec4f>(iy+j,ix+i))*((i?fx:1-fx)*(j?fy:1-fy));
            const double light=(.2627*sample[0]+.678*sample[1]+.0593*sample[2])/sample[3],delta=light-gray.at<float>(y,x),scale=.015+gray.at<float>(y,x)*.12,w=std::exp(-step*step/8.-delta*delta/(2*scale*scale));
            sum+=sample*w;weights+=w;
        }
        if(weights>0){auto& out=result.at<cv::Vec4f>(y,x);const double gain=std::clamp((coherence-.35)/.4,0.,1.);for(int c=0;c<3;++c)out[c]=float(center[c]+gain*(sum[c]/sum[3]*center[3]-center[c]));}
    }
    return result;
}
}
QJsonArray portrait_balance_reference(const QImage& source,const QJsonObject& person){
    if(source.format()!=QImage::Format_RGBA32FPx4_Premultiplied)return {};
    const auto face=person["face"].toArray(),reference=person["settings"].toObject()["balanceReference"].toArray();if(face.size()!=4)return {};
    const auto skin=portrait_effect_mask(person,"skin",source.size().scaled(768,768,Qt::KeepAspectRatio));
    QRectF bounds(face[0].toDouble()*source.width(),face[1].toDouble()*source.height(),face[2].toDouble()*source.width(),face[3].toDouble()*source.height());
    QPointF center;double radius=0;if(reference.size()==3){center={reference[0].toDouble()*source.width(),reference[1].toDouble()*source.height()};radius=reference[2].toDouble()*std::min(source.width(),source.height());bounds=QRectF(center-QPointF(radius,radius),QSizeF(radius*2,radius*2));}
    const auto box=bounds.toAlignedRect().intersected(source.rect());const int step=std::max(1,int(std::sqrt(double(box.width())*box.height()/3000.)));std::vector<std::array<double,3>> samples;
    for(int y=box.top();y<=box.bottom();y+=step)for(int x=box.left();x<=box.right();x+=step){if(radius>0 && std::hypot(x-center.x(),y-center.y())>radius)continue;if(coverage(skin,x,y,source.size())<.8)continue;const auto* p=reinterpret_cast<const float*>(source.constScanLine(y))+x*4;const double light=(.2627*p[0]+.678*p[1]+.0593*p[2])/std::max(.001F,p[3]);if(p[3]<.95F || light<.02 || light>.8)continue;samples.push_back({p[0]/p[3]/light,p[1]/p[3]/light,p[2]/p[3]/light});}
    if(samples.size()<12)return {};std::array<double,3> median{},result{};for(int c=0;c<3;++c){std::vector<double> values;for(auto value:samples)values.push_back(value[c]);auto mid=values.begin()+values.size()/2;std::nth_element(values.begin(),mid,values.end());median[c]=*mid;}
    const auto distance=[&](const auto& value){double d=0;for(int c=0;c<3;++c)d+=std::pow(value[c]-median[c],2);return d;};std::stable_sort(samples.begin(),samples.end(),[&](auto a,auto b){return distance(a)<distance(b);});const auto count=samples.size()/2;for(std::size_t i=0;i<count;++i)for(int c=0;c<3;++c)result[c]+=samples[i][c]/double(count);return {result[0],result[1],result[2]};
}
QJsonArray portrait_sample_skin_color(const QImage& source,QPoint position) {
    if(source.format()!=QImage::Format_RGBA32FPx4_Premultiplied || !source.rect().contains(position))return {};
    const QRect region=QRect(position-QPoint(2,2),QSize(5,5)).intersected(source.rect());
    std::vector<std::array<double,3>> samples;
    for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x){
        const auto* pixel=reinterpret_cast<const float*>(source.constScanLine(y))+x*4;
        if(pixel[3]<.5F)continue;
        std::array<double,3> color{pixel[0]/pixel[3],pixel[1]/pixel[3],pixel[2]/pixel[3]};
        if(std::all_of(color.begin(),color.end(),[](double v){return std::isfinite(v) && v>=0;}))samples.push_back(color);
    }
    if(samples.empty())return {};
    std::array<double,3> median{},color{};
    for(int c=0;c<3;++c){std::vector<double> channel;for(const auto& sample:samples)channel.push_back(sample[c]);std::nth_element(channel.begin(),channel.begin()+channel.size()/2,channel.end());median[c]=channel[channel.size()/2];}
    const auto distance=[&](const std::array<double,3>& sample){double result=0;for(int c=0;c<3;++c)result+=std::pow(sample[c]-median[c],2);return result;};
    std::stable_sort(samples.begin(),samples.end(),[&](const auto& a,const auto& b){return distance(a)<distance(b);});
    const auto count=std::max(std::size_t{1},samples.size()*3/4);
    for(std::size_t i=0;i<count;++i)for(int c=0;c<3;++c)color[c]+=samples[i][c]/double(count);
    return {color[0],color[1],color[2]};
}
QImage portrait_color_region(const QImage& source,const QJsonObject& recipe,QRect region,double amount,const std::atomic_bool* cancelled,const QImage& effect_mask) {
    region=region.intersected(source.rect());if(region.isEmpty())return {};
    const auto records=recipe["people"].toArray();std::vector<SkinPerson> people;bool active=false;
    const auto mask_size=source.size().scaled(768,768,Qt::KeepAspectRatio);
    for(const auto& value:records){const auto record=value.toObject();SkinPerson person;const auto settings=record["settings"].toObject(),params=settings["parameters"].toObject(),enabled=settings["enabled"].toObject();
        person.hairDetail=std::clamp(params["hair.detail"].toDouble()*.01,0.,1.);
        person.hairSmoothV2=settings["hairSmoothVersion"].toInt(1)>=2;
        person.balanceV2=settings["skinBalanceVersion"].toInt(1)>=2;
        person.chromaV2=settings["colorChromaVersion"].toInt(1)>=2;
        person.rednessV2=settings["rednessVersion"].toInt(1)>=2;
        person.oilV2=settings["oilVersion"].toInt(1)>=2;
        person.oilGloss=std::clamp(params["skin.oilGloss"].toDouble()*.01,0.,1.);
        person.smoothV2=settings["smoothVersion"].toInt(1)>=2;
        const auto smoothStyle=settings["smoothStyle"].toString();
        if(smoothStyle=="classic"){person.smoothFine=.85;person.smoothRadius=1.7;person.smoothEpsilon=.0006;person.poreRetention=.82;}
        else if(smoothStyle=="texture"){person.smoothFine=1.0;person.smoothRadius=1.8;person.smoothEpsilon=.00025;person.poreRetention=1.;}
        else if(smoothStyle=="native"){person.smoothFine=.95;person.smoothRadius=1.;person.smoothEpsilon=.00012;person.poreRetention=.98;}

        person.customV2=settings["skinCustomVersion"].toInt(1)>=2;
        person.uniformV2=settings["skinUniformVersion"].toInt(1)>=2;
        person.lightV2=settings["skinLightVersion"].toInt(1)>=2;
        person.bodyRetention*=std::clamp(settings["parameters"].toObject()["skin.retention"].toDouble(100.)*.01,0.,1.);
        person.poreRetention*=std::clamp(settings["parameters"].toObject()["skin.retention"].toDouble(100.)*.01,0.,1.);
        person.textureV2=settings["textureVersion"].toInt(1)>=2;
        person.body=QImage::fromData(QByteArray::fromBase64(record["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const bool allowed=!record["unmatched"].toBool() && !record["matchAmbiguous"].toBool();
        for(std::size_t i=0;i<keys.size();++i){const QString key=QLatin1String(keys[i]);const double v=params[key].toDouble();if(!std::isfinite(v) || std::abs(v)>100)throw EditError(QStringLiteral("肤质参数无效"));person.values[i]=allowed && !settings["disabledParameters"].toObject()[key].toBool() && enabled[key.section('.',0,0)].toBool(true)?v*.01*amount*std::clamp(settings["strength"].toDouble(100),0.,100.)*.01:0.;if(person.values[i]!=0){active=true;person.masks[i]=portrait_effect_mask(record,key,mask_size);}}
        const auto face=record["face"].toArray();
        if(person.lightV2 && !person.masks[4].isNull()){
            auto& mask=person.masks[4];cv::Mat bytes(mask.height(),mask.width(),CV_8U,mask.bits(),mask.bytesPerLine()),inside,distance;
            cv::threshold(bytes,inside,24,255,cv::THRESH_BINARY);cv::distanceTransform(inside,distance,cv::DIST_L2,3);
            const double feather=std::max(2.,face[2].toDouble()*mask.width()*.025);
            for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x){const double t=std::clamp(distance.at<float>(y,x)/feather,0.,1.);mask.scanLine(y)[x]=uchar(mask.constScanLine(y)[x]*t*t*(3-2*t));}
        }
        person.sigma=std::clamp(face[2].toDouble()*source.width()*.025,.7,12.);
        const auto body=record["body"].toArray();person.bodySigma=std::clamp(body.size()==4?body[2].toDouble()*source.width()*.008:person.sigma,.7,16.);
        const auto skin=portrait_effect_mask(record,"color.balance",mask_size);double weight{};
        person.skinReference=skin;
        if(person.values[12]!=0){auto foundationRecord=record;auto foundationSettings=settings;foundationSettings.remove("balanceReference");foundationRecord["settings"]=foundationSettings;const auto reference=portrait_balance_reference(source,foundationRecord);if(reference.size()==3)for(int c=0;c<3;++c)person.foundation[c]=reference[c].toDouble();}
        if(person.textureV2 && person.values[1]>0){
            std::vector<double> residuals;const int step=std::max(1,int(std::sqrt(double(source.width())*source.height()/3000.)));
            for(int y=1;y<source.height()-1;y+=step)for(int x=1;x<source.width()-1;x+=step){
                if(coverage(person.masks[1],x,y,source.size())<.8)continue;
                double laplacian=0;bool valid=true;constexpr int weights[]{1,-2,1};
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const auto* pixel=reinterpret_cast<const float*>(source.constScanLine(y+dy))+(x+dx)*4;
                    if(pixel[3]<.5F){valid=false;continue;}laplacian+=weights[dy+1]*weights[dx+1]*(.2627*pixel[0]+.6780*pixel[1]+.0593*pixel[2])/pixel[3];}
                if(valid)residuals.push_back(std::abs(laplacian)/6.);
            }
            if(residuals.size()>20){auto middle=residuals.begin()+residuals.size()/2;std::nth_element(residuals.begin(),middle,residuals.end());person.noise=*middle/.67448975;}
        }
        const QRect f(int(face[0].toDouble()*source.width()),int(face[1].toDouble()*source.height()),int(face[2].toDouble()*source.width()),int(face[3].toDouble()*source.height()));
        if(person.values[8]!=0)for(int y=std::max(0,f.top());y<std::min(source.height(),f.bottom());y+=std::max(1,f.height()/64))for(int x=std::max(0,f.left());x<std::min(source.width(),f.right());x+=std::max(1,f.width()/64)){
            const double mask=coverage(skin,x,y,source.size());if(mask<.75)continue;const auto* p=reinterpret_cast<const float*>(source.constScanLine(y))+x*4;if(p[3]<.01)continue;for(int c=0;c<3;++c)person.reference[c]+=p[c]/p[3]*mask;weight+=mask;}
        if(weight>0)for(auto& c:person.reference)c/=weight;
        if(person.balanceV2 && person.values[8]!=0){const auto reference=portrait_balance_reference(source,record);person.reference={};if(reference.size()==3)for(int c=0;c<3;++c)person.reference[c]=reference[c].toDouble();
            person.balanceKeep=QImage(mask_size,QImage::Format_Grayscale8);person.balanceKeep.fill(0);const auto chosen=settings["balanceReference"].toArray();
            for(int y=0;y<mask_size.height();++y)for(int x=0;x<mask_size.width();++x){const double px=double(x)/mask_size.width(),py=double(y)/mask_size.height();double d;
                if(chosen.size()==3){const double radius=chosen[2].toDouble()*std::min(source.width(),source.height());d=std::hypot((px-chosen[0].toDouble())*source.width(),(py-chosen[1].toDouble())*source.height())-radius;}
                else{const double dx=std::max({face[0].toDouble()-px,0.,px-face[0].toDouble()-face[2].toDouble()}),dy=std::max({face[1].toDouble()-py,0.,py-face[1].toDouble()-face[3].toDouble()});d=std::hypot(dx*source.width(),dy*source.height());}
                const double t=std::clamp(d/std::max(2.,face[2].toDouble()*source.width()*.06),0.,1.);person.balanceKeep.scanLine(y)[x]=uchar(255*(1-t*t*(3-2*t)));}
        }

        const auto color=settings["skinColor"].toArray();if(color.size()==3)for(int c=0;c<3;++c)person.custom[c]=color[c].toDouble();
        people.push_back(std::move(person));
    }
    auto result=portrait_repair_region(source,recipe,region,amount,cancelled,effect_mask);
    if(!active)return portrait_style_region(std::move(result),source.size(),recipe,region,amount,cancelled,effect_mask);
    if(!result.isDetached()){auto detached=allocate_image(result.size(),result.format());detached.setColorSpace(result.colorSpace());for(int y=0;y<result.height();++y)std::memcpy(detached.scanLine(y),result.constScanLine(y),std::size_t(result.width())*16);result=std::move(detached);}
    for(std::size_t person_index=0;person_index<people.size();++person_index){const auto& person=people[person_index];if(std::all_of(person.values.begin(),person.values.end(),[](double v){return v==0;}))continue;
        const double oilSigma=person.sigma*2.5;
        const double filterSigma=std::max(person.values[11]!=0?person.bodySigma:0.,(person.oilV2 && person.values[3]!=0) || (person.uniformV2 && person.values[2]!=0) || ((person.balanceV2 && person.values[8]!=0) || person.values[12]!=0)?oilSigma:person.sigma);
        const int halo=std::max({16,int(std::ceil(filterSigma*4))+4,2*int(std::ceil(person.sigma*person.smoothRadius))+int(std::ceil(person.smoothFine*4))+2});
        for(int top=region.top();top<=region.bottom();top+=384)for(int left=region.left();left<=region.right();left+=384){
            if(cancelled && cancelled->load())throw EditError(QStringLiteral("肤质处理已取消"));
            const QRect tile(left,top,std::min(384,region.right()-left+1),std::min(384,region.bottom()-top+1));const auto expanded=tile.adjusted(-halo,-halo,halo,halo).intersected(source.rect());
            cv::Mat input(expanded.height(),expanded.width(),CV_32FC4,const_cast<uchar*>(source.constScanLine(expanded.y()))+qint64(expanded.x())*16,source.bytesPerLine()),low,fine;
            cv::GaussianBlur(input,low,{0,0},person.sigma,person.sigma,cv::BORDER_REFLECT_101);cv::GaussianBlur(input,fine,{0,0},.85,.85,cv::BORDER_REFLECT_101);
            cv::Mat smoothBase,bodyBase,smoothFine,hairBase;
            if(person.hairSmoothV2 && person.values[10]!=0)hairBase=directional_hair_base(input,person.masks[10],expanded.topLeft(),source.size());
            if(person.values[11]!=0)bodyBase=guided_skin_base(fine,std::max(1,int(std::ceil(person.bodySigma*1.8))));
            if(person.smoothV2 && person.values[0]!=0){if(person.smoothFine==.85)smoothFine=fine;else cv::GaussianBlur(input,smoothFine,{0,0},person.smoothFine,person.smoothFine,cv::BORDER_REFLECT_101);smoothBase=guided_skin_base(smoothFine,std::max(1,int(std::ceil(person.sigma*person.smoothRadius))),person.smoothEpsilon);}
            cv::Mat oilReference;
            if((person.oilV2 && person.values[3]!=0) || (person.uniformV2 && person.values[2]!=0)){
                cv::Mat samples(expanded.height(),expanded.width(),CV_32FC4);
                for(int sy=0;sy<expanded.height();++sy)for(int sx=0;sx<expanded.width();++sx){
                    const auto sample=input.at<cv::Vec4f>(sy,sx);
                    const float weight=float(coverage(person.skinReference,sx+expanded.x(),sy+expanded.y(),source.size()));
                    samples.at<cv::Vec4f>(sy,sx)=sample*weight;
                }
                cv::GaussianBlur(samples,oilReference,{0,0},oilSigma,oilSigma,cv::BORDER_REFLECT_101);
            }
            cv::Mat balanceMean,balanceEnergy;
            if((person.balanceV2 && person.values[8]!=0) || person.values[12]!=0){cv::Mat samples(expanded.height(),expanded.width(),CV_32FC4),energy(expanded.height(),expanded.width(),CV_32FC1);
                for(int y=0;y<expanded.height();++y)for(int x=0;x<expanded.width();++x){const auto pixel=input.at<cv::Vec4f>(y,x);const double light=.2627*pixel[0]+.678*pixel[1]+.0593*pixel[2];const float w=light>.005 && pixel[3]>.5F?float(coverage(person.skinReference,x+expanded.x(),y+expanded.y(),source.size())):0.F;cv::Vec4f value{0,0,0,w};float e=0;for(int c=0;c<3;++c){const float chroma=float(pixel[c]/std::max(.005,light));value[c]=chroma*w;e+=chroma*chroma*w;}samples.at<cv::Vec4f>(y,x)=value;energy.at<float>(y,x)=e;}
                cv::GaussianBlur(samples,balanceMean,{0,0},oilSigma,oilSigma,cv::BORDER_REFLECT_101);cv::GaussianBlur(energy,balanceEnergy,{0,0},oilSigma,oilSigma,cv::BORDER_REFLECT_101);
            }
            cv::Mat redReference;
            if(person.rednessV2 && person.values[7]!=0){
                // Normalized skin-only chroma avoids sampling lips, hair or a dark background.
                cv::Mat samples(expanded.height(),expanded.width(),CV_32FC2);
                for(int sy=0;sy<expanded.height();++sy)for(int sx=0;sx<expanded.width();++sx){
                    const auto sample=input.at<cv::Vec4f>(sy,sx);
                    const double sampleLight=.2627*sample[0]+.6780*sample[1]+.0593*sample[2];
                    const double weight=sample[3]>.01F && sampleLight>.001?coverage(person.skinReference,sx+expanded.x(),sy+expanded.y(),source.size()):0.;
                    samples.at<cv::Vec2f>(sy,sx)={float(weight*(sample[0]-sample[1])/std::max(.001,sampleLight)),float(weight)};
                }
                cv::GaussianBlur(samples,redReference,{0,0},person.sigma,person.sigma,cv::BORDER_REFLECT_101);
            }
            for(int y=tile.top();y<=tile.bottom();++y){auto* target=reinterpret_cast<float*>(result.scanLine(y-region.y()));const auto* original=reinterpret_cast<const float*>(source.constScanLine(y));
                for(int x=tile.left();x<=tile.right();++x){const auto* p=original+x*4;auto* out=target+(x-region.x())*4;if(p[3]<=.00001F)continue;
                    double protect=effect_mask.isNull()?1:coverage(effect_mask,x,y,source.size());for(std::size_t other=0;other<people.size();++other)if(other!=person_index)protect*=1-coverage(people[other].body,x,y,source.size());
                    std::array<double,13> value{};for(std::size_t i=0;i<keys.size();++i)value[i]=person.values[i]*coverage(person.masks[i],x,y,source.size())*protect;
                    if(std::all_of(value.begin(),value.end(),[](double v){return v==0;}))continue;
                    std::array<double,3> rgb{},base{},high{};const auto l=low.at<cv::Vec4f>(y-expanded.y(),x-expanded.x()),f=fine.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());
                    for(int c=0;c<3;++c){rgb[c]=out[c]/p[3];base[c]=l[c]/std::max(.00001F,l[3]);high[c]=p[c]/p[3]-f[c]/std::max(.00001F,f[3]);}
                    const double light=luminance(rgb),base_light=luminance(base),edge=std::exp(-std::pow((light-base_light)/(.035+light*.12),2));
                    std::array<double,3> skinBase=base,bodyBaseColor=base,smoothHigh=high;
                    if(value[11]!=0){const auto guided=bodyBase.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());for(int c=0;c<3;++c)bodyBaseColor[c]=guided[c]/std::max(.00001F,guided[3]);}
                    if(person.smoothV2 && value[0]!=0){const auto guided=smoothBase.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());const auto smoothSample=smoothFine.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());for(int c=0;c<3;++c){skinBase[c]=guided[c]/std::max(.00001F,guided[3]);smoothHigh[c]=p[c]/p[3]-smoothSample[c]/std::max(.00001F,smoothSample[3]);}}
                    for(int c=0;c<3;++c){
                        if(person.smoothV2){
                            // Keep the pore-scale residual while reducing the middle band;
                            // luminance guidance retains structural edges in the base.
                            rgb[c]+=(skinBase[c]+smoothHigh[c]*person.poreRetention-rgb[c])*value[0]*.75;
                        }else rgb[c]+=(base[c]+high[c]*.9-rgb[c])*value[0]*edge*.75;
                        rgb[c]+=(bodyBaseColor[c]+high[c]*person.bodyRetention-rgb[c])*value[11]*.75;
                        if(!person.textureV2)rgb[c]+=high[c]*value[1]*.5;
                        if(person.hairSmoothV2 && value[10]!=0){const auto hair=hairBase.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());rgb[c]+=(hair[c]/std::max(.00001F,hair[3])-p[c]/p[3])*value[10]*.65*(1-person.hairDetail*.8);}
                        else if(!person.hairSmoothV2)rgb[c]+=(base[c]+high[c]*.85-rgb[c])*value[10]*edge*.45*(1-person.hairDetail*.8);
                        if(!person.uniformV2)rgb[c]+=(base[c]*light/std::max(.005,base_light)-rgb[c])*value[2]*.35*edge;}
                    if(person.uniformV2 && value[2]!=0){
                        const auto reference=oilReference.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());
                        if(reference[3]>.15F && f[3]>.01F){
                            std::array<double,3> nearby{},smooth{};
                            for(int c=0;c<3;++c){nearby[c]=reference[c]/reference[3];smooth[c]=f[c]/f[3];}
                            const double nearbyY=luminance(nearby),smoothY=luminance(smooth),currentY=luminance(rgb);
                            if(nearbyY>.005 && smoothY>.005 && currentY>.005){
                                double error=0;for(int c=0;c<3;++c)error+=std::pow(nearby[c]/nearbyY-smooth[c]/smoothY,2);
                                const double confidence=std::clamp((std::sqrt(error)-.025)/.18,0.,1.);
                                const double strength=value[2]*confidence*.55;
                                // Correct the low-frequency chroma residual, retaining original pore detail.
                                for(int c=0;c<3;++c)rgb[c]+=currentY*(nearby[c]/nearbyY-smooth[c]/smoothY)*strength;
                                const double delta=std::clamp((nearbyY-smoothY)*strength*.3,-currentY*.08,currentY*.08);
                                for(auto& channel:rgb)channel*=1+delta/currentY;
                            }
                        }
                    }
                    if(person.textureV2 && value[1]!=0){
                        const double currentLight=luminance(rgb),detail=luminance(high);
                        const double retained=value[1]>0?std::copysign(std::max(0.,std::abs(detail)-person.noise*2),detail):detail;
                        const double delta=std::clamp(retained*value[1]*.5,-currentLight*.15,currentLight*.15);
                        if(currentLight>1e-8)for(auto& channel:rgb)channel*=1+delta/currentLight;
                    }
                    const double excess=std::max(0.,light-base_light);
                    double oil=std::min(.25,std::max(0.,light-.22)*.25+excess*.4)*value[3];
                    if(person.oilV2){
                        oil=0;
                        if(value[3]!=0){
                            const auto reference=oilReference.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());
                            if(reference[3]>.1F){
                                std::array<double,3> neighborhood{},smooth{};
                                for(int c=0;c<3;++c){neighborhood[c]=reference[c]/reference[3];smooth[c]=f[c]/std::max(.00001F,f[3]);}
                                const double referenceLight=luminance(neighborhood),smoothLight=luminance(smooth);
                                const auto saturation=[](const std::array<double,3>& color){const auto range=std::minmax_element(color.begin(),color.end());return (*range.second-*range.first)/std::max(.005,*range.second);};
                                // A specular candidate is both brighter and less chromatic than nearby skin.
                                // A diffuse light gradient alone must not lower exposure across the face.
                                const double confidence=std::clamp((saturation(neighborhood)-saturation(smooth)-.015)/.15,0.,1.);
                                const double residual=std::max(0.,smoothLight-referenceLight-.012-referenceLight*(.05+.15*person.oilGloss));
                                oil=std::min(light*(.35-.1*person.oilGloss),residual*.85)*confidence*value[3];
                            }
                        }
                    }
                    for(auto& c:rgb)c*=1-oil/std::max(.05,light);
                    double lightGain=std::exp2(value[4]);
                    if(person.lightV2){
                        // A common RGB gain retains chromatic ratios; the shoulder anchors diffuse white.
                        const double peak=std::clamp(*std::max_element(rgb.begin(),rgb.end()),0.,1.);
                        lightGain/=1+(lightGain-1)*peak;
                    }
                    for(auto& c:rgb)c*=lightGain;
                    if(person.chromaV2){
                        const double originalLight=luminance(rgb);
                        const double redResidual=std::max(0.,rgb[0]/std::max(.01,originalLight)-base[0]/std::max(.01,base_light)-.03);
                        const double rosy=value[6]*(value[6]>0?std::exp(-redResidual*12.):1.);
                        rgb[0]*=std::exp2(.22*value[5]+.10*rosy);
                        rgb[1]*=std::exp2(-.025*rosy);
                        rgb[2]*=std::exp2(-.22*value[5]-.015*rosy);
                        const double adjustedLight=luminance(rgb);
                        if(adjustedLight>1e-12)for(auto& c:rgb)c*=originalLight/adjustedLight;
                    }else{
                        rgb[0]*=1+value[5]*.08+value[6]*.045;rgb[2]*=1-value[5]*.08;rgb[1]*=1-value[6]*.025;
                    }
                    if(person.rednessV2 && value[7]!=0){
                        const auto reference=redReference.at<cv::Vec2f>(y-expanded.y(),x-expanded.x());
                        const double sourceLight=.2627*p[0]+.6780*p[1]+.0593*p[2];
                        if(reference[1]>.1F && sourceLight>.001){
                            const double residual=std::max(0.,(p[0]-p[1])/sourceLight-reference[0]/reference[1]-.04);
                            const double currentLight=luminance(rgb);
                            const double correction=std::min(rgb[0]*.8,residual*currentLight/(1+.2627/.6780)*.85*value[7]);
                            rgb[0]-=correction;rgb[1]+=correction*.2627/.6780;
                        }
                    }else if(!person.rednessV2){
                        const double redness=std::max(0.,rgb[0]-rgb[1]*1.35);rgb[0]-=redness*value[7]*.4;
                    }
                    for(int mode=0;mode<2;++mode){const auto& color=mode==0?person.reference:person.custom;const double target_light=luminance(color),strength=value[std::size_t(8+mode)]*(mode==0?.35:.65);if(target_light>.00001){const double current_light=luminance(rgb);if(mode==0 && person.balanceV2){
                            const auto mean=balanceMean.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());if(mean[3]>.3F){std::array<double,3> nearby{},delta{};double squared=0,error=0;for(int c=0;c<3;++c){nearby[c]=mean[c]/mean[3];squared+=nearby[c]*nearby[c];error+=std::pow(color[c]/target_light-nearby[c],2);}const double variance=std::max(0.,balanceEnergy.at<float>(y-expanded.y(),x-expanded.x())/mean[3]-squared);
                                double gain=value[8]*.7*(1-coverage(person.balanceKeep,x,y,source.size()))*std::exp(-variance/.015)*std::clamp((std::sqrt(error)-.02)/.12,0.,1.);for(int c=0;c<3;++c){delta[c]=current_light*(color[c]/target_light-nearby[c]);if(delta[c]<0)gain=std::min(gain,rgb[c]/-delta[c]);}for(int c=0;c<3;++c)rgb[c]+=delta[c]*gain;}
                        }else if(mode==1 && person.customV2){
                            const double fineLight=(.2627*f[0]+.678*f[1]+.0593*f[2])/std::max(.00001F,f[3]);
                            if(fineLight>.005){
                                std::array<double,3> delta{};double safeStrength=strength;
                                for(int c=0;c<3;++c){
                                    const double baseChroma=f[c]/std::max(.00001F,f[3])/fineLight;
                                    delta[c]=current_light*(color[c]/target_light-baseChroma);
                                    if(delta[c]<0)safeStrength=std::min(safeStrength,rgb[c]/-delta[c]);
                                }
                                for(int c=0;c<3;++c)rgb[c]+=delta[c]*safeStrength;
                            }
                        }else for(int c=0;c<3;++c)rgb[c]+=(color[c]*current_light/target_light-rgb[c])*strength;}}
                    if(value[12]>0){
                        const auto mean=balanceMean.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());const double targetY=luminance(person.foundation),currentY=luminance(rgb);
                        if(mean[3]>.3F && targetY>.005 && currentY>.005){
                            std::array<double,3> delta{};double squared=0,error=0;
                            for(int c=0;c<3;++c){const double chroma=mean[c]/mean[3],referenceChroma=person.foundation[c]/targetY;squared+=chroma*chroma;error+=std::pow(referenceChroma-chroma,2);delta[c]=currentY*(referenceChroma-chroma);}
                            const double variance=std::max(0.,balanceEnergy.at<float>(y-expanded.y(),x-expanded.x())/mean[3]-squared);
                            double gain=value[12]*.55*std::exp(-variance/.04)*std::clamp((std::sqrt(error)-.015)/.10,0.,1.);
                            for(int c=0;c<3;++c)if(delta[c]<0)gain=std::min(gain,rgb[c]/-delta[c]);
                            // Only the low-frequency chroma field changes. Its luminance
                            // component is zero, retaining shading, pores and freckles.
                            for(int c=0;c<3;++c)rgb[c]+=gain*delta[c];
                        }
                    }
                    for(int c=0;c<3;++c)out[c]=float(std::max(0.,rgb[c])*p[3]);
                }
            }
        }
    }
    return portrait_style_region(std::move(result),source.size(),recipe,region,amount,cancelled,effect_mask);
}
}
