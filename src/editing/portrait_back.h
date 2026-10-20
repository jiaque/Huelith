#pragma once
#include "huelith/editing/project.h"
#include <QImage>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QRectF>
#include <array>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// Reduce the visible posterior upper-back convexity in profile photographs.
// This is a cosmetic contour correction, not a reconstruction of the spine.
// The chest, head, shoulder/hip joints and longitudinal coordinates stay fixed.
class PortraitBack {
public:
    PortraitBack(QSize size,const QJsonObject& person,double amount):size_(size),amount_(std::clamp(amount,0.,1.)){
        const auto located=person["settings"].toObject()["backContour"].toArray();
        if(!located.isEmpty()){
            if(located.size()!=3)throw EditError("请依次定位上背轮廓的上端、最凸处、下端");
            std::array<QPointF,3> points;
            for(int i=0;i<3;++i){const auto q=located[i].toArray();if(q.size()!=2 || !q[0].isDouble() || !q[1].isDouble())throw EditError("上背定位数据无效");
                const double x=q[0].toDouble(),y=q[1].toDouble();if(!std::isfinite(x) || !std::isfinite(y) || x<0 || x>1 || y<0 || y>1)throw EditError("上背定位须位于照片内");points[i]={x*(size.width()-1),y*(size.height()-1)};}
            a_=points[0];const auto delta=points[2]-a_;length_=std::hypot(delta.x(),delta.y());if(length_<24)throw EditError("上背定位范围太小，请重新定位");axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
            peak_=QPointF::dotProduct(points[1]-a_,axis_)/length_;const double side=QPointF::dotProduct(points[1]-a_,normal_);sag_=std::abs(side);rearSign_=side<0?-1.:1.;
            if(peak_<.15 || peak_>.85 || sag_<length_*.02 || sag_>length_*.45)throw EditError("最凸处应位于上下端之间且靠近可见背部轮廓，请重新定位");
            manual_=true;const double margin=length_*.30+sag_;const auto b=points[2];bounds_=QRectF(QPointF(std::min(a_.x(),b.x())-margin,std::min(a_.y(),b.y())-margin),QPointF(std::max(a_.x(),b.x())+margin,std::max(a_.y(),b.y())+margin));bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};return;
        }
        const auto pose=person["settings"].toObject()["pose"].toArray();
        if(person["bodyAmbiguous"].toBool() || pose.size()!=33)throw EditError("无法自动定位上背，请点击“定位上背轮廓”后调整");
        const auto reliable=[&](int i){const auto p=pose[i].toArray();return p.size()==3 && p[2].toDouble()>.5;};
        const auto pt=[&](int i){const auto p=pose[i].toArray();return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
        for(int i:{11,12,23,24,25,26})if(!reliable(i))throw EditError("肩髋或膝部定位不足，请点击“定位上背轮廓”后调整");
        a_=(pt(11)+pt(12))*.5;const auto hips=(pt(23)+pt(24))*.5;const auto delta=hips-a_;length_=std::hypot(delta.x(),delta.y());if(length_<24)throw EditError("躯干尺寸不足，无法可靠校正背部");axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        int standing=0;for(int side=0;side<2;++side){const auto thigh=pt(25+side)-pt(23+side);const double span=std::hypot(thigh.x(),thigh.y());if(span>12 && QPointF::dotProduct(thigh,axis_)/span>.45 && QPointF::dotProduct(thigh,axis_)>length_*.30)++standing;}
        if(standing<2)throw EditError("当前可能为坐姿、蹲姿或屈腿姿态，不自动改善驼背；可点击“定位上背轮廓”手动确认范围");
        const auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);if(body.isNull())throw EditError("缺少独立身体范围，无法提取背部曲线");
        const auto hair=QImage::fromData(QByteArray::fromBase64(person["settings"].toObject()["automaticMasks"].toObject()["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const auto inside=[&](QPointF q){const double u=q.x()/(size.width()-1),v=q.y()/(size.height()-1);const int x=int(u*body.width()),y=int(v*body.height());if(u<0 || v<0 || u>=1 || v>=1 || body.constScanLine(y)[x]<=160)return false;
            // Long hair belongs to the person instance but not to the torso.
            // Use the independent semantic mask before measuring cross-sections.
            return hair.isNull() || hair.constScanLine(int(v*hair.height()))[int(u*hair.width())]<96;};
        // Only transverse spread indicates a frontal torso; shoulder height
        // differences must not be mistaken for facing the camera.
        const double shoulderSpan=std::abs(QPointF::dotProduct(pt(11)-pt(12),normal_));
        const double hipSpan=std::abs(QPointF::dotProduct(pt(23)-pt(24),normal_));
        const double faceDirection=QPointF::dotProduct(pt(0)-a_,normal_);
        if(!reliable(0) || std::abs(faceDirection)<length_*.08)throw EditError("无法可靠区分前胸和后背，请使用侧面照片或手动定位上背轮廓");
        rearSign_=faceDirection>0?-1.:1.;
        const auto occlusion=QImage::fromData(QByteArray::fromBase64(person["settings"].toObject()["automaticMasks"].toObject()["sceneOcclusion"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const auto obstructed=[&](QPointF q){const double u=q.x()/(size.width()-1),v=q.y()/(size.height()-1);
            if(u<0 || v<0 || u>=1 || v>=1)return false;
            return (!occlusion.isNull() && occlusion.constScanLine(int(v*occlusion.height()))[int(u*occlusion.width())]>128)
                || (!hair.isNull() && hair.constScanLine(int(v*hair.height()))[int(u*hair.width())]>96);};
        double maxRadius=0;
        for(int i=0;i<count;++i){const double t=start+(end-start)*i/(count-1);const auto c=a_+axis_*(length_*t);if(!inside(c))throw EditError("上背中线被遮挡或不在身体范围内，请检查定位");
            const double step=std::max(.5,length_/240.);double radius=0;
            for(double r=step;r<length_*.65;r+=step)if(!inside(c+normal_*(rearSign_*r))){radius=r-step*.5;break;}
            if(radius<length_*.08)throw EditError("上背轮廓不完整、头发遮挡或身体范围不足，不能自动改善驼背");
            for(int probe=0;probe<=18;++probe)if(obstructed(c+normal_*(rearSign_*radius*(.55+.90*probe/18.))))
                throw EditError("上背调整区域有头发、背包或其他物体遮挡，不能自动推断背部轮廓；请使用保护区并手动定位可见上背");
            radii_[i]=radius;maxRadius=std::max(maxRadius,radius);
        }
        if(shoulderSpan>length_*.30 || hipSpan>length_*.30 || !reliable(0))throw EditError("改善驼背需要可见上背轮廓的侧面或斜侧面照片，不能用正面侧弯代替");
        // Smooth only the posterior contour. A straight tilted torso has no
        // convex residual, and the anterior chest is never translated with it.
        for(int pass=0;pass<3;++pass){auto next=radii_;for(int i=2;i<count-2;++i)next[i]=(radii_[i-2]+4*radii_[i-1]+6*radii_[i]+4*radii_[i+1]+radii_[i+2])/16.;radii_=next;}
        double maximum=0;
        for(int i=0;i<count;++i){const double u=double(i)/(count-1);
            const double chord=radii_.front()*(1-u)+radii_.back()*u;
            // Retain most of the natural curvature, even at maximum strength.
            correction_[i]=std::min({std::max(0.,radii_[i]-chord)*.4,length_*.025,radii_[i]*.1})*std::pow(std::sin(3.141592653589793*u),2);
            maximum=std::max(maximum,correction_[i]);
        }
        if(maximum<length_*.002)throw EditError("未检测到可靠的上背后凸，不应用改善驼背");
        // Protect complete arm segments, not just joints. A front-side arm
        // need not disable a correction confined to the posterior half.
        for(int side=0;side<2;++side)for(int segment=0;segment<2;++segment){const int first=11+side+segment*2,last=first+2;
            // Visibility confidence is not an exact position guarantee. Keep
            // uncertain arm segments out of the deformation with a growing
            // exclusion envelope, rather than treating them as reliable joints.
            const auto uncertainty=[&](int joint){const auto data=pose[joint].toArray();
                if(data.size()!=3 || !std::isfinite(data[0].toDouble()) || !std::isfinite(data[1].toDouble()) || data[2].toDouble()<.05)
                    throw EditError("手臂定位缺失，请点击“定位上背轮廓”手动确认范围");
                return reliable(joint)?0.:length_*.30*(1-std::clamp(data[2].toDouble(),0.,1.));};
            const double firstUncertainty=uncertainty(first),lastUncertainty=uncertainty(last);
            for(int j=0;j<=48;++j){const double u=j/48.;const auto d=pt(first)+(pt(last)-pt(first))*u-a_;
                const double t=QPointF::dotProduct(d,axis_)/length_,n=rearSign_*QPointF::dotProduct(d,normal_);
                const double margin=length_*.04+firstUncertainty*(1-u)+lastUncertainty*u;
                if(t+margin/length_<=start || t-margin/length_>=end)continue;
                const double r=sample(radii_,t);
                if(n+margin>r*.55 && n-margin<r*1.45)throw EditError("手臂或其定位不确定范围接近上背校正区，请使用保护区或手动定位上背轮廓");
            }
        }
        const auto b=a_+axis_*length_;const double r=maxRadius*1.8;bounds_=QRectF(QPointF(std::min(a_.x(),b.x())-r,std::min(a_.y(),b.y())-r),QPointF(std::max(a_.x(),b.x())+r,std::max(a_.y(),b.y())+r));bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};
    }
    double maximum_displacement()const{return amount_*(manual_?std::min(sag_*.40,length_*.06):*std::max_element(correction_.begin(),correction_.end()));}
    QRectF bounds()const{return bounds_;}
    QPointF displacement(QPointF q)const{
        const auto d=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-a_;const double t=QPointF::dotProduct(d,axis_)/length_,n=QPointF::dotProduct(d,normal_);if(manual_){
            if(t<=0 || t>=1)return {};const double u=t<peak_?t/peak_:(1-t)/(1-peak_);const double envelope=std::pow(std::sin(u*1.5707963267948966),2);
            const double c=sag_*envelope,band=length_*.30,shift=std::min(sag_*.40,length_*.06)*envelope*amount_,rear=rearSign_*n;
            if(std::abs(rear-c)>=band || shift<1e-12)return {};
            // Move only a compact strip around the user-confirmed posterior
            // contour. Both ends and the anterior body stay fixed.
            const auto forward=[&](double s){const double z=std::abs(s-c)/band;return s-shift*std::pow(std::max(0.,1-z*z),2);};
            double lo=c-band,hi=c+band;for(int i=0;i<24;++i){const double mid=(lo+hi)*.5;if(forward(mid)<rear)lo=mid;else hi=mid;}
            const auto offset=normal_*(rearSign_*((lo+hi)*.5-rear));return {offset.x()/(size_.width()-1),offset.y()/(size_.height()-1)};
        }
        if(t<=start || t>=end)return {};
        const double r=sample(radii_,t),shift=sample(correction_,t)*amount_,rear=rearSign_*n;
        if(rear<=r*.55 || rear>=r*1.45 || shift<1e-12)return {};
        const auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);};
        const auto forward=[&](double s){const double weight=s<r?smooth((s/r-.55)/.45):1-smooth((s/r-1)/.45);return s-shift*weight;};
        double lo=r*.55,hi=r*1.45;for(int i=0;i<24;++i){const double mid=(lo+hi)*.5;if(forward(mid)<rear)lo=mid;else hi=mid;}
        const auto offset=normal_*(rearSign_*((lo+hi)*.5-rear));return {offset.x()/(size_.width()-1),offset.y()/(size_.height()-1)};
    }
private:
    static constexpr int count=25;static constexpr double start=.04,end=.65;
    static double sample(const std::array<double,count>& values,double t){const double u=std::clamp((t-start)/(end-start)*(count-1),0.,double(count-1));const int i=std::min(count-2,int(u));return values[i]+(values[i+1]-values[i])*(u-i);}
    QSize size_;QPointF a_,axis_,normal_;bool manual_{};double peak_{},sag_{},length_{},amount_{},rearSign_{};QRectF bounds_;std::array<double,count> radii_{},correction_{};
};
}
