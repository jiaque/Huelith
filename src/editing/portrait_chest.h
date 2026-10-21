#pragma once
#include "huelith/editing/project.h"
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>
namespace huelith::editing {
// A user-confirmed visible chest silhouette, not an inferred breast centre
// or an isotropic inflation. Only the selected outer contour moves.
class PortraitChest {
public:
    PortraitChest(QSize size,const QJsonObject& person,double amount,int selectedSide=0):size_(size),amount_(std::clamp(amount,-1.,1.)){
        const auto settings=person["settings"].toObject();const auto pose=settings["pose"].toArray(),path=settings["chestContourPath"].toArray();
        auto target=path.size()==3?path[1].toArray():settings["chestContour"].toArray();
        const bool automatic=target.size()!=2;
        if(person["bodyAmbiguous"].toBool() || pose.size()!=33)throw EditError("人物或肩髋定位不足，不能调整胸部轮廓");
        const auto point=[&](int i){const auto p=pose[i].toArray();if(p.size()<3 || p[2].toDouble()<.25)throw EditError("肩髋或手臂定位不足，请修正定位");return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
        const auto left=point(11),right=point(12);origin_=(left+right)*.5;const auto delta=(point(23)+point(24))*.5-origin_;length_=std::hypot(delta.x(),delta.y());
        if(length_<30)throw EditError("可见躯干尺寸不足");axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        if(automatic){const auto c=origin_+axis_*(length_*.28);const auto sidePoint=selectedSide==0?left:right;const double sign=QPointF::dotProduct(sidePoint-origin_,normal_)<0?-1:1;const auto q=c+normal_*(sign*QLineF(left,right).length()*.40);target=QJsonArray{q.x()/(size.width()-1),q.y()/(size.height()-1)};}
        const QPointF selected(target[0].toDouble()*(size.width()-1),target[1].toDouble()*(size.height()-1));const auto d=selected-origin_;
        center_=QPointF::dotProduct(d,axis_)/length_;const double n=QPointF::dotProduct(d,normal_);sign_=n<0?-1:1;
        if(center_<.12 || center_>.45 || std::abs(n)<length_*.08)throw EditError("请点选肩线下方、腰部上方的胸部外轮廓，不要点胸口中线");
        start_=std::max(.08,center_-.12);end_=std::min(.50,center_+.12);
        std::array<double,3> pathT{},pathN{};
        if(path.size()==3){
            for(int i=0;i<3;++i){const auto q=path[i].toArray();if(q.size()!=2)throw EditError("胸部轮廓需要上端、调整处、下端三个点");const auto v=QPointF(q[0].toDouble()*(size.width()-1),q[1].toDouble()*(size.height()-1))-origin_;pathT[i]=QPointF::dotProduct(v,axis_)/length_;pathN[i]=QPointF::dotProduct(v,normal_)*sign_;
                if(pathN[i]<length_*.08)throw EditError("三个点应位于同一侧可见胸部外轮廓");
            }
            if(pathT[0]<.05 || pathT[2]>.55 || pathT[1]-pathT[0]<.025 || pathT[2]-pathT[1]<.025)throw EditError("请沿胸部轮廓从上到下定位三个点，避开肩部和腰部");
            start_=pathT[0];end_=pathT[2];
        }
        const auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(body.isNull())throw EditError("缺少人物轮廓，请重新识别");
        const auto inside=[&](QPointF p){const int x=int(p.x()/(size.width()-1)*body.width()),y=int(p.y()/(size.height()-1)*body.height());return x>=0 && y>=0 && x<body.width() && y<body.height() && body.constScanLine(y)[x]>180;};
        const double shoulder=QLineF(left,right).length(),step=std::max(.5,length_/300.);double maximum=0;armRadius_=shoulder*.12;
        for(int i=0;i<count;++i){const auto c=origin_+axis_*(length_*(start_+(end_-start_)*i/(count-1)));
            if(!inside(c))throw EditError("胸部中线不在人物范围内，请修正定位");
            double radius=0;
            if(path.size()==3){const double t=start_+(end_-start_)*i/(count-1);const int segment=t<center_?0:1;const double u=std::clamp((t-pathT[segment])/(pathT[segment+1]-pathT[segment]),0.,1.);radius=pathN[segment]+(pathN[segment+1]-pathN[segment])*u;}
            else for(double r=step;r<length_;r+=step)if(!inside(c+normal_*(sign_*r))){radius=r-step*.5;break;}
            if(radius<length_*.1)throw EditError("胸部轮廓不可见，请修正位置");
            radius=std::min(radius,shoulder*.65);
            radii_[i]=radius;maximum=std::max(maximum,radius);
        }
        const double pickedRadius=radius_at(center_);
        if(!automatic && std::abs(std::abs(n)-pickedRadius)>std::max(5.,pickedRadius*.15))throw EditError("点选位置不在可见胸部外轮廓上");
        // A connected silhouette can be an arm rather than the torso. Require
        // clearance from the full arm segments, not merely wrist endpoints.
        for(int side=0;side<2;++side){const auto shoulderPoint=point(11+side);if(QPointF::dotProduct(shoulderPoint-origin_,normal_)*sign_<0)continue;
            for(int segment=0;segment<2;++segment){const auto a=point(11+side+segment*2),b=point(13+side+segment*2);arms_.push_back({a,b});

            }
        }
        for(int pass=0;pass<3;++pass){const auto previous=radii_;for(int i=1;i<count-1;++i)radii_[i]=(previous[i-1]+2*previous[i]+previous[i+1])*.25;}
        const auto a=origin_+axis_*(length_*start_),b=origin_+axis_*(length_*end_);const double margin=maximum*1.6;
        const QRectF bounds(QPointF(std::min(a.x(),b.x())-margin,std::min(a.y(),b.y())-margin),QPointF(std::max(a.x(),b.x())+margin,std::max(a.y(),b.y())+margin));bounds_={bounds.x()/(size.width()-1),bounds.y()/(size.height()-1),bounds.width()/(size.width()-1),bounds.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    QPointF displacement(QPointF q)const{
        const auto p=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-origin_;const double t=QPointF::dotProduct(p,axis_)/length_,n=QPointF::dotProduct(p,normal_)*sign_;
        if(t<=start_ || t>=end_ || n<=0)return {};
        const double radius=radius_at(t);if(n<radius*.4 || n>radius*1.6)return {};
        const double distance=t<=center_?(t-start_)/(center_-start_):(end_-t)/(end_-center_);
        const double envelope=std::pow(std::sin(1.5707963267948966*distance),2);
        const auto smooth=[](double u){u=std::clamp(u,0.,1.);return u*u*(3-2*u);};
        const auto forward=[&](double s){const double r=s/radius,weight=r<=1?smooth((r-.4)/.6):1-smooth((r-1)/.6);
            const auto point=origin_+axis_*(length_*t)+normal_*(sign_*s);double protection=1;
            for(const auto& arm:arms_){const auto v=arm[1]-arm[0];const double u=std::clamp(QPointF::dotProduct(point-arm[0],v)/std::max(1.,QPointF::dotProduct(v,v)),0.,1.);const double distance=QLineF(point,arm[0]+v*u).length();protection=std::min(protection,smooth((distance-armRadius_)/std::max(1.,radius*.4)));}
            return s+radius*.08*amount_*envelope*weight*protection;
        };
        double lo=radius*.4,hi=radius*1.6;for(int i=0;i<24;++i){const double mid=(lo+hi)*.5;if(forward(mid)<n)lo=mid;else hi=mid;}
        const auto offset=normal_*(sign_*((lo+hi)*.5-n));return {offset.x()/(size_.width()-1),offset.y()/(size_.height()-1)};
    }
private:
    double radius_at(double t)const{const double u=std::clamp((t-start_)/(end_-start_)*(count-1),0.,double(count-1));const int i=std::min(count-2,int(u));return radii_[i]+(radii_[i+1]-radii_[i])*(u-i);}
    static constexpr int count=17;QSize size_;QPointF origin_,axis_,normal_;double length_{},amount_{},center_{},start_{},end_{},sign_{},armRadius_{};std::array<double,count> radii_{};QRectF bounds_;std::vector<std::array<QPointF,2>> arms_;
};
}
