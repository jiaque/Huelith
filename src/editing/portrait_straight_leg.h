#pragma once
#include "huelith/editing/project.h"
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <algorithm>
#include <array>
#include <cmath>

namespace huelith::editing {
// Translate cross-sections towards the hip/ankle axis, rather than thinning
// the leg. Hip, ankle, foot and a rigid neighbourhood of the knee are retained.
class PortraitStraightLeg {
public:
    PortraitStraightLeg(QSize size,const QJsonObject& person,int side,double amount):size_(size),amount_(std::clamp(amount,0.,1.)) {
        const auto pose=person["settings"].toObject()["pose"].toArray();
        if(side<0 || side>1 || pose.size()!=33 || person["bodyAmbiguous"].toBool())throw EditError("腿部关联不足，无法可靠直腿");
        const auto point=[&](int i){const auto q=pose[i].toArray();if(q.size()!=3 || q[2].toDouble()<.6)throw EditError("髋、膝、踝定位不足，请修正定位后再直腿");return QPointF(q[0].toDouble()*(size.width()-1),q[1].toDouble()*(size.height()-1));};
        a_=point(23+side);const auto knee=point(25+side),ankle=point(27+side),delta=ankle-a_;
        own_={a_,knee,ankle};other_={point(24-side),point(26-side),point(28-side)};
        length_=std::hypot(delta.x(),delta.y());if(length_<40)throw EditError("腿部尺寸不足，无法可靠直腿");
        axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        kneeT_=QPointF::dotProduct(knee-a_,axis_)/length_;offset_=QPointF::dotProduct(knee-a_,normal_);
        if(kneeT_<.3 || kneeT_>.7 || std::abs(offset_)>length_*.12)throw EditError("当前为明显屈腿或透视姿态，不自动直腿");
        const auto shoulder=(point(11)+point(12))*.5,hips=(point(23)+point(24))*.5,torso=hips-shoulder;
        if(QPointF::dotProduct(torso,axis_)<std::hypot(torso.x(),torso.y())*.55)throw EditError("坐姿或明显屈髋姿态不自动直腿");
        const auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(body.isNull())throw EditError("缺少身体范围，不能保证直腿轮廓正确");
        const auto inside=[&](QPointF q){const double u=q.x()/(size.width()-1),v=q.y()/(size.height()-1);return u>=0 && u<1 && v>=0 && v<1 && body.constScanLine(int(v*body.height()))[int(u*body.width())]>160 && distance(q,own_)<=distance(q,other_);};
        double maximum=0,minimum=length_;
        for(int i=0;i<count;++i){const double t=start+(end-start)*i/(count-1),c=center(t);const auto p=a_+axis_*(t*length_)+normal_*c;
            if(!inside(p))throw EditError("腿部骨架不在身体范围内，请检查定位");
            double radius=0,totalWidth=0;for(double sign:{-1.,1.}){double edge=0;const double step=std::max(.5,length_/300.);
                for(double r=step;r<length_*.22;r+=step)if(!inside(p+normal_*(sign*r))){edge=r-step*.5;break;}
                if(edge<=0)throw EditError("双腿贴合、遮挡或轮廓不完整，不能自动直腿");radius=std::max(radius,edge);totalWidth+=edge;}
            if(totalWidth<4)throw EditError("腿部轮廓尺寸不足，请检查身体范围");
            radii_[i]=radius;maximum=std::max(maximum,radius);minimum=std::min(minimum,radius);
        }
        correction_=std::clamp(offset_,-std::min(length_*.05,minimum*.4),std::min(length_*.05,minimum*.4));
        // The shared-boundary ramp has twice the lateral distance gradient.
        // Bound its translation so the forward map remains monotone.
        if(distance(knee,other_)<maximum*4)
            correction_=std::clamp(correction_,-minimum*.2,minimum*.2);
        // Test whole segments of the opposite leg, not just its knee/ankle.
        const auto otherHip=point(24-side),otherKnee=point(26-side),otherAnkle=point(28-side);
        for(int segment=0;segment<2;++segment){const auto first=segment?otherKnee:otherHip,last=segment?otherAnkle:otherKnee;
            for(int j=0;j<=40;++j){const auto d=first+(last-first)*(j/40.)-a_;const double t=QPointF::dotProduct(d,axis_)/length_,n=QPointF::dotProduct(d,normal_);
                if(t>start && t<end && std::abs(n-center(t))<2)throw EditError("双腿交叉或骨架重叠，请使用手动液化");}}
        const double margin=maximum*2+std::abs(offset_);
        bounds_=QRectF(QPointF(std::min(a_.x(),ankle.x())-margin,std::min(a_.y(),ankle.y())-margin),QPointF(std::max(a_.x(),ankle.x())+margin,std::max(a_.y(),ankle.y())+margin));
        bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    QPointF displacement(QPointF q)const {
        const auto d=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-a_;
        const double t=QPointF::dotProduct(d,axis_)/length_,n=QPointF::dotProduct(d,normal_);
        if(t<=start || t>=end)return {};
        // Flat maximum around the joint translates the knee as a small island;
        // cubic ramps connect it to fixed hip/ankle regions without an angle kink.
        const auto smooth=[](double u){u=std::clamp(u,0.,1.);return u*u*(3-2*u);};
        const double envelope=t<kneeT_?smooth((t-start)/(kneeT_-.06-start)):smooth((end-t)/(end-kneeT_-.06));
        const double c=center(t),r=radius(t);
        // Keep the neighbouring leg and the shared seam fixed. A smooth
        // ownership transition avoids a discontinuity in touching silhouettes.
        const double shift=correction_*amount_*envelope;
        if(std::abs(shift)<1e-12 || std::abs(n-c)>=r*2)return {};
        const auto forward=[&](double s){
            const auto pixel=a_+axis_*(t*length_)+normal_*s;
            const double ownership=smooth((distance(pixel,other_)-distance(pixel,own_))/std::max(2.,r));
            return s-shift*ownership*(1-smooth((std::abs(s-c)-r)/r));
        };
        double lo=c-r*2,hi=c+r*2;for(int i=0;i<24;++i){const double mid=(lo+hi)*.5;if(forward(mid)<n)lo=mid;else hi=mid;}
        const auto change=normal_*((lo+hi)*.5-n);return {change.x()/(size_.width()-1),change.y()/(size_.height()-1)};
    }
private:
    static double distance(QPointF q,const std::array<QPointF,3>& chain){
        double result=1e30;
        for(int i=0;i<2;++i){const auto d=chain[i+1]-chain[i];const double t=std::clamp(QPointF::dotProduct(q-chain[i],d)/std::max(1.,QPointF::dotProduct(d,d)),0.,1.);const auto v=q-chain[i]-d*t;result=std::min(result,std::hypot(v.x(),v.y()));}
        return result;
    }
    double center(double t)const{return offset_*(t<kneeT_?t/kneeT_:(1-t)/(1-kneeT_));}
    double radius(double t)const{const double u=std::clamp((t-start)/(end-start)*(count-1),0.,double(count-1));const int i=std::min(count-2,int(u));return radii_[i]+(radii_[i+1]-radii_[i])*(u-i);}
    static constexpr int count=13;static constexpr double start=.20,end=.80;
    QSize size_;QPointF a_,axis_,normal_;double length_{},kneeT_{},offset_{},correction_{},amount_{};QRectF bounds_;std::array<double,count> radii_{};std::array<QPointF,3> own_{},other_{};
};
}
