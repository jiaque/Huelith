#pragma once
#include "huelith/editing/project.h"
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QLineF>
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace huelith::editing {
// Width of visible neck skin in its own chin/clavicle frame. It does not
// scale the chin, rotate the head or narrow the shoulders.
class PortraitNeck {
public:
    PortraitNeck(QSize size,const QJsonObject& person,double width):size_(size),width_(std::clamp(width,-1.,1.)) {
        const auto settings=person["settings"].toObject();const auto pose=settings["pose"].toArray(),landmarks=person["landmarks"].toArray();
        if(pose.size()!=33 || landmarks.size()!=66 || person["bodyAmbiguous"].toBool())throw EditError("肩部或下颌定位不足，不能可靠调整颈部");
        const auto point=[&](const QJsonArray& points,int i){const auto q=points[i].toArray();if(q.size()<2 || (q.size()>2 && q[2].toDouble()<.6))throw EditError("颈部定位置信度不足，请检查肩部与下巴位置");return QPointF(q[0].toDouble()*(size.width()-1),q[1].toDouble()*(size.height()-1));};
        const auto left=point(pose,11),right=point(pose,12),chin=point(landmarks,8);
        const auto face=person["face"].toArray();const double faceWidth=face.size()==4?face[2].toDouble()*(size.width()-1):0;
        QPointF eyes;for(int i=36;i<48;++i)eyes+=point(landmarks,i);eyes/=12.;
        const auto down=chin-eyes;const double downLength=std::hypot(down.x(),down.y());
        const auto cross=[](QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();};
        const auto shoulder=right-left;
        if(downLength<1 || std::abs(cross(down,shoulder))<downLength*QLineF(left,right).length()*.25)
            throw EditError("颈部与肩线定位异常，请修正定位");
        // The shoulder midpoint shifts sideways in a three-quarter pose. Use
        // the head's longitudinal axis intersecting the clavicle line instead
        // of interpreting that perspective offset as neck tilt.
        const double along=cross(left-chin,shoulder)/cross(down,shoulder);
        a_=chin+down*along;
        const double across=QPointF::dotProduct(a_-left,shoulder)/QPointF::dotProduct(shoulder,shoulder);
        if(along<=0 || across<0 || across>1)
            throw EditError("肩部定位落在下巴附近，无法确定颈部；请重新识别人物或修正肩部定位");
        const auto delta=chin-a_;length_=std::hypot(delta.x(),delta.y());
        if(length_<8 || faceWidth<12 || length_>faceWidth*1.7)throw EditError("颈部长度或姿态异常，请修正定位");
        axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        const auto skin=QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["skin"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(skin.isNull())throw EditError("缺少可见颈部皮肤范围，不能自动调整颈部");
        const auto hair=QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(!hair.isNull()){
            hairSize_=hair.size();hairDistance_.resize(std::size_t(hair.width())*hair.height(),1e9F);
            const float sx=float(size.width()-1)/hair.width(),sy=float(size.height()-1)/hair.height(),diagonal=std::hypot(sx,sy);
            hairMargin_=2*std::max(sx,sy);
            for(int y=0;y<hair.height();++y)for(int x=0;x<hair.width();++x)if(hair.constScanLine(y)[x]>100)hairDistance_[std::size_t(y)*hair.width()+x]=0;
            // Two-pass distance to the protected hair in original pixels.
            // Its smooth ramp avoids a hard mask seam in the neck warp.
            for(int pass=0;pass<2;++pass){const int direction=pass==0?1:-1;
                for(int y=pass==0?0:hair.height()-1;y>=0 && y<hair.height();y+=direction)
                    for(int x=pass==0?0:hair.width()-1;x>=0 && x<hair.width();x+=direction){
                        auto& distance=hairDistance_[std::size_t(y)*hair.width()+x];
                        const auto relax=[&](int xx,int yy,float cost){if(xx>=0 && xx<hair.width() && yy>=0 && yy<hair.height())distance=std::min(distance,hairDistance_[std::size_t(yy)*hair.width()+xx]+cost);};
                        relax(x-direction,y,sx);relax(x,y-direction,sy);relax(x-direction,y-direction,diagonal);relax(x+direction,y-direction,diagonal);
                    }
            }
        }
        const auto inside=[&](QPointF q){const double u=q.x()/(size.width()-1),v=q.y()/(size.height()-1);return u>=0 && u<1 && v>=0 && v<1 && skin.constScanLine(int(v*skin.height()))[int(u*skin.width())]>180;};
        // Shoulder landmarks describe the arm joints, not the clavicle under
        // the chin. In a turned torso their line can lie above most of the
        // visible neck. Locate the lower skin boundary on the head axis.
        const auto downAxis=down/downLength;const double axialStep=std::max(.5,faceWidth/180.);
        double skinEnd=0;bool entered=false,closed=false;
        for(double r=axialStep;r<faceWidth*.8;r+=axialStep){
            if(inside(chin+downAxis*r)){entered=true;skinEnd=r;}
            else if(entered){skinEnd=r-axialStep*.5;closed=true;break;}
            else if(r>faceWidth*.12)break;
        }
        if(!closed || skinEnd<8)throw EditError("无法确定可见颈部与衣领边界，请修正皮肤范围");
        a_=chin+downAxis*skinEnd;length_=skinEnd;axis_=-downAxis;normal_={-axis_.y(),axis_.x()};
        std::array<bool,count> valid{};int bestStart=0,bestCount=0,runStart=0,runCount=0;
        for(int i=0;i<count;++i){const double t=.15+.70*i/(count-1);const auto p=a_+axis_*(length_*t);valid[i]=inside(p);
            if(valid[i])for(int side=0;side<2;++side){double edge=0;const double step=std::max(.5,faceWidth/180.);
                for(double r=step;r<faceWidth*.7;r+=step)if(!inside(p+normal_*(side==0?-r:r))){edge=r-step*.5;break;}
                if(edge<faceWidth*.015){valid[i]=false;break;}radii_[side][i]=edge;
            }
            // A turned neck need not have equal visible skin on both sides
            // of the head axis; require a reliable total span instead.
            if(valid[i] && radii_[0][i]+radii_[1][i]<faceWidth*.14)valid[i]=false;
            if(valid[i]){if(runCount==0)runStart=i;++runCount;if(runCount>bestCount){bestCount=runCount;bestStart=runStart;}}else runCount=0;
        }
        if(bestCount<12)throw EditError("可见颈部皮肤不足或被头发、衣领遮挡，请使用手动液化");
        start_=.15+.7*bestStart/(count-1);end_=.15+.7*(bestStart+bestCount-1)/(count-1);
        // Skin segmentation can connect a neck cross-section to the jaw or
        // shoulder for just a few rows. Do not turn those sudden wide runs
        // into lateral shear. Fit a conservative, smooth visible contour.
        for(auto& radii:radii_){
            auto conservative=radii;
            for(int i=bestStart;i<bestStart+bestCount;++i)
                for(int j=std::max(bestStart,i-2);j<=std::min(bestStart+bestCount-1,i+2);++j)
                    conservative[i]=std::min(conservative[i],radii[j]);
            for(int pass=0;pass<3;++pass){auto smoothed=conservative;
                for(int i=bestStart;i<bestStart+bestCount;++i){double sum=0,weight=0;
                    for(int j=-3;j<=3;++j){const double w=4-std::abs(j);sum+=conservative[std::clamp(i+j,bestStart,bestStart+bestCount-1)]*w;weight+=w;}
                    smoothed[i]=sum/weight;
                }conservative=smoothed;
            }radii=conservative;
        }
        double radius=0;for(int side=0;side<2;++side)for(int i=bestStart;i<bestStart+bestCount;++i)radius=std::max(radius,radii_[side][i]);
        const auto b=a_+axis_*length_;const double margin=radius*2;bounds_=QRectF(QPointF(std::min(a_.x(),b.x())-margin,std::min(a_.y(),b.y())-margin),QPointF(std::max(a_.x(),b.x())+margin,std::max(a_.y(),b.y())+margin));bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    QPointF base()const{return a_;}
    QPointF chin()const{return a_+axis_*length_;}
    QPointF displacement(QPointF q)const{
        const auto d=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-a_;const double t=QPointF::dotProduct(d,axis_)/length_,n=QPointF::dotProduct(d,normal_);
        if(t<=start_ || t>=end_ || width_==0)return {};
        const double u=std::clamp((t-.15)/.7*(count-1),0.,double(count-1));const int i=std::min(count-2,int(u));const auto& radii=radii_[n<0?0:1];const double radius=radii[i]+(radii[i+1]-radii[i])*(u-i);
        if(radius<=0 || std::abs(n)>=radius*2)return {};
        const double envelope=std::pow(std::sin(3.141592653589793*(t-start_)/(end_-start_)),2);
        const auto forward=[&](double s){const double x=std::clamp((std::abs(s)-radius)/radius,0.,1.);const double protection=hair_weight(a_+axis_*(length_*t)+normal_*s,radius);return s*(1+.12*width_*envelope*(1-x*x*(3-2*x))*protection);};
        double lo=-radius*2,hi=radius*2;for(int j=0;j<24;++j){const double mid=(lo+hi)*.5;if(forward(mid)<n)lo=mid;else hi=mid;}
        const auto shift=normal_*((lo+hi)*.5-n);return {shift.x()/(size_.width()-1),shift.y()/(size_.height()-1)};
    }
private:
    double hair_weight(QPointF p,double radius)const{
        if(hairDistance_.empty())return 1;
        const double x=std::clamp(p.x()/(size_.width()-1)*hairSize_.width(),0.,double(hairSize_.width()-1)),y=std::clamp(p.y()/(size_.height()-1)*hairSize_.height(),0.,double(hairSize_.height()-1));
        const int ix=int(x),iy=int(y),jx=std::min(ix+1,hairSize_.width()-1),jy=std::min(iy+1,hairSize_.height()-1);
        const auto at=[&](int xx,int yy){return hairDistance_[std::size_t(yy)*hairSize_.width()+xx];};
        const double distance=(at(ix,iy)*(1-x+ix)+at(jx,iy)*(x-ix))*(1-y+iy)+(at(ix,jy)*(1-x+ix)+at(jx,jy)*(x-ix))*(y-iy);
        const double u=std::clamp((distance-hairMargin_)/std::max(1.,radius*.6),0.,1.);return u*u*(3-2*u);
    }
    QSize hairSize_;std::vector<float> hairDistance_;double hairMargin_{};
    static constexpr int count=31;QSize size_;QPointF a_,axis_,normal_;double length_{},width_{},start_{},end_{};std::array<std::array<double,count>,2> radii_{};QRectF bounds_;
};
}
