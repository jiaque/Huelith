#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/healing.h"
#include "huelith/editing/project.h"
#include "huelith/editing/image_storage.h"
#include <QPainter>
#include <QPainterPath>
#include <QJsonArray>
#include <QLineF>
#include <QFile>
#include <QCryptographicHash>
#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace huelith::editing {
namespace {
QPointF landmark(const QJsonArray& points,int i,QSize size) {const auto p=points[i].toArray();return {p[0].toDouble()*size.width(),p[1].toDouble()*size.height()};}
QImage polygon(const QJsonArray& points,std::initializer_list<int> indices,QSize size) {
    QImage image(size,QImage::Format_ARGB32);image.fill(Qt::black);QPolygonF shape;for(int i:indices)shape<<landmark(points,i,size);
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawPolygon(shape);painter.end();return image.convertToFormat(QImage::Format_Grayscale8);
}
double at(const QImage& mask,int x,int y,QSize size){if(mask.isNull())return 0;return mask.constScanLine(std::clamp(int(double(y)/size.height()*mask.height()),0,mask.height()-1))[std::clamp(int(double(x)/size.width()*mask.width()),0,mask.width()-1)]/255.;}
double lum(const float* p){return .2627*p[0]+.678*p[1]+.0593*p[2];}
QImage copy_region(const QImage& source,QRect region){auto image=allocate_image(region.size(),source.format());image.setColorSpace(source.colorSpace());for(int y=0;y<region.height();++y)std::memcpy(image.scanLine(y),source.constScanLine(region.y()+y)+qint64(region.x())*16,std::size_t(region.width())*16);return image;}
}
QImage portrait_makeup_repair_mask(const QJsonObject& person,const QString& part,QSize size){
    const auto lm=person["landmarks"].toArray();QImage empty(size,QImage::Format_Grayscale8);empty.fill(0);if(lm.size()!=66)return empty;
    if(part=="lip.upper")return polygon(lm,{58,48,49,50,51,52,62,61,60,59},size);
    if(part=="lip.lower")return polygon(lm,{58,65,64,63,62,53,54,55,56,57},size);
    if(part=="brow.left" || part=="brow.right" || part=="shadow.left" || part=="shadow.right"){
        const bool left=part.endsWith(".left");const int eye=left?42:36,brow=left?22:17;const double fw=person["face"].toArray()[2].toDouble()*size.width();
        QImage shape(size,QImage::Format_ARGB32);shape.fill(Qt::black);QPainter painter(&shape);painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(Qt::white);painter.setPen(Qt::NoPen);
        if(part.startsWith("brow.")){QPainterPath path;path.moveTo(landmark(lm,brow,size));for(int i=brow+1;i<brow+5;++i)path.lineTo(landmark(lm,i,size));painter.setBrush(Qt::NoBrush);painter.setPen(QPen(Qt::white,fw*.035,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(path);}
        else{const auto a=landmark(lm,eye,size),b=landmark(lm,eye+3,size),center=(a+b)*.5;const double width=QLineF(a,b).length();auto axis=(b-a)/std::max(1.,width);if(axis.x()<0)axis=-axis;const QPointF up(axis.y(),-axis.x());painter.save();painter.translate(center+up*(width*.22));painter.rotate(std::atan2(axis.y(),axis.x())*180/3.141592653589793);painter.drawEllipse(QPointF{},width*.52,width*.30);painter.restore();QPolygonF outline;for(int i=eye;i<eye+6;++i)outline<<landmark(lm,i,size);painter.setBrush(Qt::black);painter.drawPolygon(outline);}
        painter.end();return shape.convertToFormat(QImage::Format_Grayscale8);
    }
    return empty;
}
QJsonObject portrait_makeup_repair(const QImage& source,const QJsonObject& person,const QJsonObject& object,bool manual_source){
    auto input=object;for(const auto* key:{"kind","id","wrinkleMethod","makeupPart","validatedCandidateCoverage","requestedPart","hairSourceAngle","hairSourceScale"})input.remove(key);auto stroke=healing_from_json(input);
    const auto size=source.size().scaled(2048,2048,Qt::KeepAspectRatio);QString part;QImage mask;double best=.25;
    for(const auto* key:{"lip.upper","lip.lower","brow.left","brow.right","shadow.left","shadow.right"}){const auto candidate=portrait_makeup_repair_mask(person,QLatin1String(key),size);const double weight=at(candidate,int(stroke.points.front().x()*source.width()),int(stroke.points.front().y()*source.height()),source.size());if(weight>best){best=weight;part=QLatin1String(key);mask=candidate;}}
    if(!object["requestedPart"].toString().isEmpty() && !part.startsWith(object["requestedPart"].toString()+"."))throw EditError("请在所选妆容部位内涂抹，或切换对应的部位卡片");
    if(part.isEmpty())throw EditError("请在嘴唇、眉毛或眼影内涂抹；普通皮肤请用取样修复笔");
    if(!manual_source){
        cv::Mat raw(mask.height(),mask.width(),CV_8U,mask.bits(),mask.bytesPerLine()),binary,distance;cv::threshold(raw,binary,200,255,cv::THRESH_BINARY);cv::distanceTransform(binary,distance,cv::DIST_L2,5);
        const int x=std::clamp(int(stroke.points.front().x()*mask.width()),0,mask.width()-1),y=std::clamp(int(stroke.points.front().y()*mask.height()),0,mask.height()-1);
        const double available=std::max(0.,double(distance.at<float>(y,x))-1.)*.8/std::min(mask.width(),mask.height());
        if(available<.75/std::min(source.width(),source.height()))throw EditError("此处太靠近妆容边缘，请向内部取样或放大后手动指定来源");
        stroke.radius=std::min(stroke.radius,available);
        for(;;){try{stroke.offset=automatic_healing_offset(source,stroke,0,mask,nullptr,true);break;}catch(const EditError&){const double smaller=stroke.radius*.85;if(smaller<.75/std::min(source.width(),source.height()))throw;stroke.radius=smaller;}}
    }
    // Both the destination and the entire swept source stay in the same semantic part.
    const double radius=stroke.radius*std::min(source.width(),source.height());
    for(std::size_t i=0;i<stroke.points.size();++i){const auto a=stroke.points[i?i-1:i],b=stroke.points[i];const double distance=std::hypot((b.x()-a.x())*source.width(),(b.y()-a.y())*source.height());const int steps=std::max(1,int(std::ceil(distance/std::max(1.,radius*.25))));
        for(int j=0;j<=steps;++j){const auto center=a+(b-a)*(double(j)/steps);for(const auto delta:{QPointF{},stroke.offset})for(int ring=0;ring<3;++ring)for(int edge=0;edge<24;++edge){const double angle=edge*3.141592653589793/12;const double x=(center.x()+delta.x())*source.width()+std::cos(angle)*radius*ring*.5,y=(center.y()+delta.y())*source.height()+std::sin(angle)*radius*ring*.5;if(x<0 || y<0 || x>=source.width() || y>=source.height() || mask.constScanLine(std::clamp(int(y*mask.height()/source.height()),0,mask.height()-1))[std::clamp(int(x*mask.width()/source.width()),0,mask.width()-1)]<90)throw EditError("来源与目标必须完整位于同一妆容部位，请缩小画笔或 Alt 重新取样");}}
    }
    auto result=healing_json(stroke);result["makeupPart"]=part;return result;
}
QJsonArray portrait_conceal_reference(const QImage& source,const QJsonObject& person,QPointF point,double radius){
    if(source.format()!=QImage::Format_RGBA32FPx4_Premultiplied)return {};
    const auto skin=portrait_effect_mask(person,"skin",source.size().scaled(768,768,Qt::KeepAspectRatio));std::vector<std::array<double,3>> samples;
    const double reach=std::max(3.,radius);const QPointF center(point.x()*source.width(),point.y()*source.height());
    for(int ring=0;ring<3;++ring)for(int angle=0;angle<48;++angle){const double theta=angle*6.283185307179586/48.;const auto q=center+QPointF(std::cos(theta),std::sin(theta))*(reach*(1.4+ring*.5));const int x=int(q.x()),y=int(q.y());
        if(!source.rect().contains(x,y) || at(skin,x,y,source.size())<.85)continue;const auto* p=reinterpret_cast<const float*>(source.constScanLine(y))+x*4;const double light=lum(p)/std::max(.001F,p[3]);
        if(p[3]<.9F || light<.015 || light>.85)continue;samples.push_back({p[0]/p[3],p[1]/p[3],p[2]/p[3]});
    }
    if(samples.size()<12)return {};
    std::array<double,3> median{};for(int c=0;c<3;++c){std::vector<double> channel;for(const auto& sample:samples)channel.push_back(sample[c]);auto mid=channel.begin()+channel.size()/2;std::nth_element(channel.begin(),mid,channel.end());median[c]=*mid;}
    const auto distance=[&](const auto& sample){double sum=0;for(int c=0;c<3;++c)sum+=std::pow(sample[c]-median[c],2);return sum;};std::stable_sort(samples.begin(),samples.end(),[&](const auto& a,const auto& b){return distance(a)<distance(b);});
    std::array<double,3> result{};const auto count=samples.size()/2;for(std::size_t i=0;i<count;++i)for(int c=0;c<3;++c)result[c]+=samples[i][c]/count;return {result[0],result[1],result[2]};
}
namespace {
QJsonObject brow_material_analysis(const QImage& image,const QJsonObject& person){
    QJsonObject result;const auto points=person["landmarks"].toArray();if(points.size()!=66)return result;const auto size=image.size();const double faceWidth=person["face"].toArray()[2].toDouble()*size.width();
    cv::Mat rgb(size.height(),size.width(),CV_8UC3,const_cast<uchar*>(image.constBits()),image.bytesPerLine()),gray;cv::cvtColor(rgb,gray,cv::COLOR_RGB2GRAY);
    for(int side=0;side<2;++side){QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);const int first=side?22:17;
        for(int segment=0;segment<4;++segment){const auto a=landmark(points,first+segment,size),b=landmark(points,first+segment+1,size);const auto delta=b-a;const double length=std::hypot(delta.x(),delta.y());if(length<1)continue;QPointF normal(-delta.y()/length,delta.x()/length);
            const auto bounds=QRectF(a,b).normalized().adjusted(-faceWidth*.04,-faceWidth*.04,faceWidth*.04,faceWidth*.04).toAlignedRect().intersected(image.rect());
            for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){const auto q=QPointF(x,y);const double t=std::clamp(QPointF::dotProduct(q-a,delta)/(length*length),0.,1.);const auto center=a+delta*t;const double distance=QLineF(q,center).length();if(distance>faceWidth*.03)continue;std::vector<int> skin;
                for(double d:{-.055,-.045,.045,.055}){const auto p=center+normal*(faceWidth*d);const int xx=int(p.x()),yy=int(p.y());if(xx>=0 && yy>=0 && xx<size.width() && yy<size.height())skin.push_back(gray.at<uchar>(yy,xx));}if(skin.size()<2)continue;std::sort(skin.begin(),skin.end());const double reference=skin[skin.size()*3/4],darkness=(reference-gray.at<uchar>(y,x))/std::max(20.,reference);const double coverage=std::clamp((darkness-.08)/.45,0.,1.)*std::clamp((faceWidth*.03-distance)/(faceWidth*.01),0.,1.);mask.scanLine(y)[x]=std::max(mask.constScanLine(y)[x],uchar(coverage*255));}
        }result[side?"left":"right"]=portrait_mask_png(mask);
    }return result;
}
QJsonObject iris_material_analysis(const QImage& image,const QJsonObject& person){
    QJsonObject result;const auto points=person["landmarks"].toArray();if(points.size()!=66)return result;
    const auto size=image.size();cv::Mat rgb(size.height(),size.width(),CV_8UC3,const_cast<uchar*>(image.constBits()),image.bytesPerLine()),gray;cv::cvtColor(rgb,gray,cv::COLOR_RGB2GRAY);
    for(int side=0;side<2;++side){const int first=side?42:36;const QString suffix=side?".left":".right";const auto outline=side?polygon(points,{42,43,44,45,46,47},size):polygon(points,{36,37,38,39,40,41},size);
        const auto a=landmark(points,first,size),b=landmark(points,first+3,size);const double width=QLineF(a,b).length();QImage mask(size,QImage::Format_Grayscale8),visible=mask.copy();mask.fill(0);visible.fill(0);
        const auto box=QRectF((a+b)*.5-QPointF(width,width),QSizeF(width*2,width*2)).toAlignedRect().intersected(image.rect());std::vector<int> lights;
        for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x)if(outline.constScanLine(y)[x]>240)lights.push_back(gray.at<uchar>(y,x));
        bool valid=false;cv::RotatedRect ellipse;
        if(width>=10 && lights.size()>30){
            const auto center=(a+b)*.5;const auto axis=(b-a)/width;const QPointF normal(-axis.y(),axis.x());double best=10;
            const auto sample=[&](QPointF q)->double{const int x=int(std::round(q.x())),y=int(std::round(q.y()));if(x<0 || y<0 || x>=size.width() || y>=size.height() || outline.constScanLine(y)[x]<240)return -1;return gray.at<uchar>(y,x);};
            const double step=std::max(1.,width/45.);
            for(double offset=-width*.25;offset<=width*.25;offset+=step)for(double vertical=-width*.08;vertical<=width*.08;vertical+=step){const auto c=center+axis*offset+normal*vertical;if(sample(c)<0)continue;
                for(double rx=width*.12;rx<=width*.27;rx+=step)for(double aspect:{.8,1.,1.2}){const double ry=rx*aspect;double total=0;int support=0,leftSupport=0,rightSupport=0;
                    for(int i=0;i<24;++i){const double theta=i*6.283185307179586/24.;const auto radial=axis*(rx*std::cos(theta))+normal*(ry*std::sin(theta));const double inner=sample(c+radial*.78),outer=sample(c+radial*1.18);if(inner<0 || outer<0)continue;const double contrast=outer-inner;if(contrast<-8)total-=15;else total+=std::min(80.,contrast);++support;if(std::cos(theta)<-.5 && contrast>8)++leftSupport;if(std::cos(theta)>.5 && contrast>8)++rightSupport;}
                    if(support<6 || leftSupport<2 || rightSupport<2)continue;const double score=total/support*std::min(1.,support/12.);if(score>best){best=score;ellipse=cv::RotatedRect({float(c.x()),float(c.y())},{float(rx*2),float(ry*2)},float(std::atan2(axis.y(),axis.x())*180/3.141592653589793));valid=true;}
                }
            }
        }
        QJsonObject geometry{{"valid",valid}};
        if(valid){const double angle=ellipse.angle*3.141592653589793/180.,cs=std::cos(angle),sn=std::sin(angle),rx=ellipse.size.width*.5,ry=ellipse.size.height*.5;
            std::vector<int> pupilLights;for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){const double dx=x-ellipse.center.x,dy=y-ellipse.center.y,r=std::hypot((dx*cs+dy*sn)/rx,(-dx*sn+dy*cs)/ry);if(r<.7 && outline.constScanLine(y)[x]>240)pupilLights.push_back(gray.at<uchar>(y,x));}
            std::sort(pupilLights.begin(),pupilLights.end());const int dark=pupilLights.empty()?0:pupilLights[pupilLights.size()/5],median=pupilLights.empty()?0:pupilLights[pupilLights.size()/2];
            for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){if(outline.constScanLine(y)[x]<240)continue;const double dx=x-ellipse.center.x,dy=y-ellipse.center.y,r=std::hypot((dx*cs+dy*sn)/rx,(-dx*sn+dy*cs)/ry);const double edge=std::clamp((1-r)*12.,0.,1.);if(edge<=0)continue;visible.scanLine(y)[x]=uchar(255*edge);
                const int light=gray.at<uchar>(y,x);const double pupil=std::clamp((r-.28)*10.,0.,1.);const double highlight=1-std::clamp((light-std::max(median+30,190))/35.,0.,1.);const double darkPupil=r<.6 && median-dark>8?std::clamp((light-dark)/double(std::max(8,median-dark)),0.,1.):1.;mask.scanLine(y)[x]=uchar(255*edge*pupil*highlight*darkPupil);}
            geometry["ellipse"]=QJsonArray{ellipse.center.x/size.width(),ellipse.center.y/size.height(),rx/std::min(size.width(),size.height()),ry/std::min(size.width(),size.height()),ellipse.angle};
        }
        result["iris"+suffix]=portrait_mask_png(mask);result["cornea"+suffix]=portrait_mask_png(visible);result["geometry"+suffix]=geometry;
    }return result;
}
}
static QJsonObject lip_lighting_analysis(const QImage& source,const QJsonObject& person){
    const auto points=person["landmarks"].toArray();if(points.size()!=66)return {};
    const auto size=source.size().scaled(1536,1536,Qt::KeepAspectRatio);const auto linear=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    auto local=person;auto settings=person["settings"].toObject();settings["lipMaterialVersion"]=2;settings["lipMaterial"]="sheer";settings["parameters"]=QJsonObject{};local["settings"]=settings;
    const auto mask=portrait_style_mask(local,"makeup.lip",size);QImage matte(size,QImage::Format_Grayscale8),gloss(size,QImage::Format_Grayscale8);matte.fill(0);gloss.fill(0);
    const auto a=landmark(points,58,size),b=landmark(points,62,size),chord=b-a;const double width=std::hypot(chord.x(),chord.y());if(width<6)return {};
    const auto axis=chord/width;QPointF down(-axis.y(),axis.x());if(down.y()<0)down=-down;const auto center=(landmark(points,55,size)+landmark(points,64,size))*.5;
    cv::Mat weights(size.height(),size.width(),CV_32F),weighted(weights.size(),CV_32F),luminance(weights.size(),CV_32F);
    double sum=0,count=0,saturationSum=0;for(int y=0;y<size.height();++y){const auto* pixels=reinterpret_cast<const float*>(linear.constScanLine(y));for(int x=0;x<size.width();++x){const auto* p=pixels+x*4;const float w=mask.constScanLine(y)[x]/255.F,l=float(p[3]>.001F?lum(p)/p[3]:0);weights.at<float>(y,x)=w;weighted.at<float>(y,x)=l*w;luminance.at<float>(y,x)=l;sum+=l*w;count+=w;const double peak=std::max({p[0],p[1],p[2]});saturationSum+=(peak-std::min({p[0],p[1],p[2]}))/std::max(.001,peak)*w;}}
    if(count<4)return {};const double average=sum/count,averageSaturation=saturationSum/count;
    cv::GaussianBlur(weights,weights,{0,0},std::max(1.,width*.06));cv::GaussianBlur(weighted,weighted,{0,0},std::max(1.,width*.06));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){if(!mask.constScanLine(y)[x])continue;const double l=luminance.at<float>(y,x),diffuse=weighted.at<float>(y,x)/std::max(.001F,weights.at<float>(y,x));
        const auto* pixel=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;const double peak=std::max({pixel[0],pixel[1],pixel[2]}),saturation=(peak-std::min({pixel[0],pixel[1],pixel[2]}))/std::max(.001,peak);const double neutral=std::clamp((averageSaturation-saturation+.03)/.20,0.,1.);const double excess=std::clamp((l-diffuse-average*.04)/std::max(.02,l),0.,.65)*neutral;matte.scanLine(y)[x]=uchar(std::lround(excess*255));
        const auto delta=QPointF(x+.5,y+.5)-center;const double u=QPointF::dotProduct(delta,axis)/width,v=QPointF::dotProduct(delta,down)/width;
        const double surface=std::exp(-std::pow(u/.23,2)-std::pow(v/.035,2));
        const double lighting=std::clamp(diffuse/std::max(.01,average),.25,1.5);const double shine=std::clamp(.65*surface*lighting+excess*.7,0.,1.);gloss.scanLine(y)[x]=uchar(std::lround(shine*255));
    }
    return {{"matte",portrait_mask_png(matte)},{"gloss",portrait_mask_png(gloss)}};
}
static QJsonArray hair_flow(const QImage& linear,const QImage& hair){
    const auto size=linear.size();cv::Mat gray(size.height(),size.width(),CV_32F);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;gray.at<float>(y,x)=float(p[3]>.01F?lum(p)/p[3]:0);}
    cv::GaussianBlur(gray,gray,{0,0},.8);cv::Mat gx,gy;cv::Sobel(gray,gx,CV_32F,1,0);cv::Sobel(gray,gy,CV_32F,0,1);cv::Mat xx=gx.mul(gx),xy=gx.mul(gy),yy=gy.mul(gy);for(auto* m:{&xx,&xy,&yy})cv::GaussianBlur(*m,*m,{0,0},3.);
    cv::Mat bytes(hair.height(),hair.width(),CV_8U,const_cast<uchar*>(hair.constBits()),hair.bytesPerLine());const auto box=cv::boundingRect(bytes);QJsonArray curves;if(box.empty())return curves;const double step=std::max(1.5,box.width*.008);
    auto direction=[&](QPointF p,QPointF previous){const int x=int(p.x()),y=int(p.y());if(x<1 || y<1 || x>=size.width()-1 || y>=size.height()-1 || hair.constScanLine(y)[x]<150)return QPointF{};const double a=xx.at<float>(y,x),b=xy.at<float>(y,x),c=yy.at<float>(y,x),energy=a+c,coherence=std::hypot(a-c,2*b)/std::max(1e-9,energy);if(energy<1e-6 || coherence<.18)return QPointF{};const double angle=.5*std::atan2(2*b,a-c)+1.5707963267948966;QPointF d(std::cos(angle),std::sin(angle));if(QPointF::dotProduct(d,previous)<0)d=-d;return d;};
    for(unsigned i=0;i<144;++i){unsigned hash=i*747796405u+2891336453u;hash=((hash>>((hash>>28)+4))^hash)*277803737u;hash=(hash>>22)^hash;const QPointF seed(box.x+((i%12)+.2+double(hash&255)/255.*.6)*box.width/12.,box.y+((i/12)+.2+double((hash>>8)&255)/255.*.6)*box.height/12.);const auto initial=direction(seed,{});if(initial.isNull())continue;std::vector<QPointF> backward,forward;
        for(int sign:{-1,1}){auto p=seed,previous=initial*sign;auto& points=sign<0?backward:forward;for(int n=0;n<70;++n){auto d=direction(p,previous);if(d.isNull())break;const auto mid=direction(p+d*(step*.5),d);if(mid.isNull())break;p+=mid*step;if(p.x()<0 || p.y()<0 || p.x()>=size.width() || p.y()>=size.height())break;points.push_back(p);previous=mid;}}
        if(backward.size()+forward.size()<8)continue;QJsonArray points;for(auto p=backward.rbegin();p!=backward.rend();++p)points.append(QJsonArray{p->x()/size.width(),p->y()/size.height()});points.append(QJsonArray{seed.x()/size.width(),seed.y()/size.height()});for(const auto& p:forward)points.append(QJsonArray{p.x()/size.width(),p.y()/size.height()});curves.append(QJsonObject{{"rank",double((hash>>16)&65535)/65535.},{"points",points}});
    }return curves;
}
// Local two-color matting in the uncertain boundary band. Reliable interior
// and exterior samples are mandatory; ambiguous colors retain the input alpha.
static QJsonObject refine_hair_alpha(const QImage& source,const QImage& hair){
    const auto size=source.size();cv::Mat mask(size.height(),size.width(),CV_8U,const_cast<uchar*>(hair.constBits()),hair.bytesPerLine()),fg,bg;
    cv::threshold(mask,fg,230,255,cv::THRESH_BINARY);cv::threshold(mask,bg,20,255,cv::THRESH_BINARY_INV);
    const auto kernel=cv::getStructuringElement(cv::MORPH_ELLIPSE,{5,5});cv::erode(fg,fg,kernel);cv::erode(bg,bg,kernel);
    cv::Mat f(size.height(),size.width(),CV_32FC4),b(size.height(),size.width(),CV_32FC4);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(source.constScanLine(y))+x*4;const float fw=p[3]>.95F?fg.at<uchar>(y,x)/255.F:0,bw=p[3]>.95F?bg.at<uchar>(y,x)/255.F:0;for(int c=0;c<3;++c){f.at<cv::Vec4f>(y,x)[c]=p[c]/std::max(.001F,p[3])*fw;b.at<cv::Vec4f>(y,x)[c]=p[c]/std::max(.001F,p[3])*bw;}f.at<cv::Vec4f>(y,x)[3]=fw;b.at<cv::Vec4f>(y,x)[3]=bw;}
    const double radius=std::clamp(std::min(size.width(),size.height())*.02,4.,40.);cv::GaussianBlur(f,f,{0,0},radius);cv::GaussianBlur(b,b,{0,0},radius);
    auto result=hair.copy();QImage foregroundColor(size,QImage::Format_RGBA64);foregroundColor.fill(Qt::transparent);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){if(fg.at<uchar>(y,x) || bg.at<uchar>(y,x))continue;const auto ff=f.at<cv::Vec4f>(y,x),bb=b.at<cv::Vec4f>(y,x);if(ff[3]<.025F || bb[3]<.025F)continue;const auto* p=reinterpret_cast<const float*>(source.constScanLine(y))+x*4;if(p[3]<.95F)continue;
        double energy=0,projection=0;cv::Vec3d foreground,background,pixel;
        for(int c=0;c<3;++c){foreground[c]=ff[c]/ff[3];background[c]=bb[c]/bb[3];pixel[c]=p[c]/p[3];const double d=foreground[c]-background[c];energy+=d*d;projection+=(pixel[c]-background[c])*d;}
        if(energy<.003)continue;const double alpha=std::clamp(projection/energy,0.,1.);double error=0;for(int c=0;c<3;++c){const double residual=pixel[c]-(foreground[c]*alpha+background[c]*(1-alpha));error+=residual*residual;}
        const double confidence=std::exp(-error/std::max(.0001,energy*.025));if(confidence<.5)continue;
        if(std::all_of(foreground.val,foreground.val+3,[](double c){return c>=0 && c<=1;}))reinterpret_cast<QRgba64*>(foregroundColor.scanLine(y))[x]=QRgba64::fromRgba64(quint16(foreground[0]*65535),quint16(foreground[1]*65535),quint16(foreground[2]*65535),quint16(confidence*65535));
        const double old=hair.constScanLine(y)[x]/255.;result.scanLine(y)[x]=uchar(std::clamp((old+(alpha-old)*confidence)*255.,0.,255.));
    }
    return {{"alpha",portrait_mask_png(result)},{"foreground",portrait_mask_png(foregroundColor)}};
}
static QJsonObject hair_lighting_analysis(const QImage& source,const QJsonObject& person){
    const auto size=source.size().scaled(1536,1536,Qt::KeepAspectRatio);const auto linear=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    const auto automatic=person["settings"].toObject()["automaticMasks"].toObject();auto hair=QImage::fromData(QByteArray::fromBase64(automatic["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);if(hair.isNull())return {};hair=hair.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    std::vector<double> values;for(int y=0;y<size.height();y+=2)for(int x=0;x<size.width();x+=2){if(hair.constScanLine(y)[x]<180)continue;const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;if(p[3]>.5F)values.push_back(lum(p)/p[3]);}if(values.size()<16)return {};std::sort(values.begin(),values.end());const double low=values[values.size()*7/10],high=values[values.size()*95/100];QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;const double l=p[3]>.001F?lum(p)/p[3]:0,peak=std::max({p[0],p[1],p[2]}),saturation=(peak-std::min({p[0],p[1],p[2]}))/std::max(.001,peak),t=std::clamp((l-low)/std::max(.015,high-low),0.,1.);mask.scanLine(y)[x]=uchar(255*t*t*(3-2*t)*std::clamp(1-saturation,0.,1.));}
    cv::Mat samples(size.height(),size.width(),CV_32FC2),reference;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;const float w=p[3]>.95F?hair.constScanLine(y)[x]/255.F:0; samples.at<cv::Vec2f>(y,x)={float(p[3]>.001F?lum(p)/p[3]:0)*w,w};}
    const double radius=std::clamp(person["face"].toArray()[2].toDouble()*size.width()*.055,3.,32.);cv::GaussianBlur(samples,reference,{0,0},radius);
    QImage oil(size,QImage::Format_Grayscale8);oil.fill(0);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto r=reference.at<cv::Vec2f>(y,x);if(r[1]<.8F || hair.constScanLine(y)[x]<220)continue;const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;const double l=p[3]>.001F?lum(p)/p[3]:0,nearby=r[0]/r[1],excess=std::max(0.,l-nearby-std::max(.018,nearby*.18));const double specular=mask.constScanLine(y)[x]/255.;oil.scanLine(y)[x]=uchar(255*std::min(.3,excess/std::max(.02,l)*.65)*specular);}
    const auto edge=person["settings"].toObject()["hairEdgeVersion"].toInt()>=1?refine_hair_alpha(linear,hair):QJsonObject{};
    return {{"edgeAlpha",edge["alpha"]},{"edgeForeground",edge["foreground"]},{"edgeVersion",edge.isEmpty()?0:2},{"highlight",portrait_mask_png(mask)},{"oil",portrait_mask_png(oil)},{"oilVersion",1},{"strands",hair_flow(linear,hair)},{"flowVersion",2}};
}
static QJsonObject cheek_lighting_analysis(const QImage& source,const QJsonObject& person){
    const auto face=person["face"].toArray();if(face.size()!=4)return {};
    const auto size=source.size().scaled(1536,1536,Qt::KeepAspectRatio);const auto linear=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    const double width=face[2].toDouble()*size.width();if(width<10)return {};
    cv::Mat luminance(size.height(),size.width(),CV_32F),smooth;double total=0,count=0;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;const float l=float(p[3]>.001F?lum(p)/p[3]:0);luminance.at<float>(y,x)=l;if(x>face[0].toDouble()*size.width() && x<(face[0].toDouble()+face[2].toDouble())*size.width() && y>face[1].toDouble()*size.height() && y<(face[1].toDouble()+face[3].toDouble())*size.height()){total+=l;++count;}}
    if(count<4)return {};const double average=total/count;cv::GaussianBlur(luminance,smooth,{0,0},std::max(1.,width*.035));QImage matte(size,QImage::Format_Grayscale8),gloss(size,QImage::Format_Grayscale8);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const double l=luminance.at<float>(y,x),diffuse=smooth.at<float>(y,x);const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;const double peak=std::max({p[0],p[1],p[2]}),saturation=(peak-std::min({p[0],p[1],p[2]}))/std::max(.001,peak);const double excess=std::clamp((l-diffuse-average*.08)/std::max(.02,l),0.,.5)*std::clamp(1-saturation,0.,1.);matte.scanLine(y)[x]=uchar(excess*255);gloss.scanLine(y)[x]=uchar(std::clamp(diffuse/std::max(.01,average)*.65,0.,1.)*255);}
    return {{"matte",portrait_mask_png(matte)},{"gloss",portrait_mask_png(gloss)}};
}
QImage portrait_blemish_evidence(const QImage& source,const QJsonObject& person,const QString& model) {
    if(source.isNull())throw EditError("瑕疵分析图像为空");
    const auto face=person["face"].toArray();
    if(face.size()!=4)throw EditError("瑕疵分析范围无效");
    for(const auto& value:face)if(!value.isDouble() || !std::isfinite(value.toDouble()))throw EditError("瑕疵分析范围无效");
    const double x=face[0].toDouble(),y=face[1].toDouble(),w=face[2].toDouble(),h=face[3].toDouble();
    if(x<0 || y<0 || w<=0 || h<=0 || x+w>1.001 || y+h>1.001)throw EditError("瑕疵分析范围无效");
    const int x0=std::max(0,int((x-w*.2)*source.width())),x1=std::min(source.width(),int((x+w*1.2)*source.width()));
    const int y0=std::max(0,int((y-h*.2)*source.height())),y1=std::min(source.height(),int((y+h*1.2)*source.height()));
    if(x1<=x0 || y1<=y0)throw EditError("瑕疵分析范围过小");
    QFile file(model);
    if(!file.open(QIODevice::ReadOnly) || file.size()>200'000'000)throw EditError("瑕疵检测模型缺失或无效");
    const auto bytes=file.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!="249632bbdfd7d4b282dce3a85cb988fa347b897212b9a251cf8728640b972daf")throw EditError("瑕疵检测模型校验失败");
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"portrait-blemish");Ort::SessionOptions options;options.SetIntraOpNumThreads(4);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    Ort::Session session(env,bytes.constData(),std::size_t(bytes.size()),options);
    const auto image=display_image(source.copy(QRect(x0,y0,x1-x0,y1-y0))).convertToFormat(QImage::Format_RGB888);
    cv::Mat rgb(image.height(),image.width(),CV_8UC3,const_cast<uchar*>(image.constBits()),image.bytesPerLine()),scaled;
    cv::resize(rgb,scaled,{768,768},0,0,cv::INTER_LINEAR);
    std::vector<float> input(3*768*768);
    for(int c=0;c<3;++c)for(int row=0;row<768;++row)for(int col=0;col<768;++col)input[(c*768+row)*768+col]=scaled.at<cv::Vec3b>(row,col)[c]/127.5F-1.F;
    const std::array<int64_t,4> shape{1,3,768,768};const auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());
    const char* inputs[]{"image"};const char* outputs[]{"logits"};auto result=session.Run(Ort::RunOptions{nullptr},inputs,&tensor,1,outputs,1);
    if(result.size()!=1 || !result[0].IsTensor() || result[0].GetTensorTypeAndShapeInfo().GetShape()!=std::vector<int64_t>{1,1,768,768})throw EditError("瑕疵检测输出无效");
    const auto* logits=result[0].GetTensorData<float>();cv::Mat probabilities(768,768,CV_8U);
    for(int i=0;i<768*768;++i){if(!std::isfinite(logits[i]))throw EditError("瑕疵检测输出无效");probabilities.data[i]=uchar(std::lround(255./(1.+std::exp(-double(logits[i])))));}
    cv::Mat resized;cv::resize(probabilities,resized,{x1-x0,y1-y0},0,0,cv::INTER_NEAREST);
    QImage evidence(source.size(),QImage::Format_Grayscale8);evidence.fill(0);
    for(int row=0;row<resized.rows;++row)std::memcpy(evidence.scanLine(y0+row)+x0,resized.ptr(row),std::size_t(resized.cols));
    return evidence;
}
QJsonObject portrait_repair_analysis(const QImage& source,const QJsonObject& person,const QImage& blemish_evidence) {
    const auto size=std::max(source.width(),source.height())>1024?source.size().scaled(1024,1024,Qt::KeepAspectRatio):source.size();const auto image=display_image(source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)).convertToFormat(QImage::Format_RGB888);
    if(!blemish_evidence.isNull() && (blemish_evidence.size()!=source.size() || blemish_evidence.format()!=QImage::Format_Grayscale8))throw EditError("瑕疵检测证据与图像不匹配");
    const auto neural=blemish_evidence.isNull()?QImage{}:blemish_evidence.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    auto points=person["landmarks"].toArray();const bool partial=person["settings"].toObject()["partialSkin"].toBool();if(points.size()!=66){if(!partial)throw EditError("修复定位需要有效的面部关键点");for(int i=0;i<66;++i)points.append(QJsonArray{0,0,0});}
    const bool correctedMouth=person["settings"].toObject()["mouthTopologyVersion"].toInt(1)>=2;
    const auto mouth=correctedMouth?polygon(points,{58,59,60,61,62,63,64,65},size):polygon(points,{60,61,62,63,64,65},size);QImage teeth(size,QImage::Format_Grayscale8);teeth.fill(0);
    cv::Mat rgb(image.height(),image.width(),CV_8UC3,const_cast<uchar*>(image.constBits()),image.bytesPerLine()),lab;cv::cvtColor(rgb,lab,cv::COLOR_RGB2Lab);
    std::vector<cv::Vec3f> samples;std::vector<QPoint> positions;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)if(mouth.constScanLine(y)[x]>220){const auto p=lab.at<cv::Vec3b>(y,x);samples.emplace_back(float(p[0]),float(p[1]),float(p[2]));positions.emplace_back(x,y);}
    QString reason=QStringLiteral("未可靠识别露出的牙齿；可在范围页手工补选");
    if(samples.size()>=24){
        // Deterministic local Lab clustering. Relative mouth color, not a fixed HSV white threshold.
        auto sorted=samples;std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a[0]<b[0];});
        std::array<cv::Vec3f,3> centers{sorted[sorted.size()/6],sorted[sorted.size()/2],sorted[sorted.size()*5/6]};std::vector<int> labels(samples.size());
        for(int iteration=0;iteration<16;++iteration){std::array<cv::Vec3f,3> sums{};std::array<int,3> counts{};for(std::size_t i=0;i<samples.size();++i){double best=1e20;int chosen{};for(int k=0;k<3;++k){const auto d=samples[i]-centers[k];const double error=d.dot(d);if(error<best){best=error;chosen=k;}}labels[i]=chosen;sums[chosen]+=samples[i];++counts[chosen];}for(int k=0;k<3;++k)if(counts[k])centers[k]=sums[k]/float(counts[k]);}
        const int bright=int(std::max_element(centers.begin(),centers.end(),[](auto a,auto b){return a[0]<b[0];})-centers.begin());
        const auto c=centers[bright];float dark=255,red=0;for(const auto& v:centers){dark=std::min(dark,v[0]);red=std::max(red,v[1]);}
        const double width=QLineF(landmark(points,correctedMouth?58:60,size),landmark(points,correctedMouth?62:63,size)).length();
        const double opening=QLineF(landmark(points,correctedMouth?60:61,size),landmark(points,correctedMouth?64:65,size)).length();
        if(width>4 && opening/width>.045 && c[0]-dark>22 && (red-c[1]>4 || c[1]<133)){
            cv::Mat binary(size.height(),size.width(),CV_8U,cv::Scalar(0));for(std::size_t i=0;i<positions.size();++i)if(labels[i]==bright)binary.at<uchar>(positions[i].y(),positions[i].x())=255;
            cv::Mat components,stats,centroids;const int n=cv::connectedComponentsWithStats(binary,components,stats,centroids);int retained{};
            for(int k=1;k<n;++k){const int area=stats.at<int>(k,cv::CC_STAT_AREA);if(area<std::max(3,int(samples.size()/150)) || area>int(samples.size()*.9))continue;
                for(int y=stats.at<int>(k,cv::CC_STAT_TOP);y<stats.at<int>(k,cv::CC_STAT_TOP)+stats.at<int>(k,cv::CC_STAT_HEIGHT);++y)for(int x=stats.at<int>(k,cv::CC_STAT_LEFT);x<stats.at<int>(k,cv::CC_STAT_LEFT)+stats.at<int>(k,cv::CC_STAT_WIDTH);++x)if(components.at<int>(y,x)==k){teeth.scanLine(y)[x]=255;++retained;}}
            if(retained>5)reason=QStringLiteral("已生成牙齿建议范围；请检查牙龈、舌头和唇缘后调整");
        }
    }
    // Suggestions are visual outliers, never diagnoses or automatic removals.
    auto skin=portrait_effect_mask(person,"skin",size);const auto preserved=portrait_effect_mask(person,"repair.preserve",size);
    const double fw=person["face"].toArray()[2].toDouble()*size.width();cv::Mat floating;lab.convertTo(floating,CV_32FC3);
    cv::Mat fineBase;cv::GaussianBlur(floating,fineBase,{0,0},1.2);
    cv::Mat exclusion(size.height(),size.width(),CV_8U,cv::Scalar(0));
    if(!partial){for(const auto region:{polygon(points,{36,37,38,39,40,41},size),polygon(points,{42,43,44,45,46,47},size),polygon(points,{48,49,50,51,52,53,54,55,56,57},size)}){
        cv::Mat raw(region.height(),region.width(),CV_8U,const_cast<uchar*>(region.constBits()),region.bytesPerLine());cv::max(exclusion,raw,exclusion);}
        for(int start:{17,22})for(int i=start;i<start+4;++i){const auto a=landmark(points,i,size),b=landmark(points,i+1,size);cv::line(exclusion,{int(a.x()),int(a.y())},{int(b.x()),int(b.y())},255,std::max(3,int(fw*.035)),cv::LINE_AA);}
        const int margin=std::max(3,int(fw*.025)|1);cv::dilate(exclusion,exclusion,cv::getStructuringElement(cv::MORPH_ELLIPSE,{margin,margin}));
    }
    const auto originalSkin=skin;
    if(!neural.isNull()){
        // Inflamed spots can form small holes in a skin-color mask. Only repair
        // enclosed, compact holes where the independent detector sees a core;
        // do not grow the outer skin boundary or override protected features.
        cv::Mat holes(size.height(),size.width(),CV_8U);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)holes.at<uchar>(y,x)=skin.constScanLine(y)[x]<=230?255:0;
        cv::Mat labels,stats,centers;const int count=cv::connectedComponentsWithStats(holes,labels,stats,centers);
        for(int k=1;k<count;++k){
            const int x0=stats.at<int>(k,cv::CC_STAT_LEFT),y0=stats.at<int>(k,cv::CC_STAT_TOP),w=stats.at<int>(k,cv::CC_STAT_WIDTH),h=stats.at<int>(k,cv::CC_STAT_HEIGHT),area=stats.at<int>(k,cv::CC_STAT_AREA);
            if(x0==0 || y0==0 || x0+w==size.width() || y0+h==size.height() || area<4 || area>fw*fw*.001 || std::max(w,h)>std::min(w,h)*2.8)continue;
            int supported=0;
            for(int y=y0;y<y0+h;++y)for(int x=x0;x<x0+w;++x)if(labels.at<int>(y,x)==k && neural.constScanLine(y)[x]>=90 && preserved.constScanLine(y)[x]<16 && exclusion.at<uchar>(y,x)<=16)++supported;
            if(supported<std::max(4,int(std::ceil(area*.25))))continue;
            for(int y=y0;y<y0+h;++y)for(int x=x0;x<x0+w;++x)if(labels.at<int>(y,x)==k && neural.constScanLine(y)[x]>=90 && preserved.constScanLine(y)[x]<16 && exclusion.at<uchar>(y,x)<=16)skin.scanLine(y)[x]=255;
        }
    }
    cv::Mat strength(size.height(),size.width(),CV_32F,cv::Scalar(0)),types(size.height(),size.width(),CV_8U,cv::Scalar(0));
    for(double scale:{.012,.025,.045}){cv::Mat low;cv::GaussianBlur(floating,low,{0,0},std::max(1.5,fw*scale));
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){
            if(preserved.constScanLine(y)[x]>=16 || skin.constScanLine(y)[x]<=230 || exclusion.at<uchar>(y,x)>16)continue;
            const auto a=floating.at<cv::Vec3f>(y,x),b=low.at<cv::Vec3f>(y,x);const double red=a[1]-b[1],dark=b[0]-a[0],yellow=a[2]-b[2];
            // Very dark, tiny, neutral or elongated marks are intentionally conservative.
            if(a[0]<b[0]*.62 || a[0]>b[0]+15)continue;
            double score=0;uchar type=0;
            if(red>3.5){score=red/4.;type=dark>6?1:2;}
            else if(dark>7 && dark<42 && yellow>2 && red>-.5){score=std::min(dark/9.,(yellow+std::max(0.,red))/3.);type=3;}
            if(score>strength.at<float>(y,x)){strength.at<float>(y,x)=float(score);types.at<uchar>(y,x)=type;}
        }
    }
    cv::Mat candidates;cv::compare(strength,1.,candidates,cv::CMP_GT);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)if(originalSkin.constScanLine(y)[x]<=230)candidates.at<uchar>(y,x)=0;
    cv::morphologyEx(candidates,candidates,cv::MORPH_OPEN,cv::getStructuringElement(cv::MORPH_ELLIPSE,{3,3}));
    QJsonArray suggestions;std::vector<QJsonObject> ranked;
    const auto detectionSkin=skin;
    for(int pass=0;pass<(neural.isNull()?1:2);++pass){
    skin=pass==0?originalSkin:detectionSkin;
    cv::Mat selected=candidates;
    if(pass==1){
        selected=cv::Mat(size.height(),size.width(),CV_8U,cv::Scalar(0));
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)
            if(neural.constScanLine(y)[x]>=90 && skin.constScanLine(y)[x]>230 && preserved.constScanLine(y)[x]<16 && exclusion.at<uchar>(y,x)<=16)selected.at<uchar>(y,x)=255;
        cv::morphologyEx(selected,selected,cv::MORPH_OPEN,cv::getStructuringElement(cv::MORPH_ELLIPSE,{3,3}));
    }
    cv::Mat components,stats,centroids;const int n=cv::connectedComponentsWithStats(selected,components,stats,centroids);
    for(int k=1;k<n;++k){const int area=stats.at<int>(k,cv::CC_STAT_AREA),width=stats.at<int>(k,cv::CC_STAT_WIDTH),height=stats.at<int>(k,cv::CC_STAT_HEIGHT);
        if(area<std::max(5.,fw*fw*.00008) || area>fw*fw*.009 || std::max(width,height)>std::min(width,height)*2.8)continue;
        std::array<int,4> votes{};double total=0;for(int y=stats.at<int>(k,cv::CC_STAT_TOP);y<stats.at<int>(k,cv::CC_STAT_TOP)+height;++y)for(int x=stats.at<int>(k,cv::CC_STAT_LEFT);x<stats.at<int>(k,cv::CC_STAT_LEFT)+width;++x)if(components.at<int>(y,x)==k){++votes[types.at<uchar>(y,x)];total+=strength.at<float>(y,x);}
        if(pass==1 && (votes[0]>area*.5 || ranked.size()>=40))continue;
        int type=int(std::max_element(votes.begin()+1,votes.end())-votes.begin());const double evidence=total/area,compactness=double(area)/(width*height);
        const double confidence=std::clamp((evidence-1.)/2.,0.,1.)*.6+std::clamp(compactness,0.,1.)*.4;
        const double radius=std::clamp(std::max(width,height)*.6,2.,std::max(2.,fw*.055));
        const double cx=centroids.at<double>(k,0),cy=centroids.at<double>(k,1);
        if(pass==1 && std::any_of(ranked.begin(),ranked.end(),[&](const auto& previous){return std::hypot(cx-previous["x"].toDouble()*size.width(),cy-previous["y"].toDouble()*size.height())<std::max(radius,previous["radius"].toDouble()*std::min(size.width(),size.height()));}))continue;
        bool blocked=false;
        for(const auto& value:person["settings"].toObject()["blemishes"].toArray()){
            const auto mark=value.toObject();const auto state=mark["state"].toString();if(state!="preserved" && state!="repaired")continue;
            if(std::hypot(cx-mark["x"].toDouble()*size.width(),cy-mark["y"].toDouble()*size.height())<=radius+mark["radius"].toDouble()*std::min(size.width(),size.height())){blocked=true;break;}
        }
        for(int y=std::max(0,int(cy-radius));!blocked && y<std::min(size.height(),int(cy+radius+1));++y)for(int x=std::max(0,int(cx-radius));x<std::min(size.width(),int(cx+radius+1));++x)
            if(std::hypot(x-cx,y-cy)<=radius && preserved.constScanLine(y)[x]>=16){blocked=true;break;}
        if(blocked)continue;
        int neuralSupport=0;
        if(!neural.isNull())for(int y=stats.at<int>(k,cv::CC_STAT_TOP);y<stats.at<int>(k,cv::CC_STAT_TOP)+height;++y)for(int x=stats.at<int>(k,cv::CC_STAT_LEFT);x<stats.at<int>(k,cv::CC_STAT_LEFT)+width;++x)
            if(components.at<int>(y,x)==k && neural.constScanLine(y)[x]>=90)++neuralSupport;
        const bool modelSupported=neuralSupport>=std::max(3,int(std::ceil(area*.002)));
        const auto center=floating.at<cv::Vec3f>(std::clamp(int(cy),0,size.height()-1),std::clamp(int(cx),0,size.width()-1));int validRing=0,support=0;std::vector<float> ringLights;std::vector<cv::Vec3d> ringBasis;
        for(int i=0;i<24;++i){const double theta=i*6.283185307179586/24.;const int x=int(cx+std::cos(theta)*radius*1.6),y=int(cy+std::sin(theta)*radius*1.6);
            if(x<0 || y<0 || x>=size.width() || y>=size.height() || skin.constScanLine(y)[x]<230 || preserved.constScanLine(y)[x]>=16 || exclusion.at<uchar>(y,x)>16)continue;
            ++validRing;const auto ring=floating.at<cv::Vec3f>(y,x);ringLights.push_back(ring[0]);ringBasis.push_back({1.,std::cos(theta),std::sin(theta)});if(type==3?(ring[0]-center[0]>5 && center[2]-ring[2]>1):(center[1]-ring[1]>2.5))++support;
        }
        // A blob must be surrounded by comparable skin, unlike a facial crease or mask boundary.
        if(validRing<23 || support<19)continue;
        auto sortedLights=ringLights;std::sort(sortedLights.begin(),sortedLights.end());
        // Strong illumination discontinuities indicate nostrils, folds, ears, or jewelry.
        // Use quantiles so one hair or highlight cannot reject an otherwise sound patch.
        const auto discontinuous=[&](const auto& values){return values[values.size()*9/10]-values[values.size()/10]>30 || values[values.size()/5]<center[0]-15;};
        const bool needsLightingCorrection=discontinuous(sortedLights);
        if(needsLightingCorrection){
            if(!modelSupported)continue;
            // Neural evidence can allow removal of a smooth lighting ramp,
            // but cannot bypass semantic, decision, shape or ring-support gates.
            cv::Mat design(int(ringLights.size()),3,CV_64F),values(int(ringLights.size()),1,CV_64F),lighting;
            for(int i=0;i<int(ringLights.size());++i){for(int c=0;c<3;++c)design.at<double>(i,c)=ringBasis[i][c];values.at<double>(i)=ringLights[i];}
            if(!cv::solve(design,values,lighting,cv::DECOMP_SVD))continue;
            for(int i=0;i<int(ringLights.size());++i)ringLights[i]-=float(lighting.at<double>(1)*ringBasis[i][1]+lighting.at<double>(2)*ringBasis[i][2]);
            std::sort(ringLights.begin(),ringLights.end());if(discontinuous(ringLights))continue;
        }
        // Darkness alone is not texture. Compare fine luminance variation in
        // the component interior with nearby skin, excluding the color edge.
        double interiorDetail=0,contextDetail=0;int interiorCount=0,contextCount=0;
        if(type==1){
            for(int y=std::max(1,stats.at<int>(k,cv::CC_STAT_TOP));y<std::min(size.height()-1,stats.at<int>(k,cv::CC_STAT_TOP)+height);++y)
                for(int x=std::max(1,stats.at<int>(k,cv::CC_STAT_LEFT));x<std::min(size.width()-1,stats.at<int>(k,cv::CC_STAT_LEFT)+width);++x)
                    if(components.at<int>(y,x)==k && components.at<int>(y-1,x)==k && components.at<int>(y+1,x)==k && components.at<int>(y,x-1)==k && components.at<int>(y,x+1)==k){interiorDetail+=std::abs(floating.at<cv::Vec3f>(y,x)[0]-fineBase.at<cv::Vec3f>(y,x)[0]);++interiorCount;}
            for(int i=0;i<24;++i){const double theta=i*6.283185307179586/24.;const int x=int(cx+std::cos(theta)*radius*1.6),y=int(cy+std::sin(theta)*radius*1.6);
                if(x>=0 && y>=0 && x<size.width() && y<size.height() && skin.constScanLine(y)[x]>230 && exclusion.at<uchar>(y,x)<=16){contextDetail+=std::abs(floating.at<cv::Vec3f>(y,x)[0]-fineBase.at<cv::Vec3f>(y,x)[0]);++contextCount;}}
            interiorDetail/=std::max(1,interiorCount);contextDetail/=std::max(1,contextCount);
            if(interiorCount<8 || interiorDetail<2. || interiorDetail<contextDetail*1.5)type=2;
        }
        QJsonObject candidate{{"x",centroids.at<double>(k,0)/size.width()},{"y",centroids.at<double>(k,1)/size.height()},{"radius",radius/std::min(size.width(),size.height())},{"score",evidence*std::sqrt(area)},{"confidence",confidence},{"category",type==1?"red-texture":type==2?"red-mark":"pigment"},{"analysisVersion",needsLightingCorrection?3:2},{"state","pending"},{"classificationVersion",2},{"interiorDetail",interiorDetail},{"contextDetail",contextDetail}};
        if(needsLightingCorrection){candidate["evidenceSource"]="detection-net-and-color";candidate["evidencePixels"]=neuralSupport;}
        // Keep the detected footprint separate from the circular brush locator.
        // Confirming a suggestion must not heal the whole bounding circle.
        cv::Mat footprint;cv::compare(components,k,footprint,cv::CMP_EQ);
        cv::dilate(footprint,footprint,cv::getStructuringElement(cv::MORPH_ELLIPSE,{3,3}));
        cv::GaussianBlur(footprint,footprint,{3,3},.6);
        QImage coverage(size,QImage::Format_Grayscale8);coverage.fill(0);
        for(int y=std::max(0,int(cy-radius));y<std::min(size.height(),int(cy+radius+1));++y)for(int x=std::max(0,int(cx-radius));x<std::min(size.width(),int(cx+radius+1));++x)
            if(std::hypot(x-cx,y-cy)<=radius && skin.constScanLine(y)[x]>230 && preserved.constScanLine(y)[x]<16 && exclusion.at<uchar>(y,x)<=16)coverage.scanLine(y)[x]=footprint.at<uchar>(y,x);
        candidate["coverage"]=portrait_mask_png(coverage);
        int supplemented=0;
        if(pass==1)for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)if(coverage.constScanLine(y)[x]>0 && originalSkin.constScanLine(y)[x]<=230)++supplemented;
        candidate["supplementedSkinPixels"]=supplemented;
        if(pass==1){candidate["analysisVersion"]=4;candidate["evidenceSource"]="independent-detection-and-color";candidate["evidencePixels"]=neuralSupport;}
        ranked.push_back(candidate);
    }
    }
    skin=originalSkin;
    std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a["score"].toDouble()>b["score"].toDouble();});for(std::size_t i=0;i<std::min<std::size_t>(40,ranked.size());++i)suggestions.append(ranked[i]);
    QImage whites(size,QImage::Format_Grayscale8),red_eye(size,QImage::Format_Grayscale8),under(size,QImage::Format_Grayscale8),bags(size,QImage::Format_Grayscale8);whites.fill(0);red_eye.fill(0);under.fill(0);bags.fill(0);
    int whitePixels=0,redPixels=0;
    for(int side=0;side<(partial?0:2);++side){const auto eye=side==0?polygon(points,{36,37,38,39,40,41},size):polygon(points,{42,43,44,45,46,47},size);const int start=side==0?36:42;
        const auto a=landmark(points,start,size),b=landmark(points,start+3,size);const auto center=(a+b)*.5;const double ew=QLineF(a,b).length();if(ew<3)continue;
        const auto axis=(b-a)/ew;QPointF down(-axis.y(),axis.x());if(down.y()<0)down=-down;
        const auto eyeBox=QRectF(center-QPointF(ew,ew),QSizeF(ew*2,ew*2)).toAlignedRect().intersected(image.rect());
        std::vector<int> eyeLights;for(int y=eyeBox.top();y<=eyeBox.bottom();++y)for(int x=eyeBox.left();x<=eyeBox.right();++x)if(eye.constScanLine(y)[x]>240)eyeLights.push_back(lab.at<cv::Vec3b>(y,x)[0]);
        QPointF irisCenter=center;double irisRadius=ew*.17;bool reliableIris=false;double scleraThreshold=100;
        if(eyeLights.size()>18){
            std::sort(eyeLights.begin(),eyeLights.end());const int darkLevel=eyeLights[eyeLights.size()/5],brightLevel=eyeLights[eyeLights.size()*85/100];
            scleraThreshold=darkLevel+(brightLevel-darkLevel)*.4;
            cv::Mat eyePixels(size.height(),size.width(),CV_8U,const_cast<uchar*>(eye.constBits()),eye.bytesPerLine()),interior;
            cv::erode(eyePixels,interior,cv::getStructuringElement(cv::MORPH_ELLIPSE,{3,3}));
            cv::Mat irisCandidates(eyeBox.height(),eyeBox.width(),CV_8U,cv::Scalar(0));
            for(int y=eyeBox.top();y<=eyeBox.bottom();++y)for(int x=eyeBox.left();x<=eyeBox.right();++x)if(interior.at<uchar>(y,x)>240 && lab.at<cv::Vec3b>(y,x)[0]<=eyeLights[eyeLights.size()*35/100])irisCandidates.at<uchar>(y-eyeBox.y(),x-eyeBox.x())=255;
            cv::Mat irisLabels,irisStats,irisCentroids;const int irisCount=cv::connectedComponentsWithStats(irisCandidates,irisLabels,irisStats,irisCentroids);double bestScore=0;
            for(int label=1;label<irisCount;++label){const int area=irisStats.at<int>(label,cv::CC_STAT_AREA);if(area<2 || area>int(eyeLights.size()*.6))continue;
                const QPointF candidate(irisCentroids.at<double>(label,0)+eyeBox.x(),irisCentroids.at<double>(label,1)+eyeBox.y());
                const double score=area/(1+std::pow(QLineF(candidate,center).length()/ew,2)*4);
                if(score>bestScore){bestScore=score;irisCenter=candidate;irisRadius=std::max(ew*.17,std::max(irisStats.at<int>(label,cv::CC_STAT_WIDTH),irisStats.at<int>(label,cv::CC_STAT_HEIGHT))*.6);}
            }
            reliableIris=bestScore>0 && brightLevel-darkLevel>20 && irisRadius<ew*.32;
        }
        for(int y=std::max(0,int(center.y()-ew));y<std::min(size.height(),int(center.y()+ew));++y)for(int x=std::max(0,int(center.x()-ew));x<std::min(size.width(),int(center.x()+ew));++x){
            const auto pixel=lab.at<cv::Vec3b>(y,x);const auto* c=image.constScanLine(y)+x*3;
            if(eye.constScanLine(y)[x]>220){const double neutral=std::clamp(1-std::hypot(pixel[1]-128.,pixel[2]-128.)/35.,0.,1.);
                if(reliableIris && QLineF(QPointF(x,y),irisCenter).length()>irisRadius && std::abs(QPointF::dotProduct(QPointF(x,y)-center,axis))<ew*.44){whites.scanLine(y)[x]=uchar(neutral*std::clamp((pixel[0]-scleraThreshold)/35.,0.,1.)*255);if(whites.constScanLine(y)[x]>80)++whitePixels;}
                const double excess=std::max(0.,c[0]-std::max(c[1],c[2])*1.4);red_eye.scanLine(y)[x]=uchar(std::min(1.,excess/80.)*255);}
            const auto offset=QPointF(x,y)-center;const double along=QPointF::dotProduct(offset,axis),below=QPointF::dotProduct(offset,down);
            const double dx=along/(ew*.65),dy=(below-ew*.36)/(ew*.24),db=(below-ew*.52)/(ew*.22);
            const double skin_weight=skin.constScanLine(y)[x]/255.;under.scanLine(y)[x]=uchar(std::max(double(under.constScanLine(y)[x]),std::max(0.,1-dx*dx-dy*dy)*skin_weight*255));bags.scanLine(y)[x]=uchar(std::max(double(bags.constScanLine(y)[x]),std::max(0.,1-dx*dx-db*db)*skin_weight*255));
        }
        cv::Mat redCandidates(eyeBox.height(),eyeBox.width(),CV_8U,cv::Scalar(0)),acceptedRed(eyeBox.height(),eyeBox.width(),CV_8U,cv::Scalar(0));
        for(int y=eyeBox.top();y<=eyeBox.bottom();++y)for(int x=eyeBox.left();x<=eyeBox.right();++x){const auto* color=image.constScanLine(y)+x*3;
            if(eye.constScanLine(y)[x]>220 && color[0]>70 && color[0]>std::max(color[1],color[2])*1.8 && color[0]-std::max(color[1],color[2])>40)redCandidates.at<uchar>(y-eyeBox.y(),x-eyeBox.x())=255;
        }
        cv::Mat redLabels,redStats,redCentroids;const int redCount=cv::connectedComponentsWithStats(redCandidates,redLabels,redStats,redCentroids);
        for(int label=1;label<redCount;++label){const int area=redStats.at<int>(label,cv::CC_STAT_AREA),rw=redStats.at<int>(label,cv::CC_STAT_WIDTH),rh=redStats.at<int>(label,cv::CC_STAT_HEIGHT);
            if(area<3 || std::max(rw,rh)>ew*.55 || std::max(rw,rh)>std::min(rw,rh)*2 || area<rw*rh*.35)continue;
            const QPointF redCenter(redCentroids.at<double>(label,0)+eyeBox.x(),redCentroids.at<double>(label,1)+eyeBox.y());
            const auto* central=image.constScanLine(std::clamp(int(std::round(redCenter.y())),0,size.height()-1))+std::clamp(int(std::round(redCenter.x())),0,size.width()-1)*3;
            // A brown iris ring around a dark pupil is not a flash-red pupil.
            if(!(central[0]>std::max(central[1],central[2])*1.6 || std::min({central[0],central[1],central[2]})>180))continue;
            int peak=0;for(int y=0;y<redLabels.rows;++y)for(int x=0;x<redLabels.cols;++x)if(redLabels.at<int>(y,x)==label)peak=std::max(peak,int(image.constScanLine(y+eyeBox.y())[(x+eyeBox.x())*3]));
            if(peak<160)continue;
            for(int y=0;y<redLabels.rows;++y)for(int x=0;x<redLabels.cols;++x)if(redLabels.at<int>(y,x)==label)acceptedRed.at<uchar>(y,x)=255;
        }
        // Expand confirmed pupils to cover antialiased red fringes, preserving neutral glints.
        cv::dilate(acceptedRed,acceptedRed,cv::getStructuringElement(cv::MORPH_ELLIPSE,{5,5}));
        for(int y=eyeBox.top();y<=eyeBox.bottom();++y)for(int x=eyeBox.left();x<=eyeBox.right();++x){
            const auto* color=image.constScanLine(y)+x*3;const double excess=color[0]-std::max(color[1],color[2]);
            const bool confirmed=acceptedRed.at<uchar>(y-eyeBox.y(),x-eyeBox.x()) && eye.constScanLine(y)[x]>220;
            red_eye.scanLine(y)[x]=confirmed?uchar(std::clamp((excess-8.)/20.,0.,1.)*255):0;
            if(red_eye.constScanLine(y)[x]>80)++redPixels;
        }
    }
    return {{"hairLighting",hair_lighting_analysis(source,person)},{"cheekLighting",cheek_lighting_analysis(source,person)},{"lipLighting",lip_lighting_analysis(source,person)},{"browMaterials",brow_material_analysis(image,person)},{"irisMaterials",iris_material_analysis(display_image(source.scaled(source.size().scaled(2048,2048,Qt::KeepAspectRatio),Qt::IgnoreAspectRatio,Qt::SmoothTransformation)).convertToFormat(QImage::Format_RGB888),person)},{"teeth",portrait_mask_png(teeth)},{"teethStatus",reason},{"redEyeStatus",redPixels>0?QStringLiteral("已发现闪光红眼候选"):QStringLiteral("未发现可靠的闪光红眼候选")},{"eyeStatus",whitePixels>5?QStringLiteral("已生成眼白建议范围，已排除估计的虹膜与瞳孔；请检查遮挡和反光"):QStringLiteral("眼白范围不够可靠，未自动提亮；可手动补选眼白")},{"blemishes",suggestions},{"eye.white",portrait_mask_png(whites)},{"eye.red",portrait_mask_png(red_eye)},{"eye.circles",portrait_mask_png(under)},{"eye.bags",portrait_mask_png(bags)},{"eyeglassFrame",partial?QString{}:portrait_mask_png(portrait_frame_protection(source,person))}};
}
QImage portrait_repair_region(const QImage& source,const QJsonObject& recipe,QRect region,double amount,const std::atomic_bool* cancelled,const QImage& effect_mask) {
    const auto people=recipe["people"].toArray();bool active=false;for(const auto& entry:people){const auto s=entry.toObject()["settings"].toObject();active|=!s["repairs"].toArray().isEmpty();const auto p=s["parameters"].toObject();for(auto i=p.begin();i!=p.end();++i)if((i.key().startsWith("eye.") || i.key().startsWith("teeth.") || i.key()=="repair.conceal") && i.value().toDouble()!=0)active=true;}
    if(!active)return region==source.rect()?source:source.copy(region);
    auto result=copy_region(source,region);
    std::vector<QImage> bodies;for(const auto& v:people)bodies.push_back(QImage::fromData(QByteArray::fromBase64(v.toObject()["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8));
    for(int index=0;index<people.size();++index){const auto person=people[index].toObject(),settings=person["settings"].toObject();if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool())continue;
        const auto parameters=settings["parameters"].toObject(),enabled=settings["enabled"].toObject();const double strength=amount*std::clamp(settings["strength"].toDouble(100),0.,100.)*.01;
        auto protection=[&](int x,int y){double w=effect_mask.isNull()?1:at(effect_mask,x,y,source.size());for(int other=0;other<people.size();++other)if(other!=index)w*=1-at(bodies[std::size_t(other)],x,y,source.size());return w;};
        auto preserved=portrait_effect_mask(person,"repair.preserve",source.size().scaled(768,768,Qt::KeepAspectRatio));
        for(const auto& suggestion:settings["blemishes"].toArray()){const auto mark=suggestion.toObject();if(mark["state"].toString()!="preserved")continue;
            const double cx=mark["x"].toDouble()*preserved.width(),cy=mark["y"].toDouble()*preserved.height(),r=mark["radius"].toDouble()*std::min(preserved.width(),preserved.height());
            for(int y=std::max(0,int(cy-r-2));y<std::min(preserved.height(),int(cy+r+3));++y)for(int x=std::max(0,int(cx-r-2));x<std::min(preserved.width(),int(cx+r+3));++x){const double weight=std::clamp(r+1-std::hypot(x-cx,y-cy),0.,1.);preserved.scanLine(y)[x]=std::max(preserved.constScanLine(y)[x],uchar(weight*255));}
        }
        for(const auto& entry:settings["repairs"].toArray()){
            if(cancelled && cancelled->load())throw EditError("人像修复已取消");const auto object=entry.toObject();auto healing=object;healing.remove("kind");healing.remove("id");healing.remove("wrinkleMethod");healing.remove("makeupPart");healing.remove("validatedCandidateCoverage");healing.remove("hairSourceAngle");healing.remove("hairSourceScale");auto stroke=healing_from_json(healing);if(!stroke.enabled || stroke.points.empty())continue;
            const QString kind=object["kind"].toString("heal");if(!enabled[kind=="wrinkle"?"wrinkle":kind=="hairline"?"hair":"repair"].toBool(true))continue;
            const double radius=stroke.radius*std::min(source.width(),source.height());QRectF bounds;for(const auto p:stroke.points){const QPointF q(p.x()*source.width(),p.y()*source.height());const QRectF b(q.x()-radius-3,q.y()-radius-3,radius*2+6,radius*2+6);bounds=bounds.isNull()?b:bounds.united(b);}
            const auto destination=bounds.toAlignedRect().intersected(source.rect());const auto visible=destination.intersected(region);if(visible.isEmpty())continue;
            const auto coverage_text=settings["masks"].toObject()["repair.item."+object["id"].toString()].toString();const auto edit_coverage=QImage::fromData(QByteArray::fromBase64(coverage_text.toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
            if(kind=="hairline" && object.contains("hairSourceAngle")){
                const double angle=object["hairSourceAngle"].toDouble()*3.141592653589793/180,scale=std::clamp(object["hairSourceScale"].toDouble(1),.25,2.);
                const auto first=stroke.points.front();const QPointF anchor(first.x()*source.width(),first.y()*source.height()),origin((first.x()+stroke.offset.x())*source.width(),(first.y()+stroke.offset.y())*source.height());
                for(int y=visible.top();y<=visible.bottom();++y){if(cancelled && cancelled->load())throw EditError("补发已取消");auto* out=reinterpret_cast<float*>(result.scanLine(y-region.y()));for(int x=visible.left();x<=visible.right();++x){const QPointF pixel(x+.5,y+.5);double distance=radius;
                    for(std::size_t n=0;n<stroke.points.size();++n){const auto ap=stroke.points[n?n-1:0],bp=stroke.points[n];const QPointF a(ap.x()*source.width(),ap.y()*source.height()),b(bp.x()*source.width(),bp.y()*source.height()),delta=b-a;const double length=QPointF::dotProduct(delta,delta),t=length>0?std::clamp(QPointF::dotProduct(pixel-a,delta)/length,0.,1.):0.;distance=std::min(distance,QLineF(pixel,a+delta*t).length());}
                    if(distance>=radius)continue;const auto relative=(pixel-anchor)/scale;const QPointF sample=origin+QPointF(relative.x()*std::cos(angle)+relative.y()*std::sin(angle),-relative.x()*std::sin(angle)+relative.y()*std::cos(angle));if(sample.x()<0 || sample.y()<0 || sample.x()>=source.width()-1 || sample.y()>=source.height()-1)continue;
                    const int ix=int(sample.x()),iy=int(sample.y());const double fx=sample.x()-ix,fy=sample.y()-iy;const auto* a=reinterpret_cast<const float*>(source.constScanLine(iy));const auto* b=reinterpret_cast<const float*>(source.constScanLine(iy+1));const double t=std::clamp((1-distance/radius)/std::max(.001,stroke.feather),0.,1.),w=t*t*(3-2*t)*stroke.opacity*strength*protection(x,y)*(edit_coverage.isNull()?1:at(edit_coverage,x,y,source.size()));auto* dst=out+(x-region.x())*4;
                    for(int c=0;c<3;++c){const double color=(a[ix*4+c]*(1-fx)+a[(ix+1)*4+c]*fx)*(1-fy)+(b[ix*4+c]*(1-fx)+b[(ix+1)*4+c]*fx)*fy;dst[c]=float(dst[c]*(1-w)+color*w);}
                }}continue;
            }
            if(kind=="wrinkle" && object["wrinkleMethod"].toString()=="lighting-v2"){
                const auto skin=portrait_effect_mask(person,"skin",source.size().scaled(768,768,Qt::KeepAspectRatio));
                const double sigma=std::max(.6,radius*.12);const int halo=int(std::ceil(radius*1.5+sigma*4))+3;
                const auto expanded=destination.adjusted(-halo,-halo,halo,halo).intersected(source.rect());
                cv::Mat input(expanded.height(),expanded.width(),CV_32FC4,const_cast<uchar*>(source.constScanLine(expanded.y()))+qint64(expanded.x())*16,source.bytesPerLine()),fine;
                cv::GaussianBlur(input,fine,{0,0},sigma,sigma,cv::BORDER_REFLECT_101);
                const auto lightAt=[&](QPointF point){
                    const double px=std::clamp(point.x()-expanded.x(),0.,double(fine.cols-1)),py=std::clamp(point.y()-expanded.y(),0.,double(fine.rows-1));
                    const int ix=int(px),iy=int(py);double total=0;
                    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto c=fine.at<cv::Vec4f>(std::min(iy+dy,fine.rows-1),std::min(ix+dx,fine.cols-1));total+=(dx?px-ix:1-px+ix)*(dy?py-iy:1-py+iy)*(.2627*c[0]+.678*c[1]+.0593*c[2])/std::max(.001F,c[3]);}return total;
                };
                std::vector<QPointF> path;for(const auto point:stroke.points)path.emplace_back(point.x()*source.width(),point.y()*source.height());
                for(int y=visible.top();y<=visible.bottom();++y){if(cancelled && cancelled->load())throw EditError("人像修复已取消");auto* out=reinterpret_cast<float*>(result.scanLine(y-region.y()));const auto* original=reinterpret_cast<const float*>(source.constScanLine(y));
                    for(int x=visible.left();x<=visible.right();++x){const QPointF pixel(x,y);QPointF closest=path.front(),normal(0,1);double distance=QLineF(pixel,closest).length();
                        for(std::size_t segment=1;segment<path.size();++segment){const auto axis=path[segment]-path[segment-1];const double length2=QPointF::dotProduct(axis,axis);if(length2<1e-8)continue;const double t=std::clamp(QPointF::dotProduct(pixel-path[segment-1],axis)/length2,0.,1.);const auto q=path[segment-1]+axis*t;const double d=QLineF(pixel,q).length();if(d<=distance){distance=d;closest=q;normal=QPointF(-axis.y(),axis.x())/std::sqrt(length2);}}
                        if(distance>=radius)continue;
                        const auto left=closest-normal*(radius*1.4),right=closest+normal*(radius*1.4);
                        if(!QRectF(source.rect()).contains(left) || !QRectF(source.rect()).contains(right) || at(skin,int(left.x()),int(left.y()),source.size())<.7 || at(skin,int(right.x()),int(right.y()),source.size())<.7)continue;
                        const double t=std::clamp(.5+QPointF::dotProduct(pixel-closest,normal)/(radius*2.8),0.,1.);
                        const double reference=lightAt(left)*(1-t)+lightAt(right)*t,localLight=lightAt(pixel);const auto* p=original+x*4;const double current=lum(p)/std::max(.001F,p[3]);if(current<.003)continue;
                        const double residual=reference-localLight,threshold=reference*.025;
                        const double delta=std::copysign(std::max(0.,std::abs(residual)-threshold),residual);
                        const double fade=std::clamp((1-distance/radius)/std::max(.01,stroke.feather),0.,1.);
                        const double weight=fade*fade*(3-2*fade)*stroke.opacity*strength*protection(x,y)*(kind=="hairline"?1:1-at(preserved,x,y,source.size()))*at(skin,x,y,source.size())*(edit_coverage.isNull()?1:at(edit_coverage,x,y,source.size()));
                        const double change=std::clamp(delta*.75,-current*.2,current*.25)*weight;
                        for(int c=0;c<3;++c)out[(x-region.x())*4+c]+=float(p[c]*change/current);
                    }
                }
                continue;
            }
            const QPointF offset(stroke.offset.x()*source.width(),stroke.offset.y()*source.height());QRect sourceBounds=bounds.translated(offset).toAlignedRect();for(std::size_t i=0;i<stroke.source_offsets.size();++i){const auto q=stroke.points[i]+stroke.source_offsets[i];sourceBounds=sourceBounds.united(QRectF(q.x()*source.width()-radius-3,q.y()*source.height()-radius-3,radius*2+6,radius*2+6).toAlignedRect());}const auto crop=destination.united(sourceBounds).adjusted(-3,-3,3,3).intersected(source.rect());
            auto local=stroke;for(auto& p:local.points)p={(p.x()*source.width()-crop.x())/crop.width(),(p.y()*source.height()-crop.y())/crop.height()};local.offset={offset.x()/crop.width(),offset.y()/crop.height()};for(auto& o:local.source_offsets)o={o.x()*source.width()/crop.width(),o.y()*source.height()/crop.height()};local.radius=radius/std::min(crop.width(),crop.height());
            // A stroke's full source and boundary are evaluated even for a partial display ROI.
            const auto original=source.copy(crop);QImage strokeCoverage;const auto repaired=apply_healing(original,QJsonArray{healing_json(local)},cancelled,&strokeCoverage);
            auto skin=portrait_effect_mask(person,kind=="hairline"?"hair.smooth":"skin",source.size().scaled(768,768,Qt::KeepAspectRatio));
            // Explicit clone strokes must reach missing hair, not only existing hair pixels.
            if(kind=="hairline")skin.fill(255);
            // Confirmed candidates already contain their validated skin footprint.
            // Reapplying the original color mask would erase repaired detection holes.
            if(object["validatedCandidateCoverage"].toBool() && !edit_coverage.isNull())skin.fill(255);
            if(kind=="makeup" && object.contains("makeupPart"))skin=portrait_makeup_repair_mask(person,object["makeupPart"].toString(),skin.size());
            else if(kind=="makeup")for(const auto* key:{"makeup.lip","makeup.brow.left","makeup.brow.right","makeup.shadow.left","makeup.shadow.right"}){const auto extra=portrait_effect_mask(person,QLatin1String(key),skin.size());for(int y=0;y<skin.height();++y)for(int x=0;x<skin.width();++x)skin.scanLine(y)[x]=std::max(skin.constScanLine(y)[x],extra.constScanLine(y)[x]);}
            for(int y=visible.top();y<=visible.bottom();++y){auto* out=reinterpret_cast<float*>(result.scanLine(y-region.y()));const auto* a=reinterpret_cast<const float*>(original.constScanLine(y-crop.y()));const auto* b=reinterpret_cast<const float*>(repaired.constScanLine(y-crop.y()));for(int x=visible.left();x<=visible.right();++x){const int i=(x-crop.x())*4,j=(x-region.x())*4;const double w=strength*protection(x,y)*(kind=="hairline"?1:1-at(preserved,x,y,source.size()))*at(skin,x,y,source.size())*(edit_coverage.isNull()?1:at(edit_coverage,x,y,source.size()));const double coverage=reinterpret_cast<const float*>(strokeCoverage.constScanLine(y-crop.y()))[i];for(int c=0;c<3;++c)out[j+c]+=float(((b[i+c]-a[i+c])+(a[i+c]-out[j+c])*coverage)*w);}}
        }
        const std::array<QString,7> keys{"eye.circles","eye.bags","eye.white","eye.red","teeth.yellow","teeth.light","repair.conceal"};
        for(const auto& key:keys){const double value=parameters[key].toDouble()*.01*strength;if(value==0 || settings["disabledParameters"].toObject()[key].toBool() || !enabled[key.section('.',0,0)].toBool(true))continue;const auto mask=portrait_effect_mask(person,key,source.size().scaled(768,768,Qt::KeepAspectRatio));
            if((key=="eye.circles" && settings["eyeCirclesVersion"].toInt(1)>=2) || (key=="eye.bags" && settings["eyeBagsVersion"].toInt(1)>=2)){
                const bool circles=key=="eye.circles";
                const auto automatic=settings["automaticMasks"].toObject();
                const auto decode=[&](const char* name){return QImage::fromData(QByteArray::fromBase64(automatic[name].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);};
                const auto skin=decode("skin"),bags=decode("eye.bags"),frame=decode("eyeglassFrame");
                const double sigma=std::clamp(person["face"].toArray()[2].toDouble()*source.width()*(circles?.06:.035),1.5,24.);
                const int halo=int(std::ceil(sigma*4))+2;
                int minX=mask.width(),minY=mask.height(),maxX=-1,maxY=-1;
                for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x)if(mask.constScanLine(y)[x]){minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);}
                if(maxX<0)continue;
                const auto area=QRect(QPoint(int(double(minX)*source.width()/mask.width()),int(double(minY)*source.height()/mask.height())),QPoint(int(std::ceil(double(maxX+1)*source.width()/mask.width())),int(std::ceil(double(maxY+1)*source.height()/mask.height())))).intersected(region);
                for(int top=area.top();top<=area.bottom();top+=384)for(int left=area.left();left<=area.right();left+=384){
                    if(cancelled && cancelled->load())throw EditError("人像修复已取消");
                    const QRect tile(left,top,std::min(384,area.right()-left+1),std::min(384,area.bottom()-top+1));
                    const auto expanded=tile.adjusted(-halo,-halo,halo,halo).intersected(source.rect());
                    cv::Mat samples(expanded.height(),expanded.width(),CV_32FC4),reference,band;
                    for(int y=0;y<expanded.height();++y){const auto* row=reinterpret_cast<const float*>(source.constScanLine(y+expanded.y()));
                        for(int x=0;x<expanded.width();++x){const int sx=x+expanded.x(),sy=y+expanded.y();const auto* pixel=row+sx*4;
                            const double weight=circles?at(skin,sx,sy,source.size())*(1-at(mask,sx,sy,source.size()))*(1-at(bags,sx,sy,source.size()))*(1-at(frame,sx,sy,source.size())):1.;
                            samples.at<cv::Vec4f>(y,x)={float(pixel[0]*weight),float(pixel[1]*weight),float(pixel[2]*weight),float(pixel[3]*weight)};
                        }
                    }
                    cv::GaussianBlur(samples,reference,{0,0},sigma,sigma,cv::BORDER_REFLECT_101);
                    if(!circles)cv::GaussianBlur(samples,band,{0,0},std::max(.7,sigma*.3),std::max(.7,sigma*.3),cv::BORDER_REFLECT_101);
                    for(int y=tile.top();y<=tile.bottom();++y){if(cancelled && cancelled->load())throw EditError("人像修复已取消");auto* row=reinterpret_cast<float*>(result.scanLine(y-region.y()));
                        for(int x=tile.left();x<=tile.right();++x){auto* pixel=row+(x-region.x())*4;
                            const double weight=value*at(mask,x,y,source.size())*protection(x,y)*(1-at(frame,x,y,source.size()));
                            if(weight<=0 || pixel[3]<.01F)continue;
                            const auto local=reference.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());if(local[3]<.05F)continue;
                            const double light=lum(pixel)/pixel[3],referenceLight=(.2627*local[0]+.678*local[1]+.0593*local[2])/local[3];if(light<.005 || referenceLight<.005)continue;
                            if(!circles){
                                const auto medium=band.at<cv::Vec4f>(y-expanded.y(),x-expanded.x());
                                const double mediumLight=(.2627*medium[0]+.678*medium[1]+.0593*medium[2])/std::max(.001F,medium[3]);
                                const double delta=std::clamp((referenceLight-mediumLight)*.65,-light*.12,light*.12)*weight;
                                for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*(1+delta/light));
                                continue;
                            }
                            std::array<double,3> chroma{},target{};double distance=0;
                            for(int c=0;c<3;++c){chroma[c]=pixel[c]/pixel[3]/light;target[c]=local[c]/local[3]/referenceLight;distance+=std::pow(chroma[c]-target[c],2);}
                            const double confidence=std::clamp((std::sqrt(distance)-.035)/.2,0.,1.);
                            const double blend=weight*confidence*.65;
                            const double correctedLight=light+std::min(std::max(0.,referenceLight-light)*.35,light*.18)*weight*confidence;
                            for(int c=0;c<3;++c)pixel[c]=float((chroma[c]+(target[c]-chroma[c])*blend)*correctedLight*pixel[3]);
                        }
                    }
                }
                continue;
            }
            const auto color=settings["concealColor"].toArray();
            cv::Mat concealBase;QRect concealArea;
            if(key=="repair.conceal" && settings["concealVersion"].toInt(1)>=2 && color.size()==3){
                const double sigma=std::clamp(person["face"].toArray()[2].toDouble()*source.width()*.012,1.,8.);const int halo=int(std::ceil(sigma*4))+2;concealArea=region.adjusted(-halo,-halo,halo,halo).intersected(source.rect());
                cv::Mat input(concealArea.height(),concealArea.width(),CV_32FC4,const_cast<uchar*>(source.constScanLine(concealArea.y()))+qint64(concealArea.x())*16,source.bytesPerLine());cv::GaussianBlur(input,concealBase,{0,0},sigma,sigma,cv::BORDER_REFLECT_101);
            }
            const auto frame=key=="eye.white" && settings["eyeWhiteVersion"].toInt(1)>=2?QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["eyeglassFrame"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8):QImage{};
            for(int y=region.top();y<=region.bottom();++y){if(cancelled && cancelled->load())throw EditError("人像修复已取消");auto* row=reinterpret_cast<float*>(result.scanLine(y-region.y()));for(int x=region.left();x<=region.right();++x){auto* p=row+(x-region.x())*4;double w=value*at(mask,x,y,source.size())*protection(x,y)*(1-at(frame,x,y,source.size()))*(key=="repair.conceal"?1-at(preserved,x,y,source.size()):1);if(w==0 || p[3]<.00001F)continue;const double l=lum(p);
                if(key=="teeth.light" || key=="eye.white"){
                    double gain=std::exp2(w*(key=="eye.white"?.4:.65));
                    if((key=="teeth.light" && settings["teethColorVersion"].toInt(1)>=2) || (key=="eye.white" && settings["eyeWhiteVersion"].toInt(1)>=2)){const double light=l/p[3];gain=light>=1?1:gain/(1+(gain-1)*std::max(0.,light));}
                    for(int c=0;c<3;++c)p[c]=float(p[c]*gain);
                }
                else if(key=="teeth.yellow"){
                    if(settings["teethColorVersion"].toInt(1)>=2){
                        const double yellow=std::max(0.,(double(p[0])+p[1])*.5-p[2]);
                        const double correction=std::min(yellow*.6*w,std::max(0.,double(std::min(p[0],p[1])))*(.2627+.678)/.0593);
                        p[2]+=float(correction);const double balance=correction*.0593/(.2627+.678);p[0]-=float(balance);p[1]-=float(balance);
                    }else for(int c=0;c<3;++c)p[c]=float(p[c]+(l-p[c])*w*.7);
                }
                else if(key=="eye.red"){
                    if(settings["redEyeVersion"].toInt(1)>=2){const double neutral=(double(p[1])+p[2])*.5;for(int c=0;c<3;++c)p[c]=float(p[c]+(neutral-p[c])*w);}
                    else{const double excess=std::max(0.,double(p[0])-std::max(p[1],p[2])*1.4);p[0]-=float(excess*w);}
                }
                else if(key=="repair.conceal" && color.size()==3){const double target=.2627*color[0].toDouble()+.678*color[1].toDouble()+.0593*color[2].toDouble();if(target>.001){
                    if(!concealBase.empty()){const auto base=concealBase.at<cv::Vec4f>(y-concealArea.y(),x-concealArea.x());const double baseY=.2627*base[0]+.678*base[1]+.0593*base[2];
                        if(baseY>.001){std::array<double,3> delta{};double gain=w*.75;for(int c=0;c<3;++c){delta[c]=l*(color[c].toDouble()/target-base[c]/baseY);if(delta[c]<0)gain=std::min(gain,double(p[c])/-delta[c]);}for(int c=0;c<3;++c)p[c]+=float(delta[c]*gain);}
                    }else for(int c=0;c<3;++c)p[c]+=float((color[c].toDouble()*l/target-p[c])*w*.75);
                }}
                else if(key=="eye.circles" || key=="eye.bags"){const double gain=std::exp2(w*.45);for(int c=0;c<3;++c)p[c]=float(p[c]*gain);}
            }}
        }
    }
    return result;
}
}
