#pragma once
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// A monotone horizontal warp with a fixed centre and a C1 fixed outer ring.
// Region coordinates are stored in the original image, independently of zoom.
class PortraitManualSlim {
public:
    PortraitManualSlim(QSize size,const QJsonObject& person,double amount) {
        const auto a=person["settings"].toObject()["manualSlimRegion"].toArray();
        if(a.size()!=4)throw EditError("请先在画布拖出手动瘦身区域");
        for(const auto v:a)if(!v.isDouble() || !std::isfinite(v.toDouble()))throw EditError("瘦身区域坐标无效");
        box_={a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};
        if(box_.left()<0 || box_.top()<0 || box_.right()>1 || box_.bottom()>1 || box_.width()*size.width()<30 || box_.height()*size.height()<30)
            throw EditError("请在画布内设置至少 30 像素宽高的瘦身区域");
        amount_=.25*std::clamp(amount,-1.,1.);
    }
    QRectF bounds()const{return box_;}
    QPointF displacement(QPointF q)const {
        if(!box_.contains(q))return {};
        const double cx=box_.center().x(),rx=box_.width()*.5;
        const double v=(q.y()-box_.center().y())/(box_.height()*.5);
        const double weight=std::pow(1-v*v,2),target=(q.x()-cx)/rx;
        double lo=-1,hi=1;
        for(int i=0;i<30;++i){const double u=(lo+hi)*.5;
            const double mapped=u*(1-amount_*weight*std::pow(1-u*u,2));
            if(mapped<target)lo=u;else hi=u;
        }
        return {((lo+hi)*.5-target)*rx,0};
    }
private:
    QRectF box_;double amount_{};
};
}
