#pragma once
#include "huelith/editing/project.h"
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <array>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// A monotone width map in a bone's own pixel frame. Endpoints and axial
// positions remain fixed, including bent limbs represented by separate bones.
class PortraitLimb {
public:
    PortraitLimb(QSize size,QPointF start,QPointF end,const QImage& body,double amount,double maximumRadiusRatio=.45,double activeStart=0.,double activeEnd=1.,bool checkContourContinuity=false):size_(size),amount_(amount),activeStart_(activeStart),activeEnd_(activeEnd){
        a_={start.x()*(size.width()-1),start.y()*(size.height()-1)};
        const QPointF b(end.x()*(size.width()-1),end.y()*(size.height()-1));
        const auto delta=b-a_;length_=std::hypot(delta.x(),delta.y());
        if(length_<12 || body.isNull())throw EditError("肢段定位或身体范围不足，请修正定位后再调整粗细");
        axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        const auto inside=[&](QPointF p){
            const int x=int(p.x()/(size.width()-1)*body.width()),y=int(p.y()/(size.height()-1)*body.height());
            return x>=0 && y>=0 && x<body.width() && y<body.height() && body.constScanLine(y)[x]>127;
        };
        for(int i=0;i<5;++i){
            if(checkContourContinuity){
                // Pose joints can lie near the silhouette edge. Fit a local
                // cross-section to the mask without translating bone endpoints.
                const auto center=a_+axis_*(length_*(.25+i*.125));
                const double limit=length_*maximumRadiusRatio,step=std::max(.5,length_/200.);
                double bestDistance=1e30,bestLo=0,bestHi=0,begin=-limit;bool inRun=false;
                for(double d=-limit;d<=limit+step;d+=step){
                    const bool hit=d<=limit && inside(center+normal_*d);
                    if(hit && !inRun){begin=d;inRun=true;}
                    if(!hit && inRun){
                        const double runEnd=d-step,distance=begin>0?begin:(runEnd<0?-runEnd:0.);
                        if(distance<bestDistance){bestDistance=distance;bestLo=begin;bestHi=runEnd;}
                        inRun=false;
                    }
                }
                if(bestDistance>length_*.12 || bestLo<=-limit+step || bestHi>=limit-step || bestHi-bestLo<4)
                    throw EditError("手臂轮廓不足或与其他部位贴合，请修正范围或使用手动液化");
                centers_[i]=(bestLo+bestHi)*.5;
                radii_[0][i]=radii_[1][i]=(bestHi-bestLo)*.5;
                maxRadius_=std::max(maxRadius_,radii_[0][i]+std::abs(centers_[i]));
                continue;
            }
            for(int side=0;side<2;++side){
            const auto center=a_+axis_*(length_*(.25+i*.125));
            if(!inside(center))throw EditError("肢段中线不在人物范围内，请修正关键点");
            double radius=0;const double step=std::max(1.,length_/100.);
            for(double d=step;d<length_*maximumRadiusRatio;d+=step)if(!inside(center+normal_*(side==0?-d:d))){radius=d-step*.5;break;}
            if(radius<2)throw EditError("肢体边界被遮挡、贴近躯干或定位不足，请使用手动液化或修正范围");
            radii_[side][i]=radius;maxRadius_=std::max(maxRadius_,radius);
            }
        }
        if(checkContourContinuity)for(int side=0;side<2;++side)for(int i=1;i<5;++i){
            const double smaller=std::min(radii_[side][i-1],radii_[side][i]);
            if(std::max(radii_[side][i-1],radii_[side][i])>smaller*2.)
                throw EditError("手臂轮廓与其他部位贴合或突然中断，无法可靠区分粗细；请修正范围或使用手动液化");
        }
        const double r=maxRadius_*2;
        bounds_=QRectF(QPointF(std::min(a_.x(),b.x())-r,std::min(a_.y(),b.y())-r),QPointF(std::max(a_.x(),b.x())+r,std::max(a_.y(),b.y())+r));
        bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    double maximum_radius()const{return maxRadius_;}
    void protect_segment(QPointF start,QPointF end,double radius=0){
        protectedSegments_.push_back({{start.x()*(size_.width()-1),start.y()*(size_.height()-1)},
                                     {end.x()*(size_.width()-1),end.y()*(size_.height()-1)},
                                     std::max(2.,(radius>0?radius:maxRadius_)*1.25)});
    }
    QPointF displacement(QPointF q)const{
        const auto d=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-a_;
        const double t=QPointF::dotProduct(d,axis_)/length_;
        if(t<=activeStart_ || t>=activeEnd_)return {};
        const double position=std::clamp((t-.25)*8.,0.,4.);
        const int i=std::min(3,int(position));
        const double center=centers_[i]+(centers_[i+1]-centers_[i])*(position-i);
        const double n=QPointF::dotProduct(d,normal_)-center;
        const auto& samples=radii_[n<0?0:1];const double radius=samples[i]+(samples[i+1]-samples[i])*(position-i);
        if(std::abs(n)>=radius*2)return {};
        const double envelope=std::pow(std::sin(3.141592653589793*(t-activeStart_)/(activeEnd_-activeStart_)),2);
        const auto forward=[&](double s){const double u=std::clamp((std::abs(s)-radius)/radius,0.,1.);
            return s*(1-.16*amount_*envelope*(1-u*u*(3-2*u)));};
        double lo=-radius*2,hi=radius*2;
        for(int iteration=0;iteration<18;++iteration){const double mid=(lo+hi)*.5;if(forward(mid)<n)lo=mid;else hi=mid;}
        double weight=1.;
        for(const auto& protection:protectedSegments_){
            const auto segment=protection.end-protection.start;
            const auto pixel=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1));
            const double along=std::clamp(QPointF::dotProduct(pixel-protection.start,segment)/std::max(1.,QPointF::dotProduct(segment,segment)),0.,1.);
            const auto delta=pixel-(protection.start+segment*along);
            const double protectedRadius=protection.radius;
            const double u=std::clamp((std::hypot(delta.x(),delta.y())-protectedRadius)/protectedRadius,0.,1.);
            weight=std::min(weight,u*u*(3-2*u));
        }
        const auto offset=normal_*(((lo+hi)*.5-n)*weight);
        return {offset.x()/(size_.width()-1),offset.y()/(size_.height()-1)};
    }
private:
    QSize size_;QPointF a_,axis_,normal_;double length_{},amount_{},activeStart_{},activeEnd_{1},maxRadius_{};
    std::array<std::array<double,5>,2> radii_{};std::array<double,5> centers_{};QRectF bounds_;
    struct ProtectedSegment{QPointF start,end;double radius;};
    std::vector<ProtectedSegment> protectedSegments_;
};
}
