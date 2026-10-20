#pragma once
#include "huelith/editing/project.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
// Two source-space range lines define the stretch axis. The lower line stays
// fixed, the upper body translates rigidly, and only the selected span grows.
class PortraitManualHeight {
public:
    PortraitManualHeight(QSize size,const QJsonObject& person,double amount):size_(size){
        const auto range=person["settings"].toObject()["manualHeightRange"].toArray();
        if(range.size()!=2)throw EditError("请先在画布设置手动增高的上下范围线");
        const auto pixel=[&](QJsonArray a){if(a.size()!=2 || !a[0].isDouble() || !a[1].isDouble() || !std::isfinite(a[0].toDouble()) || !std::isfinite(a[1].toDouble()))throw EditError("增高范围坐标无效");return QPointF(a[0].toDouble()*(size.width()-1),a[1].toDouble()*(size.height()-1));};
        origin_=pixel(range[0].toArray());const auto bottom=pixel(range[1].toArray()),delta=bottom-origin_;
        length_=std::hypot(delta.x(),delta.y());if(length_<30)throw EditError("两条增高范围线距离太近");
        axis_=delta/length_;normal_={-axis_.y(),axis_.x()};
        if(axis_.y()<.5)throw EditError("请从上到下设置范围线，倾斜角度不超过 60 度");
        gain_=length_*.12*std::clamp(amount,-1.,1.);
        if(!person["settings"].toObject()["manualHeightProtectHead"].toBool(true)){
            radius_=std::max(double(size.width()),double(size.height()));upper_=-length_*.3;plateau_=0;return;
        }
        const auto face=person["face"].toArray();if(face.size()!=4)throw EditError("缺少头部定位，无法保护头部比例");
        double headTop=0,headBottom=-1e9;radius_=length_*.35;
        const auto protect=[&](QPointF p){const auto d=p-origin_;const double t=QPointF::dotProduct(d,axis_);headTop=std::min(headTop,t);headBottom=std::max(headBottom,t);radius_=std::max(radius_,std::abs(QPointF::dotProduct(d,normal_))*1.5);
            headBounds_=headBounds_.isNull()?QRectF(p,QSizeF(1,1)):headBounds_.united(QRectF(p,QSizeF(1,1)));
            const auto moved=p-axis_*gain_;if(moved.x()<0 || moved.y()<0 || moved.x()>size.width()-1 || moved.y()>size.height()-1)throw EditError("增高后头部超出画布，请缩小幅度或扩展画布");};
        const auto landmarks=person["landmarks"].toArray();
        if(landmarks.size()<3)throw EditError("缺少头部定位，无法保护头部比例");
        for(const auto& v:landmarks)protect(pixel(QJsonArray{v.toArray()[0],v.toArray()[1]}));
        const auto hair=QImage::fromData(QByteArray::fromBase64(person["settings"].toObject()["automaticMasks"].toObject()["hair"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(!hair.isNull())for(int y=0;y<hair.height();y+=2)for(int x=0;x<hair.width();x+=2)if(hair.constScanLine(y)[x]>180)protect({double(x)/(hair.width()-1)*(size.width()-1),double(y)/(hair.height()-1)*(size.height()-1)});
        if(headBottom>0)throw EditError("上范围线穿过头部或头发，请放到头部下方");
        const auto body=person["body"].toArray();if(body.size()==4)for(int y=0;y<2;++y)for(int x=0;x<2;++x){const auto q=pixel(QJsonArray{body[0].toDouble()+x*body[2].toDouble(),body[1].toDouble()+y*body[3].toDouble()})-origin_;radius_=std::max(radius_,std::abs(QPointF::dotProduct(q,normal_))*1.5);}
        upper_=headTop-std::max(length_*.3,std::abs(gain_)*4);plateau_=headTop;
        headBounds_=headBounds_.united(headBounds_.translated(-axis_*gain_)).adjusted(-4,-4,4,4);
    }
    double maximum_displacement()const{return std::abs(gain_);}
    double background_weight(QPointF q,double background)const{
        const QPointF p(q.x()*(size_.width()-1),q.y()*(size_.height()-1));
        const double dx=std::max({headBounds_.left()-p.x(),0.,p.x()-headBounds_.right()}),dy=std::max({headBounds_.top()-p.y(),0.,p.y()-headBounds_.bottom()});
        return std::max(background,1-smooth(std::hypot(dx,dy)/std::max(8.,4*std::abs(gain_))));
    }
    QRectF bounds()const{return {0,0,1,1};}
    QPointF displacement(QPointF q)const{
        const auto p=QPointF(q.x()*(size_.width()-1),q.y()*(size_.height()-1))-origin_;
        const double t=QPointF::dotProduct(p,axis_),n=std::abs(QPointF::dotProduct(p,normal_));
        if(t<=upper_ || t>=length_ || n>=radius_*1.4)return {};
        const double lateral=1-smooth((n/radius_-.7)/.7);
        const auto forward=[&](double s){double weight=1;if(s<plateau_)weight=smooth((s-upper_)/(plateau_-upper_));else if(s>0)weight=1-smooth(s/length_);return s-gain_*weight*lateral;};
        double lo=upper_,hi=length_;for(int i=0;i<28;++i){const double mid=(lo+hi)*.5;if(forward(mid)<t)lo=mid;else hi=mid;}
        const auto d=axis_*((lo+hi)*.5-t);return {d.x()/(size_.width()-1),d.y()/(size_.height()-1)};
    }
private:
    static double smooth(double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);}
    QSize size_;QPointF origin_,axis_,normal_;QRectF headBounds_;double length_{},gain_{},radius_{},upper_{},plateau_{};
};
}
