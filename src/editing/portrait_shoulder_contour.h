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
namespace huelith::editing {
// Flatten the outer shoulder contour while retaining the neck and sleeve
// junctions. The shoulder joint is not translated as in elevation/width.
class PortraitShoulderContour {
public:
    enum class Profile { SquareShoulder, Trapezius };
    PortraitShoulderContour(QSize size,const QJsonObject& person,int side,double amount,Profile profile=Profile::SquareShoulder):size_(size),amount_(std::clamp(amount,profile==Profile::Trapezius?-1.:0.,1.)){
        const auto settings=person["settings"].toObject();const auto pose=settings["pose"].toArray();
        if(pose.size()!=33 || person["bodyAmbiguous"].toBool())throw EditError("肩部定位不足，不能调整直角肩");
        const auto point=[&](int i){const auto p=pose[i].toArray();if(p.size()<3 || p[2].toDouble()<.6)throw EditError("肩部定位不足，请修正定位");return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
        const auto left=point(11),right=point(12);center_=(left+right)*.5;width_=QLineF(left,right).length();
        if(width_<30)throw EditError("肩部投影过短，不能调整直角肩");
        axis_=(side==0?left-right:right-left)/width_;down_={-axis_.y(),axis_.x()};if(down_.y()<0)down_=-down_;
        const auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        const auto hair=QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(body.isNull())throw EditError("缺少肩部轮廓，请重新识别人物");
        const auto sample=[&](const QImage& image,QPointF p){if(image.isNull())return 0;const int x=int(p.x()/(size.width()-1)*image.width()),y=int(p.y()/(size.height()-1)*image.height());return x>=0 && y>=0 && x<image.width() && y<image.height()?int(image.constScanLine(y)[x]):0;};
        const double step=std::max(.5,width_/300.);
        const auto landmarks=person["landmarks"].toArray();if(landmarks.size()!=66)throw EditError("缺少下巴定位，不能确定肩颈轮廓");
        const auto chinPoint=landmarks[8].toArray();const QPointF chin(chinPoint[0].toDouble()*(size.width()-1),chinPoint[1].toDouble()*(size.height()-1));
        const double top=std::max(-width_*.5,QPointF::dotProduct(chin-center_,down_)-width_*.08);
        const auto trace=[&](double x,double& edge){
            for(double y=top;y<=width_*.35;y+=step){const auto p=center_+axis_*x+down_*y;
                if(sample(body,p)>180){if(y<=top+step || sample(hair,p)>100)return false;edge=y;return true;}
            }return false;
        };
        // Search outward from the neck. Starting every photo at the same
        // fraction of shoulder span confuses the face/hair with a shoulder.
        constexpr int searchCount=65;int bestStart=0,bestCount=0,runStart=0,runCount=0;
        // Pose shoulders describe internal joints, not the outer sleeve edge.
        // Continue beyond the joint (0.5 span) to include the shoulder cap;
        // stopping there can reshape only a raised collar beside the neck.
        constexpr double searchStart=.12;
        const double searchEnd=profile==Profile::Trapezius?.5:.75;
        double previousEdge=0;
        for(int i=0;i<searchCount;++i){double edge;const double x=width_*(searchStart+(searchEnd-searchStart)*i/(searchCount-1));
            if(trace(x,edge)){
                // A separate arm/body edge below a shoulder is a new segment,
                // not a continuation to bridge into the shoulder curve.
                const double searchStep=width_*(searchEnd-searchStart)/(searchCount-1);
                if(runCount && std::abs(edge-previousEdge)>std::max(step*2,searchStep*2.5))runCount=0;
                if(runCount==0)runStart=i;
                ++runCount;previousEdge=edge;
                if(runCount>bestCount){bestStart=runStart;bestCount=runCount;}
            }else runCount=0;
        }
        if(bestCount<17)throw EditError("可见肩线过短或被遮挡，请修正范围或使用手动液化");
        start_=searchStart+(searchEnd-searchStart)*bestStart/(searchCount-1);end_=searchStart+(searchEnd-searchStart)*(bestStart+bestCount-1)/(searchCount-1);
        if(profile==Profile::Trapezius){
            // Only the neck-to-joint segment is inset. The outer shoulder cap
            // remains fixed, unlike the separate square-shoulder profile.
            end_=std::min(end_,.5);
            if(end_-start_<.09)throw EditError("可见斜方肌轮廓不足，请修正范围或使用手动液化");
        }
        for(int i=0;i<count;++i)if(!trace(width_*(start_+(end_-start_)*i/(count-1)),curve_[i]))throw EditError("肩线轮廓不连续，请修正人物范围");
        const auto raw=curve_;for(int i=1;i<count-1;++i)curve_[i]=(raw[i-1]+2*raw[i]+raw[i+1])*.25;
        for(int i=1;i<count;++i)if(std::abs(curve_[i]-curve_[i-1])>width_*.09)throw EditError("肩线轮廓不连续，请修正范围");
        for(int i=0;i<count;++i){const double t=double(i)/(count-1);const double target=curve_.back()+(curve_.front()-curve_.back())*std::pow(1-t,3);
            const auto smooth=[](double v){v=std::clamp(v,0.,1.);return v*v*(3-2*v);};
            const double join=smooth(t/.18)*smooth((1-t)/.22);
            const double insetLimit=std::min(width_*.05,width_*(end_-start_)*.12);
            shifts_[i]=profile==Profile::Trapezius?insetLimit*amount_*16*t*t*(1-t)*(1-t):(target-curve_[i])*.85*amount_*join;
            maximum_=std::max(maximum_,std::abs(shifts_[i]));
        }
        // Segmentation stair steps must not become ripples in the sleeve.
        // Smooth the displacement (not the source photograph), retaining the
        // fixed neck/sleeve endpoints and a zero derivative at both joins.
        for(int pass=0;pass<3;++pass){const auto previous=shifts_;for(int i=1;i<count-1;++i)shifts_[i]=(previous[i-1]+2*previous[i]+previous[i+1])*.25;}
        // A sufficiently broad support keeps the vertical map monotone.
        band_=std::max(width_*.16,maximum_*3.5);
        QRectF bounds;for(int i=0;i<count;++i){const auto p=center_+axis_*(width_*(start_+(end_-start_)*i/(count-1)))+down_*curve_[i];bounds=bounds.isNull()?QRectF(p,QSizeF(1,1)):bounds.united(QRectF(p,QSizeF(1,1)));}
        bounds=bounds.adjusted(-band_,-band_,band_,band_);bounds_={bounds.x()/(size.width()-1),bounds.y()/(size.height()-1),bounds.width()/(size.width()-1),bounds.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    double maximum_displacement()const{return maximum_;}
    QPointF displacement(QPointF q)const{
        const auto p=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-center_;const double t=(QPointF::dotProduct(p,axis_)/width_-start_)/(end_-start_);
        if(t<=0 || t>=1)return {};
        const double index=t*(count-1);const int i=std::min(count-2,int(index));const double f=index-i,edge=curve_[i]*(1-f)+curve_[i+1]*f;
        const double m0=i==0?0:(shifts_[i+1]-shifts_[i-1])*.5;
        const double m1=i+1==count-1?0:(shifts_[i+2]-shifts_[i])*.5;
        const double f2=f*f,f3=f2*f;
        const double shift=(2*f3-3*f2+1)*shifts_[i]+(f3-2*f2+f)*m0+(-2*f3+3*f2)*shifts_[i+1]+(f3-f2)*m1;
        const double y=QPointF::dotProduct(p,down_);if(std::abs(y-edge)>=band_)return {};
        const auto forward=[&](double v){const double r=std::min(1.,std::abs(v-edge)/band_);const double weight=std::pow(1-r*r,3);return v+shift*weight;};
        double lo=edge-band_,hi=edge+band_;for(int iteration=0;iteration<26;++iteration){const double mid=(lo+hi)*.5;if(forward(mid)<y)lo=mid;else hi=mid;}
        const auto d=down_*((lo+hi)*.5-y);return {d.x()/(size_.width()-1),d.y()/(size_.height()-1)};
    }
private:
    static constexpr int count=25;QSize size_;QPointF center_,axis_,down_;double start_{},end_{},width_{},amount_{},maximum_{},band_{};QRectF bounds_;std::array<double,count> curve_{},shifts_{};
};
}
