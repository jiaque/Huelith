#pragma once
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QLineF>
#include <array>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// Pelvic silhouette width, in the torso frame. Waist, central pelvis and
// distal legs are fixed; the visible hip contour supplies the target radius.
class PortraitPelvis {
public:
    PortraitPelvis(QSize size,const QJsonObject& person,int side,double amount):size_(size),amount_(std::clamp(amount,-1.,1.)){
        const auto pose=person["settings"].toObject()["pose"].toArray();
        if(person["bodyAmbiguous"].toBool() || pose.size()!=33)throw EditError("骨盆定位不足，请修正人物定位");
        const auto point=[&](int i){const auto p=pose[i].toArray();if(p.size()!=3 || p[2].toDouble()<.6)throw EditError("髋膝或肩部定位不足，不能自动调整臀胯");return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
        const auto left=point(23),right=point(24);origin_=(left+right)*.5;
        const auto torso=origin_-(point(11)+point(12))*.5;length_=std::hypot(torso.x(),torso.y());
        if(length_<30 || QLineF(left,right).length()<length_*.15)throw EditError("骨盆可见宽度不足，不能推测遮挡轮廓");
        axis_=torso/length_;normal_={-axis_.y(),axis_.x()};
        sign_=QPointF::dotProduct((side==0?left:right)-origin_,normal_)<0?-1:1;
        const auto knee=point(25+side),hip=side==0?left:right;
        if(QPointF::dotProduct(knee-hip,axis_)<length_*.35)throw EditError("当前坐姿或屈腿遮挡骨盆过渡，请使用手动液化");
        const auto mask=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(mask.isNull())throw EditError("缺少人物轮廓，请重新识别");
        const auto inside=[&](QPointF p){const int x=int(p.x()/(size.width()-1)*mask.width()),y=int(p.y()/(size.height()-1)*mask.height());return x>=0 && y>=0 && x<mask.width() && y<mask.height() && mask.constScanLine(y)[x]>180;};
        const double step=std::max(.5,length_/400.),hipSpan=QLineF(left,right).length();double maximum=0;std::array<bool,count> valid{};
        for(int i=0;i<count;++i){
            const double t=start_+(end_-start_)*i/(count-1);const auto center=origin_+axis_*(t*length_);
            if(!inside(center))continue;
            double radius=0;for(double r=step;r<hipSpan*1.4;r+=step)if(!inside(center+normal_*(sign_*r))){radius=r-step*.5;break;}
            if(radius<hipSpan*.3 || radius>hipSpan*1.25)continue;
            valid[i]=true;
            radii_[i]=radius;maximum=std::max(maximum,radius);
            // Hands resting at the hip must not become part of its silhouette.
            for(int arm=0;arm<2;++arm){const auto a=pose[13+arm].toArray(),b=pose[15+arm].toArray();if(a.size()!=3 || b.size()!=3 || a[2].toDouble()<.6 || b[2].toDouble()<.6)continue;
                const auto pa=point(13+arm),pb=point(15+arm),v=pb-pa,edge=center+normal_*(sign_*radius);
                const double u=std::clamp(QPointF::dotProduct(edge-pa,v)/std::max(1.,QPointF::dotProduct(v,v)),0.,1.);
                if(QLineF(edge,pa+v*u).length()<hipSpan*.2)valid[i]=false;
            }
        }
        // Trim occluded transition rows, never skip an occluded hip target.
        const int centerIndex=int(std::round(-start_/(end_-start_)*(count-1)));
        if(!valid[centerIndex])throw EditError("臀胯目标轮廓被遮挡或不完整，请使用手动液化");
        int first=centerIndex,last=centerIndex;
        while(first>0 && valid[first-1] && std::abs(radii_[first]-radii_[first-1])<=length_*.05)--first;
        while(last<count-1 && valid[last+1] && std::abs(radii_[last+1]-radii_[last])<=length_*.05)++last;
        const double stepT=(end_-start_)/(count-1),newStart=start_+first*stepT,newEnd=start_+last*stepT;
        if(newStart>-.06 || newEnd<.08)throw EditError("臀胯上下可见过渡不足，请使用手动液化");
        const auto original=radii_;
        for(int i=0;i<count;++i){const double u=first+(last-first)*double(i)/(count-1);const int j=std::min(count-2,int(u));radii_[i]=original[j]+(original[j+1]-original[j])*(u-j);}
        start_=newStart;end_=newEnd;
        for(int pass=0;pass<3;++pass){const auto previous=radii_;for(int i=1;i<count-1;++i)radii_[i]=(previous[i-1]+2*previous[i]+previous[i+1])*.25;}
        const auto a=origin_+axis_*(start_*length_),b=origin_+axis_*(end_*length_);const double margin=maximum*1.6;
        bounds_={(std::min(a.x(),b.x())-margin)/(size.width()-1),(std::min(a.y(),b.y())-margin)/(size.height()-1),(std::abs(a.x()-b.x())+2*margin)/(size.width()-1),(std::abs(a.y()-b.y())+2*margin)/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    double maximum_displacement()const{return *std::max_element(radii_.begin(),radii_.end())*.12*std::abs(amount_);}
    QPointF displacement(QPointF q)const{
        const auto p=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-origin_;
        const double t=QPointF::dotProduct(p,axis_)/length_,n=QPointF::dotProduct(p,normal_)*sign_;
        if(t<=start_ || t>=end_ || n<=0)return {};
        const double u=std::clamp((t-start_)/(end_-start_)*(count-1),0.,double(count-1));const int i=std::min(count-2,int(u));const double radius=radii_[i]+(radii_[i+1]-radii_[i])*(u-i);
        if(n<=radius*.4 || n>=radius*1.6)return {};
        const auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);};
        const double longitudinal=t<0?smooth((t-start_)/-start_):smooth((end_-t)/end_);
        const auto forward=[&](double s){const double r=s/radius;return s+radius*.12*amount_*longitudinal*(r<=1?smooth((r-.4)/.6):1-smooth((r-1)/.6));};
        double lo=radius*.4,hi=radius*1.6;for(int j=0;j<24;++j){const double mid=(lo+hi)*.5;if(forward(mid)<n)lo=mid;else hi=mid;}
        const auto offset=normal_*(sign_*((lo+hi)*.5-n));return {offset.x()/(size_.width()-1),offset.y()/(size_.height()-1)};
    }
private:
    static constexpr int count=25;QSize size_;QPointF origin_,axis_,normal_;double length_{},sign_{},amount_{};
    double start_=-.30,end_=.22;std::array<double,count> radii_{};QRectF bounds_;
};
}
