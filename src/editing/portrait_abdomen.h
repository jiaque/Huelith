#pragma once
#include "huelith/editing/project.h"
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <array>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// Local lower-abdominal boundary motion. The centre strip, chest and pelvis
// are fixed; unlike waist scaling, interior clothing is not uniformly shrunk.
class PortraitAbdomen {
public:
    PortraitAbdomen(QSize size,const QJsonObject& person,double amount,double localAmount=0):size_(size),amount_(std::clamp(amount,0.,1.)),localAmount_(std::clamp(localAmount,0.,1.)){
        const auto pose=person["settings"].toObject()["pose"].toArray();
        if(person["bodyAmbiguous"].toBool() || pose.size()!=33)throw EditError("身体关联或肩髋定位不足，不能自动收腹");
        const auto reliable=[&](int i){const auto p=pose[i].toArray();return p.size()==3 && p[2].toDouble()>.5;};
        const auto pt=[&](int i){const auto p=pose[i].toArray();return QPointF(p[0].toDouble()*(size.width()-1),p[1].toDouble()*(size.height()-1));};
        for(int i:{11,12,23,24})if(!reliable(i))throw EditError("肩髋定位不足，请先修正身体定位");
        a_=(pt(11)+pt(12))*.5;const auto hips=(pt(23)+pt(24))*.5;const auto delta=hips-a_;length_=std::hypot(delta.x(),delta.y());
        if(length_<20)throw EditError("腹部尺寸不足，无法可靠收腹");axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        const double shoulder=std::abs(QPointF::dotProduct(pt(11)-pt(12),normal_)),hip=std::abs(QPointF::dotProduct(pt(23)-pt(24),normal_));
        const auto front=person["settings"].toObject()["abdomenFront"].toArray();
        if(front.size()==2){const QPointF q(front[0].toDouble()*(size.width()-1),front[1].toDouble()*(size.height()-1));const auto d=q-a_;frontT_=QPointF::dotProduct(d,axis_)/length_;frontN_=QPointF::dotProduct(d,normal_);if(frontT_<.57 || frontT_>.85 || std::abs(frontN_)<length_*.10)throw EditError("请点选下腹外轮廓，不要点击躯干中线、胸部或骨盆");frontSide_=frontN_<0?0:1;}
        if(frontSide_<0 && (shoulder<length_*.55 || hip<length_*.32))throw EditError("侧身需要先点选腹部前缘，以保持后背不动");
        if(localAmount_>0 && frontSide_<0)throw EditError("局部瘦肚子需要先点选可见的腹部前突轮廓");
        auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(body.isNull())throw EditError("缺少身体范围，无法定位腹部轮廓");
        const auto inside=[&](QPointF p){const int x=int(p.x()/(size.width()-1)*body.width()),y=int(p.y()/(size.height()-1)*body.height());return x>=0 && y>=0 && x<body.width() && y<body.height() && body.constScanLine(y)[x]>160;};
        // Clothing and a hand resting on the torso share the visible silhouette.
        // Deform that local silhouette continuously rather than rejecting the torso.
        double maximum=0;
        for(int i=0;i<9;++i){const double t=.55+i*.04375;const auto center=a_+axis_*(length_*t);if(!inside(center))throw EditError("腹部中线不在人物范围内，请检查定位");bool stop=false;for(int side=0;side<2;++side){double radius=0;const double step=std::max(1.,length_/150.);for(double r=step;r<length_;r+=step)if(!inside(center+normal_*(side==0?-r:r))){radius=r-step*.5;break;}if(radius<length_*.08){if(i<5)throw EditError("腹部边界被遮挡或范围不可靠，请修正身体范围");stop=true;break;}radius=std::min(radius,frontSide_>=0?length_*.65:std::max(shoulder,hip)*.8);radii_[side][i]=radius;maximum=std::max(maximum,radius);}if(stop){activeEnd_=.55+(i-1)*.04375;break;}}
        if(frontSide_>=0){if(frontT_>=activeEnd_-.015)throw EditError("点选位置已接近骨盆或不可靠边界，请选择更上方的可见腹部");const double position=std::clamp((frontT_-.55)/.04375,0.,8.);const int i=std::min(7,int(position));const auto& row=radii_[frontSide_];const double r=row[i]+(row[i+1]-row[i])*(position-i);if(std::abs(std::abs(frontN_)-r)>std::max(5.,r*.25))throw EditError("点选位置不在可见身体轮廓附近，请点腹部前缘");}
        const auto lo=a_+axis_*(length_*.55),hi=a_+axis_*(length_*activeEnd_);const double r=maximum*1.8;
        bounds_=QRectF(QPointF(std::min(lo.x(),hi.x())-r,std::min(lo.y(),hi.y())-r),QPointF(std::max(lo.x(),hi.x())+r,std::max(lo.y(),hi.y())+r));
        bounds_={bounds_.x()/(size.width()-1),bounds_.y()/(size.height()-1),bounds_.width()/(size.width()-1),bounds_.height()/(size.height()-1)};
    }
    QRectF bounds()const{return bounds_;}
    QPointF displacement(QPointF q)const{
        const auto d=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-a_;const double t=QPointF::dotProduct(d,axis_)/length_,n=QPointF::dotProduct(d,normal_);
        if(t<=.55 || t>=activeEnd_ || (amount_==0 && localAmount_==0))return {};
        if(frontSide_>=0 && (n<0?0:1)!=frontSide_)return {};
        const double position=std::clamp((t-.55)/.04375,0.,8.);const int i=std::min(7,int(position));const auto& row=radii_[n<0?0:1];const double radius=row[i]+(row[i+1]-row[i])*(position-i);
        if(std::abs(n)<=radius*.35 || std::abs(n)>=radius*1.8)return {};
        const auto smooth=[](double u){u=std::clamp(u,0.,1.);return u*u*(3-2*u);};
        const double envelope=std::pow(std::sin(3.141592653589793*(t-.55)/(activeEnd_-.55)),2);
        const double localDistance=std::abs(t-frontT_)/.10;
        const double localEnvelope=localDistance>=1?0:std::pow(1-localDistance*localDistance,3)*envelope;
        const double contraction=std::min(.14,.10*amount_*envelope+.12*localAmount_*localEnvelope);
        if(contraction<1e-12)return {};
        const auto forward=[&](double x){const double r=std::abs(x)/radius;const double weight=r<=1?smooth((r-.35)/.65):1-smooth((r-1)/.8);return x-std::copysign(radius*contraction*weight,x);};
        double lo=-radius*1.8,hi=radius*1.8;for(int j=0;j<22;++j){const double mid=(lo+hi)*.5;if(forward(mid)<n)lo=mid;else hi=mid;}
        const auto offset=normal_*((lo+hi)*.5-n);return {offset.x()/(size_.width()-1),offset.y()/(size_.height()-1)};
    }
private:
    QSize size_;QPointF a_,axis_,normal_;double length_{},amount_{},localAmount_{},activeEnd_{.90},frontT_{.725},frontN_{};int frontSide_{-1};QRectF bounds_;std::array<std::array<double,9>,2> radii_{};
};
}
