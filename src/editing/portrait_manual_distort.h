#pragma once
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// A compact stationary velocity field: the inner half rotates/shears, the
// outer half smoothly returns to a fixed boundary. Inverse flow is sampled
// directly, rather than adding an affine offset that can fold at the edge.
class PortraitManualDistort {
public:
    PortraitManualDistort(QSize size,const QJsonObject& person,double rotation,double shear):size_(size){
        const auto a=person["settings"].toObject()["manualDistortRegion"].toArray();
        if(a.size()!=3)throw EditError("请先在画布设置手动扭曲范围");
        for(const auto v:a)if(!v.isDouble() || !std::isfinite(v.toDouble()))throw EditError("扭曲范围坐标无效");
        center_={a[0].toDouble()*(size.width()-1),a[1].toDouble()*(size.height()-1)};
        radius_=a[2].toDouble()*std::min(size.width(),size.height());
        if(a[0].toDouble()<0 || a[0].toDouble()>1 || a[1].toDouble()<0 || a[1].toDouble()>1 || a[2].toDouble()<=0 || a[2].toDouble()>1)throw EditError("请在画布内设置有效扭曲中心及范围");
        rotation_=.5235987755982988*std::clamp(rotation,-1.,1.);
        shear_=.4*std::clamp(shear,-1.,1.);
    }
    QRectF bounds()const{return {(center_.x()-radius_)/(size_.width()-1),(center_.y()-radius_)/(size_.height()-1),2*radius_/(size_.width()-1),2*radius_/(size_.height()-1)};}
    QPointF displacement(QPointF q)const{
        const QPointF original(q.x()*(size_.width()-1)-center_.x(),q.y()*(size_.height()-1)-center_.y());
        if(std::hypot(original.x(),original.y())>=radius_)return {};
        const auto velocity=[&](QPointF p){const double t=std::clamp((std::hypot(p.x(),p.y())/radius_-.5)*2,0.,1.);const double w=1-t*t*(3-2*t);return QPointF((-rotation_+shear_)*p.y(),rotation_*p.x())*w;};
        QPointF p=original;constexpr double dt=-1./24;
        for(int i=0;i<24;++i){const auto a=velocity(p),b=velocity(p+a*(dt*.5)),c=velocity(p+b*(dt*.5)),d=velocity(p+c*dt);p+=(a+b*2+c*2+d)*(dt/6);}
        return {(p.x()-original.x())/(size_.width()-1),(p.y()-original.y())/(size_.height()-1)};
    }
private:
    QSize size_;QPointF center_;double radius_{},rotation_{},shear_{};
};
}
