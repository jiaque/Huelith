#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <opencv2/imgproc.hpp>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
namespace {
QImage decode(const QJsonObject& o,const char* key){return QImage::fromData(QByteArray::fromBase64(o[key].toString().toLatin1()),"PNG");}
double smooth(double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);}
double sample(const QImage& image,int x,int y,QSize size,bool sixteen=false){
    if(image.isNull())return 0;const double u=std::clamp((x+.5)*image.width()/size.width()-.5,0.,double(image.width()-1)),v=std::clamp((y+.5)*image.height()/size.height()-.5,0.,double(image.height()-1));const int ix=int(u),iy=int(v),jx=std::min(ix+1,image.width()-1),jy=std::min(iy+1,image.height()-1);
    const auto at=[&](int a,int b){return sixteen?reinterpret_cast<const quint16*>(image.constScanLine(b))[a]/65535.:image.constScanLine(b)[a]/255.;};return (at(ix,iy)*(1-u+ix)+at(jx,iy)*(u-ix))*(1-v+iy)+(at(ix,jy)*(1-u+ix)+at(jx,jy)*(u-ix))*(v-iy);
}
}
QJsonObject portrait_double_chin_analysis(const QImage& source,const QJsonObject& person){
    const auto lm=person["landmarks"].toArray();if(source.isNull() || lm.size()<66)return {};
    const QSize size=source.size().scaled(768,768,Qt::KeepAspectRatio);
    const auto image=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA32FPx4);
    const auto masks=person["settings"].toObject()["automaticMasks"].toObject();
    auto skin=decode(masks,"skin").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(skin.isNull())return {};
    const auto hair=decode(masks,"hair").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    const auto pt=[&](int i){const auto p=lm[i].toArray();return QPointF(p[0].toDouble()*size.width(),p[1].toDouble()*size.height());};
    auto axis=pt(45)-pt(36);const double span=std::hypot(axis.x(),axis.y());if(span<24)return {};axis/=span;
    QPointF down(-axis.y(),axis.x());if(QPointF::dotProduct(pt(8)-pt(27),down)<0)down=-down;const auto chin=pt(8);
    cv::Mat light(size.height(),size.width(),CV_32F),allowed(size.height(),size.width(),CV_8U,cv::Scalar(0));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(image.constScanLine(y))+4*x;
        const double l=p[3]>.01?(.2627*p[0]+.678*p[1]+.0593*p[2])/p[3]:0;
        light.at<float>(y,x)=float(std::log2(std::max(.003,l)));
        allowed.at<uchar>(y,x)=skin.constScanLine(y)[x]>210 && (hair.isNull() || hair.constScanLine(y)[x]<24) && l>.018 && l<.85?255:0;
    }
    // Erode the skin boundary before any softening: collars, hair and the
    // silhouette must not enter the movable region through a blurred mask.
    const int radius=std::max(1,int(span*.025));cv::erode(allowed,allowed,cv::getStructuringElement(cv::MORPH_ELLIPSE,{radius*2+1,radius*2+1}));
    cv::GaussianBlur(light,light,{0,0},std::max(1.,span*.025));
    const auto read=[&](const cv::Mat& m,QPointF p){const int x=int(std::lround(p.x())),y=int(std::lround(p.y()));return x<0 || y<0 || x>=size.width() || y>=size.height()?0.:m.type()==CV_8U?double(m.at<uchar>(y,x))/255.:double(m.at<float>(y,x));};
    std::vector<double> depths(41,-1),contrast(41,0);int valid=0;
    for(int i=0;i<=40;++i){const double u=(i-20)*.025;
        for(double v=.09;v<=.48;v+=.01){const auto q=chin+axis*(u*span)+down*(v*span);
            if(read(allowed,q)<.9 || read(allowed,q+down*(span*.08))<.9 || read(allowed,q-down*(span*.06))<.9)continue;
            const double c=.5*(read(light,q+down*(span*.08))+read(light,q-down*(span*.06)))-read(light,q);
            if(c>contrast[i]){contrast[i]=c;depths[i]=v;}
        }
        if(contrast[i]>.06)++valid;
    }
    if(valid<9)return {};
    QImage field(size,QImage::Format_Grayscale8);field.fill(0);QRect bounds;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){if(!allowed.at<uchar>(y,x))continue;
        const auto d=(QPointF(x,y)-chin)/span;const double u=QPointF::dotProduct(d,axis),v=QPointF::dotProduct(d,down);if(std::abs(u)>=.5 || v<=.035 || v>=.55)continue;
        const double index=(u/.025)+20;const int a=std::clamp(int(index),0,39),b=a+1;const double t=index-a;
        if(contrast[a]<=.06 || contrast[b]<=.06 || std::abs(depths[a]-depths[b])>.08)continue;
        const double center=depths[a]*(1-t)+depths[b]*t;
        const double w=smooth((.5-std::abs(u))/.12)*smooth((v-.035)/.07)*smooth((.55-v)/.12)*
            std::exp(-std::pow((v-center)/.075,2))*std::clamp((contrast[a]*(1-t)+contrast[b]*t)/.22,0.,1.);
        field.scanLine(y)[x]=uchar(std::lround(w*255));if(w>.01)bounds|=QRect(x,y,1,1);
    }
    if(bounds.isEmpty())return {};
    // The original segmentation remains a hard ceiling after smoothing.
    cv::Mat view(size.height(),size.width(),CV_8U,field.bits(),field.bytesPerLine());cv::GaussianBlur(view,view,{0,0},std::max(1.,span*.035));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)if(!allowed.at<uchar>(y,x))field.scanLine(y)[x]=0;
    return {{"version",2},{"mask",portrait_mask_png(field)},
        {"shift",QJsonArray{down.x()*span*.012/size.width(),down.y()*span*.012/size.height()}},
        {"bounds",QJsonArray{double(bounds.x()-radius*4)/size.width(),double(bounds.y()-radius*4)/size.height(),double(bounds.width()+radius*8)/size.width(),double(bounds.height()+radius*8)/size.height()}}};
}
QJsonObject portrait_forehead_volume(const QImage& source,const QJsonObject& person){
    const auto lm=person["landmarks"].toArray();if(source.isNull() || lm.size()<60)return {};
    for(int i:{17,21,22,26,36,39,42,45}){const auto p=lm[i].toArray();if(p.size()<2 || (p.size()>2 && p[2].toDouble()<.3))return {};}
    const QSize size=source.size().scaled(768,768,Qt::KeepAspectRatio);const auto image=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA32FPx4);
    const auto automatic=person["settings"].toObject()["automaticMasks"].toObject();auto skin=decode(automatic,"faceSkin").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(skin.isNull())return {};
    const auto frame=decode(automatic,"eyeglassFrame").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    const auto pt=[&](int i){const auto p=lm[i].toArray();return QPointF(p[0].toDouble()*size.width(),p[1].toDouble()*size.height());};
    const auto left=(pt(36)+pt(39))*.5,right=(pt(42)+pt(45))*.5;auto axis=right-left;const double span=std::hypot(axis.x(),axis.y());if(span<12)return {};axis/=span;QPointF down(-axis.y(),axis.x());if(QPointF::dotProduct(pt(8)-pt(27),down)<0)down=-down;QPointF origin;for(int i=17;i<=26;++i)origin+=pt(i);origin/=10.;
    cv::Mat weights(size.height(),size.width(),CV_32F,cv::Scalar(0)),logs(weights.size(),CV_32F,cv::Scalar(0)),region=weights.clone();double visible=0;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto d=(QPointF(x+.5,y+.5)-origin)/span;const double u=QPointF::dotProduct(d,axis),v=QPointF::dotProduct(d,down);
        // A tapered forehead band, with a deliberate gap above the eyebrows.
        const double shape=smooth((.88-std::abs(u))/.18)*smooth((-v-.10)/.15)*smooth((v+.90)/.22);if(shape==0)continue;
        const auto* p=reinterpret_cast<const float*>(image.constScanLine(y))+4*x;if(p[3]<.01F)continue;const double l=(.2627*p[0]+.678*p[1]+.0593*p[2])/p[3];if(!std::isfinite(l) || l<.003 || l>1.2)continue;
        const double w=shape*smooth((skin.constScanLine(y)[x]/255.-.65)/.30)*(frame.isNull()?1:1-frame.constScanLine(y)[x]/255.);region.at<float>(y,x)=float(w);weights.at<float>(y,x)=float(w);logs.at<float>(y,x)=float(w*std::log2(l));visible+=w;
    }if(visible<std::max(40.,span*span*.10))return {};
    const double sigma=std::max(1.,span*.07);cv::GaussianBlur(weights,weights,{0,0},sigma);cv::GaussianBlur(logs,logs,{0,0},sigma);
    struct Entry{int x,y;double u,v,weight,light;};std::vector<Entry> entries;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)if(region.at<float>(y,x)>.15 && weights.at<float>(y,x)>.08){const auto d=(QPointF(x+.5,y+.5)-origin)/span;entries.push_back({x,y,QPointF::dotProduct(d,axis),QPointF::dotProduct(d,down),region.at<float>(y,x),logs.at<float>(y,x)/weights.at<float>(y,x)});}
    if(entries.size()<32)return {};cv::Vec3d plane(0,0,0);
    // Fit the existing light gradient, reducing influence of local dark basins
    // and isolated specular highlights. Do not flatten the original light direction.
    for(int iteration=0;iteration<5;++iteration){cv::Matx33d matrix=cv::Matx33d::zeros();cv::Vec3d rhs(0,0,0);
        for(const auto& e:entries){const cv::Vec3d a(1,e.u,e.v);const double residual=e.light-plane.dot(a);const double robust=iteration==0?1:residual<0?1/(1+std::pow(residual/.12,2)):1/(1+std::pow(residual/.3,2));const double w=e.weight*robust;for(int r=0;r<3;++r){rhs[r]+=w*a[r]*e.light;for(int c=0;c<3;++c)matrix(r,c)+=w*a[r]*a[c];}}
        if(!cv::solve(matrix,rhs,plane,cv::DECOMP_SVD))return {};
    }
    QImage gain(size,QImage::Format_Grayscale16);gain.fill(0);double maximum=0;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const double w=region.at<float>(y,x);if(w<=0 || weights.at<float>(y,x)<.01)continue;const auto d=(QPointF(x+.5,y+.5)-origin)/span;const double deficit=plane.dot(cv::Vec3d(1,QPointF::dotProduct(d,axis),QPointF::dotProduct(d,down)))-logs.at<float>(y,x)/weights.at<float>(y,x);const double correction=std::clamp(deficit-.025,0.,.5)*w;reinterpret_cast<quint16*>(gain.scanLine(y))[x]=quint16(std::lround(correction*2*65535));maximum=std::max(maximum,correction);}
    return {{"version",1},{"gain",portrait_mask_png(gain)},{"maxStops",maximum},{"method","forehead-local-log-plane"}};
}
QJsonObject portrait_tear_volume(const QImage& source,const QJsonObject& person){
    const auto lm=person["landmarks"].toArray();if(source.isNull() || lm.size()<60)return {};
    const QSize size=source.size();const auto image=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA32FPx4);
    const auto automatic=person["settings"].toObject()["automaticMasks"].toObject();const auto skin=decode(automatic,"faceSkin").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(skin.isNull())return {};
    const auto frame=decode(automatic,"eyeglassFrame").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    const auto pt=[&](int i){const auto a=lm[i].toArray();return QPointF(a[0].toDouble()*size.width(),a[1].toDouble()*size.height());};
    cv::Mat weights(size.height(),size.width(),CV_32F,cv::Scalar(0)),logs(weights.size(),CV_32F,cv::Scalar(0));
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(image.constScanLine(y))+x*4;if(p[3]<.01F)continue;const double l=(.2627*p[0]+.678*p[1]+.0593*p[2])/p[3];if(!std::isfinite(l) || l<.003 || l>1.2)continue;const double w=smooth((skin.constScanLine(y)[x]/255.-.3)/.60)*(frame.isNull()?1:1-frame.constScanLine(y)[x]/255.);weights.at<float>(y,x)=float(w);logs.at<float>(y,x)=float(w*std::log2(l));}
    const auto read=[](const cv::Mat& m,QPointF p){const double x=std::clamp(p.x()-.5,0.,double(m.cols-1)),y=std::clamp(p.y()-.5,0.,double(m.rows-1));const int ix=int(x),iy=int(y),jx=std::min(ix+1,m.cols-1),jy=std::min(iy+1,m.rows-1);return (m.at<float>(iy,ix)*(1-x+ix)+m.at<float>(iy,jx)*(x-ix))*(1-y+iy)+(m.at<float>(jy,ix)*(1-x+ix)+m.at<float>(jy,jx)*(x-ix))*(y-iy);};
    QJsonObject result{{"version",1},{"samplingVersion",2}};bool any=false;
    for(int side=0;side<2;++side){const QString suffix=side==0?".left":".right";const int inner=side==0?42:39,outer=side==0?45:36;
        bool reliable=true;for(int i:{inner,outer}){const auto a=lm[i].toArray();if(a.size()<2 || (a.size()>2 && a[2].toDouble()<.3))reliable=false;}
        auto axis=pt(outer)-pt(inner);const double width=std::hypot(axis.x(),axis.y());if(!reliable || width<6){result["valid"+suffix]=false;continue;}axis/=width;QPointF down(-axis.y(),axis.x());if(QPointF::dotProduct(pt(8)-pt(27),down)<0)down=-down;
        const auto position=[&](double u,double v){return pt(inner)+(axis*u+down*v)*width;};cv::Mat light,weight;const double sigma=std::max(.7,width*.025);cv::GaussianBlur(logs,light,{0,0},sigma);cv::GaussianBlur(weights,weight,{0,0},sigma);
        const auto luminance=[&](QPointF p,double& value){if(p.x()<1 || p.y()<1 || p.x()>=size.width()-1 || p.y()>=size.height()-1 || read(weight,p)<.25)return false;value=read(light,p)/read(weight,p);return true;};
        constexpr int count=32;std::array<double,count> curve{},depth{},confidence{};int valid=0;
        // Search a narrow inner-orbital track, below the eyelid/pretarsal bulge.
        // A symmetric normal profile rejects a mere illumination gradient.
        const QPointF normal=(-axis*.55+down)/std::sqrt(1+.55*.55);
        for(int i=0;i<count;++i){const double u=.02+.66*i/(count-1),prior=.27+.55*u;curve[i]=prior;double best=-1;
            for(int step=-4;step<=4;++step){const double v=prior+step*.02;const auto center=position(u,v);double c,a,b;if(!luminance(center,c) || !luminance(center-normal*(width*.13),a) || !luminance(center+normal*(width*.13),b))continue;
                const double deficit=(a+b)*.5-c,score=deficit-.025*std::pow(step*.2,2);if(score>best){best=score;curve[i]=v;confidence[i]=1;depth[i]=std::clamp(deficit-.035,0.,.4);}
            }valid+=confidence[i]>0;
        }
        if(valid<count/2){result["valid"+suffix]=false;continue;}any=true;result["valid"+suffix]=true;
        for(int pass=0;pass<2;++pass){auto filtered=curve;for(int i=1;i<count-1;++i)filtered[i]=curve[i]*.5+(curve[i-1]+curve[i+1])*.25;curve=filtered;}
        // Re-estimate at the smoothed path, rather than applying the score from
        // a different location. Broad bags or color casts are not this control.
        for(int i=0;i<count;++i){double c,a,b;const auto center=position(.02+.66*i/(count-1),curve[i]);if(!luminance(center,c) || !luminance(center-normal*(width*.13),a) || !luminance(center+normal*(width*.13),b)){depth[i]=0;continue;}depth[i]=std::clamp((a+b)*.5-c-.035,0.,.4);}
        QJsonArray path;QPolygonF points;double maximum=0;for(int i=0;i<count;++i){const auto p=position(.02+.66*i/(count-1),curve[i]);points<<p;path.append(QJsonArray{p.x()/size.width(),p.y()/size.height()});maximum=std::max(maximum,depth[i]);}
        QImage gain(size,QImage::Format_Grayscale16);gain.fill(0);const double radius=std::max(.8,width*.07);const QRect box=points.boundingRect().adjusted(-radius,-radius,radius,radius).toAlignedRect().intersected(QRect(QPoint{},size));
        for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){const QPointF p(x+.5,y+.5);double correction=0;
            for(int i=0;i<count-1;++i){const auto delta=points[i+1]-points[i];const double t=std::clamp(QPointF::dotProduct(p-points[i],delta)/std::max(.001,QPointF::dotProduct(delta,delta)),0.,1.);const auto d=p-(points[i]+delta*t);const double distance=std::hypot(d.x(),d.y());if(distance>=radius)continue;const double edge=smooth((i+t)/3.)*smooth((count-1-i-t)/3.);correction=std::max(correction,((1-t)*depth[i]+t*depth[i+1])*smooth(1-distance/radius)*edge);}
            correction*=smooth((QPointF::dotProduct(p-pt(inner),down)/width-.22)/.08)*smooth((skin.constScanLine(y)[x]/255.-.3)/.60)*(frame.isNull()?1:1-frame.constScanLine(y)[x]/255.);reinterpret_cast<quint16*>(gain.scanLine(y))[x]=quint16(std::lround(std::clamp(correction*2,0.,1.)*65535));
        }
        const auto bounds=box.adjusted(-2,-2,2,2).intersected(QRect(QPoint{},size));result["bounds"+suffix]=QJsonArray{double(bounds.x())/size.width(),double(bounds.y())/size.height(),double(bounds.width())/size.width(),double(bounds.height())/size.height()};result["gain"+suffix]=portrait_mask_png(gain);result["curve"+suffix]=path;result["maxStops"+suffix]=maximum;
    }return any?result:QJsonObject{};
}
static QJsonObject cheek_volume_analysis(const QImage& source,const QJsonObject& person,int regionKind){
    const bool lateral=regionKind==1,orbital=regionKind==2,oral=regionKind==3;
    const auto lm=person["landmarks"].toArray();if(source.isNull() || lm.size()<66)return {};
    const QSize size=source.size();
    const auto image=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA32FPx4);
    const auto masks=person["settings"].toObject()["automaticMasks"].toObject();
    const auto skin=decode(masks,"faceSkin").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(skin.isNull())return {};
    const auto frame=decode(masks,"eyeglassFrame").convertToFormat(QImage::Format_Grayscale8).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    const auto pt=[&](int i){const auto a=lm[i].toArray();return QPointF(a[0].toDouble()*size.width(),a[1].toDouble()*size.height());};
    const auto face=pt(16)-pt(0);const double scale=std::hypot(face.x(),face.y());if(scale<16)return {};
    QJsonObject result{{"version",1},{"samplingVersion",2},{"method",oral?"perioral-anatomical-log-basin":orbital?"orbital-anatomical-log-basin":lateral?"cheek-anatomical-log-basin":"malar-anatomical-log-basin"}};bool any=false;
    for(int side=0;side<2;++side){
        const QString suffix=side==0?".left":".right";const int lower=lateral?(side==0?15:1):(side==0?46:41),alar=side==0?35:31,edge=side==0?13:3,lip=side==0?62:58;
        bool reliable=true;for(int i:{lower,alar,edge,lip}){const auto a=lm[i].toArray();if(a.size()<2 || (a.size()>2 && a[2].toDouble()<.3))reliable=false;}if(!reliable){result["valid"+suffix]=false;continue;}
        // Inset anatomical cheek patch: no ellipse centered on the cheek and
        // no invented highlight on a flat or already convex lighting field.
        const QPointF center=lateral?pt(lower)*.15+pt(alar)*.10+pt(edge)*.35+pt(lip)*.25+pt(side==0?12:4)*.15:pt(lower)*.30+pt(alar)*.25+pt(edge)*.30+pt(lip)*.15;
        QPolygonF polygon;if(lateral){for(auto p:{pt(lower),pt(edge),pt(side==0?12:4),pt(lip),pt(alar)})polygon<<center+(p-center)*.80;}else for(auto p:{pt(lower),pt(edge),pt(lip),pt(alar)})polygon<<center+(p-center)*.78;
        if(orbital || oral){
            const int outer=oral?(side==0?62:58):(side==0?45:36);const auto point=lm[outer].toArray();if(point.size()>2 && point[2].toDouble()<.3){result["valid"+suffix]=false;continue;}
            auto axis=(pt(42)+pt(45)-pt(36)-pt(39))*.5;axis/=std::max(1.,std::hypot(axis.x(),axis.y()));const QPointF down(-axis.y(),axis.x());axis*=side==0?1.:-1.;
            polygon.clear();for(auto uv:{QPointF(.02,oral?-.04:-.10),QPointF(.18,oral?-.04:-.10),QPointF(.18,oral?.20:.12),QPointF(.02,oral?.20:.12)})polygon<<pt(outer)+(axis*uv.x()+down*uv.y())*scale;
        }
        std::vector<cv::Point2f> contour;for(auto p:polygon)contour.emplace_back(float(p.x()),float(p.y()));
        const QRect box=polygon.boundingRect().toAlignedRect().intersected(QRect(QPoint{},size));
        cv::Mat region(size.height(),size.width(),CV_32F,cv::Scalar(0)),logs=region.clone();double visible=0;
        for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){
            const double distance=cv::pointPolygonTest(contour,cv::Point2f(x+.5F,y+.5F),true);if(distance<=0)continue;
            if(orbital || oral){bool protectedFeature=false;for(int i=17;i<(oral?66:48);++i)if(oral || i<27 || i>=36){const auto delta=QPointF(x+.5,y+.5)-pt(i);if(std::hypot(delta.x(),delta.y())<scale*.028){protectedFeature=true;break;}}if(protectedFeature)continue;}

            const auto* pixel=reinterpret_cast<const float*>(image.constScanLine(y))+4*x;if(pixel[3]<.01F)continue;
            const double light=(.2627*pixel[0]+.678*pixel[1]+.0593*pixel[2])/pixel[3];if(!std::isfinite(light) || light<.003 || light>1.2)continue;
            const double weight=smooth(distance/std::max(1.,scale*.035))*smooth((skin.constScanLine(y)[x]/255.-.65)/.30)*(frame.isNull()?1:1-frame.constScanLine(y)[x]/255.);
            region.at<float>(y,x)=float(weight);logs.at<float>(y,x)=float(weight*std::log2(light));visible+=weight;
        }
        if(visible<std::max(4.,scale*scale*.002)){result["valid"+suffix]=false;continue;}
        cv::Mat weights;cv::GaussianBlur(region,weights,{0,0},std::max(.7,scale*.012));cv::GaussianBlur(logs,logs,{0,0},std::max(.7,scale*.012));
        cv::Vec3d plane(0,0,0);
        for(int iteration=0;iteration<5;++iteration){cv::Matx33d matrix=cv::Matx33d::zeros();cv::Vec3d rhs(0,0,0);
            for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){const double w=region.at<float>(y,x);if(w<.15 || weights.at<float>(y,x)<.05)continue;
                const cv::Vec3d a(1,(x+.5-center.x())/scale,(y+.5-center.y())/scale);const double light=logs.at<float>(y,x)/weights.at<float>(y,x),residual=light-plane.dot(a);
                const double robust=iteration==0?1:1/(1+std::pow(residual/(residual<0?.10:.25),2));for(int r=0;r<3;++r){rhs[r]+=w*robust*a[r]*light;for(int c=0;c<3;++c)matrix(r,c)+=w*robust*a[r]*a[c];}}
            if(!cv::solve(matrix,rhs,plane,cv::DECOMP_SVD)){reliable=false;break;}
        }
        if(!reliable){result["valid"+suffix]=false;continue;}
        QImage gain(size,QImage::Format_Grayscale16);gain.fill(0);double maximum=0;
        for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){if(weights.at<float>(y,x)<.01)continue;
            const double deficit=plane.dot(cv::Vec3d(1,(x+.5-center.x())/scale,(y+.5-center.y())/scale))-logs.at<float>(y,x)/weights.at<float>(y,x);
            const double stops=std::clamp(deficit-.025,0.,.30)*region.at<float>(y,x);reinterpret_cast<quint16*>(gain.scanLine(y))[x]=quint16(std::lround(stops*2*65535));maximum=std::max(maximum,stops);
        }
        const auto bounds=box.adjusted(-2,-2,2,2).intersected(QRect(QPoint{},size));result["bounds"+suffix]=QJsonArray{double(bounds.x())/size.width(),double(bounds.y())/size.height(),double(bounds.width())/size.width(),double(bounds.height())/size.height()};
        result["valid"+suffix]=true;result["gain"+suffix]=portrait_mask_png(gain);result["maxStops"+suffix]=maximum;any=true;
    }return any?result:QJsonObject{};
}
QJsonObject portrait_malar_volume(const QImage& source,const QJsonObject& person){return cheek_volume_analysis(source,person,0);}
QJsonObject portrait_cheek_volume(const QImage& source,const QJsonObject& person){return cheek_volume_analysis(source,person,1);}
QJsonObject portrait_perioral_volume(const QImage& source,const QJsonObject& person){return cheek_volume_analysis(source,person,3);}
QJsonObject portrait_orbital_volume(const QImage& source,const QJsonObject& person){return cheek_volume_analysis(source,person,2);}
QJsonObject portrait_volume_combination(const QImage& source,const QJsonObject& person,const std::atomic_bool* cancelled){
    if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool() || person["settings"].toObject()["partialSkin"].toBool())return {};
    auto settings=person["settings"].toObject();auto masks=settings["automaticMasks"].toObject();
    if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=portrait_mask_png(portrait_frame_protection(source,person));
    settings["automaticMasks"]=masks;auto candidate=person;candidate["settings"]=settings;
    auto parameters=settings["parameters"].toObject(),disabled=settings["disabledParameters"].toObject();QJsonArray applied,skipped;
    const auto check=[&]{if(cancelled && cancelled->load())throw EditError("丰盈分析已取消");};
    const auto put=[&](QString key,int value){parameters[key]=value;disabled.remove(key);disabled.remove(key.section('.',0,1));};
    check();const auto forehead=portrait_forehead_volume(source,candidate);settings["foreheadVolume"]=forehead;
    put("face.foreheadFill",forehead.isEmpty()?0:40);(forehead.isEmpty()?skipped:applied).append(QStringLiteral("额头"));
    struct Part{const char* cache;const char* key;const char* label;QJsonObject (*analyze)(const QImage&,const QJsonObject&);int strength;};
    bool cheeks[2]{};
    for(const auto& part:std::array<Part,4>{{{"tearVolume","face.tearFill","泪沟",portrait_tear_volume,45},{"malarVolume","face.malarTone","苹果肌",portrait_malar_volume,35},{"cheekVolume","face.cheekTone","面颊",portrait_cheek_volume,35},{"perioralVolume","face.perioralTone","口周",portrait_perioral_volume,25}}}){
        check();const auto analysis=part.analyze(source,candidate);settings[part.cache]=analysis;
        for(int side=0;side<2;++side){const QString suffix=side==0?".left":".right";const bool valid=analysis["valid"+suffix].toBool();put(QString(part.key)+suffix,valid?part.strength:0);
            (valid?applied:skipped).append(QString::fromUtf8(part.label)+(side==0?QStringLiteral("·人物左侧"):QStringLiteral("·人物右侧")));
            if(QString(part.key)=="face.malarTone")put("face.malarShape"+suffix,valid?20:0);
            if(QString(part.key)=="face.cheekTone")cheeks[side]=valid;
        }
    }
    // The current cheek-outline control is bilateral. Do not move an unknown
    // half merely because the opposite half was detected successfully.
    if(applied.isEmpty())return {};
    const auto landmarks=person["landmarks"].toArray();
    for(int side=0;side<2;++side){bool valid=landmarks.size()>=66;for(int i:side==0?std::array<int,3>{16,26,45}:std::array<int,3>{0,17,36})if(valid){const auto p=landmarks[i].toArray();valid=p.size()>=2 && (p.size()<3 || p[2].toDouble()>=.3);}put(QString("face.templeFill")+(side==0?".left":".right"),valid?20:0);}
    put("face.volume",cheeks[0] && cheeks[1]?20:0);settings["cheekVolumeVersion"]=2;
    settings["parameters"]=parameters;settings["disabledParameters"]=disabled;auto enabled=settings["enabled"].toObject();enabled["face"]=true;settings["enabled"]=enabled;
    check();return {{"settings",settings},{"applied",applied},{"skipped",skipped}};
}
QJsonObject portrait_lift_combination(const QImage& source,const QJsonObject& person,const std::atomic_bool* cancelled){
    if(source.isNull() || person["unmatched"].toBool() || person["matchAmbiguous"].toBool() || person["settings"].toObject()["partialSkin"].toBool())return {};
    auto settings=person["settings"].toObject();auto masks=settings["automaticMasks"].toObject();
    if(decode(masks,"faceSkin").isNull())return {};
    if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=portrait_mask_png(portrait_frame_protection(source,person));
    settings["automaticMasks"]=masks;auto candidate=person;candidate["settings"]=settings;
    auto parameters=settings["parameters"].toObject(),disabled=settings["disabledParameters"].toObject();QJsonArray applied,skipped;
    const auto check=[&]{if(cancelled && cancelled->load())throw EditError("提拉分析已取消");};
    const auto put=[&](const QString& key,int amount,bool valid,const QString& label){parameters[key]=valid?amount:0;disabled.remove(key);disabled.remove(key.section('.',0,1));(valid?applied:skipped).append(label);};
    const auto lm=person["landmarks"].toArray();const auto reliable=[&](int index){if(index>=lm.size())return false;const auto p=lm[index].toArray();return p.size()>=2 && (p.size()<3 || p[2].toDouble()>=.3);};
    check();put("face.foreheadLift",25,portrait_forehead_lift_available(candidate,source.size()),QStringLiteral("额头"));
    check();settings["orbitalVolume"]=portrait_orbital_volume(source,candidate);
    check();settings["perioralVolume"]=portrait_perioral_volume(source,candidate);
    for(int side=0;side<2;++side){
        check();const QString suffix=side==0?".left":".right",label=side==0?QStringLiteral("·人物左侧"):QStringLiteral("·人物右侧");
        bool contour=lm.size()>=66;for(int j=0;j<=8;++j)contour=contour && reliable(side==0?16-j:j);
        bool middle=true;for(int i:side==0?std::array<int,4>{46,35,13,62}:std::array<int,4>{41,31,3,58})middle=middle && reliable(i);
        const bool outer=portrait_orbital_lift_available(candidate,source.size(),side),oral=portrait_perioral_lift_available(candidate,source.size(),side);
        put("face.contourLift"+suffix,30,contour && (outer || oral),QStringLiteral("轮廓")+label);
        put("face.midfaceLift"+suffix,30,middle && (outer || oral),QStringLiteral("面中")+label);
        put("face.orbitalLift"+suffix,25,outer,QStringLiteral("眼周形态")+label);
        put("face.perioralLift"+suffix,25,oral,QStringLiteral("口周形态")+label);
        put("face.orbitalTone"+suffix,20,settings["orbitalVolume"].toObject()["valid"+suffix].toBool(),QStringLiteral("眼周明暗")+label);
        put("face.perioralTone"+suffix,20,settings["perioralVolume"].toObject()["valid"+suffix].toBool(),QStringLiteral("口周明暗")+label);
    }
    if(applied.isEmpty())return {};
    settings["parameters"]=parameters;settings["disabledParameters"]=disabled;auto enabled=settings["enabled"].toObject();enabled["face"]=true;settings["enabled"]=enabled;
    check();return {{"settings",settings},{"applied",applied},{"skipped",skipped}};
}
QImage portrait_volume_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled){
    const auto s=person["settings"].toObject();if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool() || !s["enabled"].toObject()["face"].toBool(true))return base;
    const auto parameters=s["parameters"].toObject(),disabled=s["disabledParameters"].toObject();
    for(const auto& key:QStringList{"face.foreheadFill","face.tearFill.left","face.tearFill.right","face.malarTone.left","face.malarTone.right","face.cheekTone.left","face.cheekTone.right","face.orbitalTone.left","face.orbitalTone.right","face.perioralTone.left","face.perioralTone.right"}){
        const bool forehead=key=="face.foreheadFill",malar=key.startsWith("face.malarTone"),cheek=key.startsWith("face.cheekTone"),orbital=key.startsWith("face.orbitalTone"),oral=key.startsWith("face.perioralTone");if(disabled[forehead?key:oral?QString("face.perioralTone"):orbital?QString("face.orbitalTone"):cheek?QString("face.cheekTone"):malar?QString("face.malarTone"):QString("face.tearFill")].toBool() || disabled[key].toBool())continue;
        const double strength=std::clamp(parameters[key].toDouble()*.01*amount*s["strength"].toDouble(100)*.01,0.,1.);if(strength==0)continue;
        const auto data=s[forehead?"foreheadVolume":oral?"perioralVolume":orbital?"orbitalVolume":cheek?"cheekVolume":malar?"malarVolume":"tearVolume"].toObject();if(data["version"].toInt()!=1)continue;const auto gain=decode(data,forehead?"gain":key.endsWith("left")?"gain.left":"gain.right").convertToFormat(QImage::Format_Grayscale16);if(gain.isNull())continue;
        const QString root=key.endsWith(".left") || key.endsWith(".right")?key.section('.',0,1):key;
        const auto localMask=decode(s["masks"].toObject(),s["volumeRangeLinks"].toObject()[root].toString(root).toUtf8().constData());
        QRect draw=region;const auto bounds=data[forehead?"bounds":key.endsWith("left")?"bounds.left":"bounds.right"].toArray();if(bounds.size()==4){const QRectF area(bounds[0].toDouble()*size.width(),bounds[1].toDouble()*size.height(),bounds[2].toDouble()*size.width(),bounds[3].toDouble()*size.height());draw=draw.intersected(area.toAlignedRect());}
        for(int y=draw.top();y<=draw.bottom();++y){if(cancelled && cancelled->load())throw EditError("丰盈明暗已取消");auto* row=reinterpret_cast<float*>(base.scanLine(y-region.y()));for(int x=draw.left();x<=draw.right();++x){const double stops=.5*sample(gain,x,y,size,true)*strength*(localMask.isNull()?1:sample(localMask,x,y,size))*(effect_mask.isNull()?1:sample(effect_mask,x,y,size));if(stops<=0)continue;auto* p=row+(x-region.x())*4;if(p[3]<.001F)continue;const double headroom=p[3]/std::max(.00001F,std::max({p[0],p[1],p[2]})),factor=std::min(std::exp2(stops),std::max(1.,headroom));for(int c=0;c<3;++c)p[c]*=float(factor);}}
    }return base;
}
}
