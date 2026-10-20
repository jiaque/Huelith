#pragma once
#include "portrait_straight_leg.h"

namespace huelith::editing {
// Axial length redistribution with a rigid knee island and a translated foot.
// The inverse is solved along the axis; transverse image coordinates never scale.
class PortraitLegLength {
public:
    PortraitLegLength(QSize size,const QJsonObject& person,int side,double amount,bool paired=false):size_(size),amount_(std::clamp(amount,-1.,1.)) {
        // Validate both silhouettes before building a shared field for equal amounts.
        try{PortraitStraightLeg validation(size,person,side,0);}catch(const EditError& e){throw EditError(QString::fromUtf8(e.what()).replace("直腿","调整腿长"));}
        if(paired)try{PortraitStraightLeg validation(size,person,1-side,0);}catch(const EditError& e){throw EditError(QString::fromUtf8(e.what()).replace("直腿","调整腿长"));}
        const auto pose=person["settings"].toObject()["pose"].toArray();
        const auto point=[&](int i){const auto p=pose[i].toArray();if(p.size()!=3 || p[2].toDouble()<.6)throw EditError("脚踝、脚跟或脚尖定位不足，无法保持脚部形状");return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
        const auto joint=[&](int i){return paired?(point(i)+point(i+1))*.5:point(i+side);};
        a_=joint(23);const auto knee=joint(25),ankle=joint(27),delta=ankle-a_;length_=std::hypot(delta.x(),delta.y());axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        kneeT_=QPointF::dotProduct(knee-a_,axis_)/length_;const double bend=QPointF::dotProduct(knee-a_,normal_);
        const auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const auto inside=[&](QPointF q){const double x=q.x()/(size.width()-1),y=q.y()/(size.height()-1);return x>=0 && x<1 && y>=0 && y<1 && body.constScanLine(int(y*body.height()))[int(x*body.width())]>160;};
        radius_=std::abs(bend);
        for(int limb=paired?0:side;limb<=(paired?1:side);++limb)for(int i=0;i<9;++i){
            const double t=.25+i*.0625;
            const auto hip=point(23+limb),k=point(25+limb),foot=point(27+limb);
            const auto p=t<kneeT_?hip+(k-hip)*(t/kneeT_):k+(foot-k)*((t-kneeT_)/(1-kneeT_));
            const double c=QPointF::dotProduct(p-a_,normal_);
            for(double sign:{-1.,1.})for(double r=1;r<length_*.5;r+=1)if(!inside(p+normal_*(sign*r))){radius_=std::max(radius_,std::abs(c+sign*r));break;}}
        footEnd_=1.;
        for(int limb=paired?0:side;limb<=(paired?1:side);++limb)for(int index:{27+limb,29+limb,31+limb}){const auto p=point(index),d=p-a_;const double t=QPointF::dotProduct(d,axis_)/length_;
            if(t<.85 || t>1.35)throw EditError("脚部姿态不适合轴向伸展，请使用手动增高");
            footEnd_=std::max(footEnd_,t+.04);radius_=std::max(radius_,std::abs(QPointF::dotProduct(d,normal_))+length_*.025);
            const auto moved=p+axis_*(length_*.06*amount_);if(moved.x()<1 || moved.y()<1 || moved.x()>size.width()-2 || moved.y()>size.height()-2)throw EditError("调整后脚部会超出照片，请先扩展画布或减小腿长");
        }
        radius_+=length_*.015;
        // The whole opposite leg and foot must remain outside the moving strip.
        const std::array<int,5> other{24-side,26-side,28-side,30-side,32-side};
        if(!paired)for(int segment=0;segment<4;++segment){const auto first=point(other[segment]),last=point(other[segment+1]);for(int j=0;j<=30;++j){const auto d=first+(last-first)*(j/30.)-a_;const double t=QPointF::dotProduct(d,axis_)/length_,n=QPointF::dotProduct(d,normal_);
                if(t>.15 && t<footEnd_+.3 && std::abs(n)<radius_*2)throw EditError("双腿或脚部作用范围重叠，请使用手动增高");}}
        const auto end=a_+axis_*(length_*(footEnd_+.3));const double margin=radius_*2;
        bounds_=QRectF(QPointF(std::min(a_.x(),end.x())-margin,std::min(a_.y(),end.y())-margin),QPointF(std::max(a_.x(),end.x())+margin,std::max(a_.y(),end.y())+margin));bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    double maximum_displacement()const{return length_*.06*std::abs(amount_);}
    QPointF displacement(QPointF q)const {
        const auto d=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-a_;const double t=QPointF::dotProduct(d,axis_)/length_,n=QPointF::dotProduct(d,normal_);
        if(t<=.15 || t>=footEnd_+.3 || std::abs(n)>=radius_*2 || amount_==0)return {};
        const double weight=1-smooth((std::abs(n)-radius_)/radius_);
        const auto envelope=[&](double s){if(s<kneeT_-.05)return kneeT_*smooth((s-.15)/(kneeT_-.05-.15));if(s<kneeT_+.05)return kneeT_;if(s<.85)return kneeT_+(1-kneeT_)*smooth((s-kneeT_-.05)/(.85-kneeT_-.05));if(s<footEnd_)return 1.;return 1-smooth((s-footEnd_)/.3);};
        const auto forward=[&](double s){return s+.06*amount_*weight*envelope(s);};
        double lo=.15,hi=footEnd_+.3;for(int i=0;i<26;++i){const double mid=(lo+hi)*.5;if(forward(mid)<t)lo=mid;else hi=mid;}
        const auto change=axis_*(((lo+hi)*.5-t)*length_);return {change.x()/(size_.width()-1),change.y()/(size_.height()-1)};
    }
private:
    static double smooth(double u){u=std::clamp(u,0.,1.);return u*u*(3-2*u);}
    QSize size_;QPointF a_,axis_,normal_;double length_{},kneeT_{},radius_{},footEnd_{},amount_{};QRectF bounds_;
};
}
