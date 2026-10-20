#pragma once
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// Integrate a compact radial velocity field. Its flow preserves radial order,
// including when the user changes the inner and outer radii independently.
class PortraitManualScale {
public:
    PortraitManualScale(QSize size,const QJsonObject& person,double amount):size_(size){
        const auto settings=person["settings"].toObject();keepAspect_=settings["manualScaleKeepAspect"].toBool(true);const auto a=settings["manualScaleRegion"].toArray();
        if(a.size()!=4)throw EditError("请先在画布设置手动缩放的中心和范围");
        for(const auto v:a)if(!v.isDouble() || !std::isfinite(v.toDouble()))throw EditError("缩放范围坐标无效");
        center_={a[0].toDouble()*(size.width()-1),a[1].toDouble()*(size.height()-1)};
        const double unit=std::min(size.width(),size.height());inner_=a[2].toDouble()*unit;outer_=a[3].toDouble()*unit;
        // UI minimums are in original pixels; preview proxies must retain the
        // same normalized region rather than reapplying those pixel minimums.
        if(center_.x()<0 || center_.y()<0 || center_.x()>size.width()-1 || center_.y()>size.height()-1 || inner_<=0 || outer_<=inner_ || outer_>unit)
            throw EditError("缩放中心须在画布内，内圈须小于外圈，外圈不超过照片短边");
        // With a fixed outer boundary, a thin transition cannot accommodate
        // the same expansion as a wide one. Bound its compression before the
        // flow is sampled into the finite deformation grid.
        time_=std::min(.35,.7*(outer_-inner_)/outer_)*std::clamp(amount,-1.,1.);
        steps_=std::clamp(int(std::ceil(8+16*std::abs(time_)*outer_/(outer_-inner_))),16,512);
    }
    QRectF bounds()const{return {(center_.x()-outer_)/(size_.width()-1),(center_.y()-outer_)/(size_.height()-1),2*outer_/(size_.width()-1),2*outer_/(size_.height()-1)};}
    QPointF displacement(QPointF q)const{
        const QPointF d(q.x()*(size_.width()-1)-center_.x(),q.y()*(size_.height()-1)-center_.y());
        const double radius=std::hypot(d.x(),d.y());if(radius<1e-10 || radius>=outer_)return {};
        if(!keepAspect_){
            const auto vx=[&](double x){const double t=std::clamp((std::hypot(x,d.y())-inner_)/(outer_-inner_),0.,1.);return x*(1-t*t*(3-2*t));};
            const double dt=-time_/steps_;double x=d.x();for(int i=0;i<steps_;++i){const double a=vx(x),b=vx(x+dt*a*.5),c=vx(x+dt*b*.5),e=vx(x+dt*c);x+=dt*(a+2*b+2*c+e)/6;}return {(x-d.x())/(size_.width()-1),0};
        }
        const auto velocity=[&](double r){const double t=std::clamp((r-inner_)/(outer_-inner_),0.,1.);return r*(1-t*t*(3-2*t));};
        const double dt=-time_/steps_;double r=radius;
        for(int i=0;i<steps_;++i){const double a=velocity(r),b=velocity(r+dt*a*.5),c=velocity(r+dt*b*.5),e=velocity(r+dt*c);r+=dt*(a+2*b+2*c+e)/6;}
        return {d.x()*(r/radius-1)/(size_.width()-1),d.y()*(r/radius-1)/(size_.height()-1)};
    }
private:
    bool keepAspect_{true};QSize size_;QPointF center_;double inner_{},outer_{},time_{};int steps_{};
};
}
