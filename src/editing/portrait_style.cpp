#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/portrait_landmarks.h"
#include <cstring>
#include <QPainter>
#include <QPainterPath>
#include <QJsonArray>
#include <QLineF>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace huelith::editing {
namespace {
QPointF point(const QJsonArray& a,int i,QSize size){const auto p=a[i].toArray();return {p[0].toDouble()*size.width(),p[1].toDouble()*size.height()};}
QPointF mean(const QJsonArray& a,int first,int last,QSize size){QPointF p;for(int i=first;i<=last;++i)p+=point(a,i,size);return p/(last-first+1);}
double coverage(const QImage& m,int x,int y,QSize s){if(m.isNull())return 0;return m.constScanLine(std::clamp(int(double(y)/s.height()*m.height()),0,m.height()-1))[std::clamp(int(double(x)/s.width()*m.width()),0,m.width()-1)]/255.;}
// Original deterministic radial-fiber material. Smooth angular noise avoids repeated spokes.
double iris_fiber(double u,double v,double footprint){
    const double radius=std::hypot(u,v),angle=std::atan2(v,u)/6.283185307179586+.5;
    const auto noise=[](double phase,int cells){const double q=(phase-std::floor(phase))*cells;const int cell=int(q);const double t=q-cell,s=t*t*(3-2*t);const auto hash=[cells](int i){unsigned n=unsigned(i%cells)*747796405u+2891336453u;n=((n>>((n>>28)+4))^n)*277803737u;n=(n>>22)^n;return double(n&65535)/32767.5-1.;};return hash(cell)*(1-s)+hash(cell+1)*s;};
    const double bend=.014*noise(angle,7)*radius*radius;
    const double fine=noise(angle+bend,48)*std::exp(-std::pow(48*footprint/6.283185307179586,2));
    const double broad=noise(angle+bend*.5,19)*std::exp(-std::pow(19*footprint/6.283185307179586,2));
    const double envelope=std::clamp((radius-.28)*6.,0.,1.)*std::clamp((1-radius)*8.,0.,1.);
    return envelope*(.11*broad+.07*fine)-.07*std::exp(-std::pow((radius-.88)/.07,2));
}
QImage decoded(const QJsonObject& o,const char* key){return QImage::fromData(QByteArray::fromBase64(o[key].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);}
QImage decoration_support(const QJsonObject& person,QSize size){
    const auto s=person["settings"].toObject(),automatic=s["automaticMasks"].toObject();auto skin=decoded(automatic,"skin").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);const auto lm=person["landmarks"].toArray();
    if(skin.isNull() || lm.size()<60){skin=QImage(size,QImage::Format_Grayscale8);skin.fill(0);return skin;}
    QImage allowed(size,QImage::Format_RGB32);allowed.fill(Qt::black);QPainter p(&allowed);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(Qt::white);QPolygonF face;for(int i=0;i<=16;++i)face<<point(lm,i,size);for(int i=26;i>=17;--i)face<<point(lm,i,size);p.drawPolygon(face);p.setBrush(Qt::black);
    for(const auto& range:std::array<std::pair<int,int>,3>{{{36,41},{42,47},{48,59}}}){QPolygonF polygon;for(int i=range.first;i<=range.second;++i)polygon<<point(lm,i,size);p.drawPolygon(polygon);}p.end();allowed=allowed.convertToFormat(QImage::Format_Grayscale8);
    const auto frame=decoded(automatic,"eyeglassFrame").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)skin.scanLine(y)[x]=uchar(skin.constScanLine(y)[x]*allowed.constScanLine(y)[x]/255.*(frame.isNull()?1:1-frame.constScanLine(y)[x]/255.));return skin;
}
QImage sticker_field(const QJsonObject& person,QSize size,QRect region){
    QImage field(region.size(),QImage::Format_Grayscale8);field.fill(0);const auto t=portrait_sticker_transform(person,size);if(!t.isInvertible())return field;
    // Rasterize in a canonical source-space box. Changing the caller's tile
    // clip must not change QPainter's projective curve subdivision/antialiasing.
    const auto path=t.map(portrait_sticker_path(person["settings"].toObject()["stickerMaterial"].toString("heart")));const QRect bounds=path.boundingRect().adjusted(-2,-2,2,2).toAlignedRect().intersected(QRect(QPoint(0,0),size));const auto copy=bounds.intersected(region);if(copy.isEmpty())return field;
    QImage shape(bounds.size(),QImage::Format_Grayscale8);shape.fill(0);QPainter painter(&shape);painter.setRenderHint(QPainter::Antialiasing);painter.translate(-bounds.x(),-bounds.y());painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawPath(path);painter.end();
    for(int y=copy.top();y<=copy.bottom();++y)std::memcpy(field.scanLine(y-region.y())+copy.x()-region.x(),shape.constScanLine(y-bounds.y())+copy.x()-bounds.x(),std::size_t(copy.width()));return field;
}
double mole_coverage(QPointF uv,double radius,double softness,double footprint){
    const double r=std::hypot(uv.x(),uv.y());const double angle=std::atan2(uv.y(),uv.x());const double boundary=radius*(1+.055*std::sin(angle*3)+.035*std::cos(angle*5));
    const double feather=std::max(footprint,radius*(.10+softness*.65));double t=std::clamp((boundary-r)/feather+.5,0.,1.);return t*t*(3-2*t);
}
QImage freckle_field(const QJsonObject& person,QSize size,QRect region){
    QImage field(region.size(),QImage::Format_Grayscale8);field.fill(0);const auto lm=person["landmarks"].toArray();if(lm.size()<60)return field;
    const auto settings=person["settings"].toObject(),params=settings["parameters"].toObject();const auto pt=[&](int i){return point(lm,i,size);};
    const auto chord=mean(lm,42,47,size)-mean(lm,36,41,size);const double span=std::hypot(chord.x(),chord.y());if(span<2)return field;const auto axis=chord/span;QPointF down(-axis.y(),axis.x());if(QPointF::dotProduct(pt(8)-pt(27),down)<0)down=-down;
    const auto seed=unsigned(std::clamp(settings["freckleSeed"].toDouble(1729),0.,2147483647.));
    const auto random=[seed](unsigned index,unsigned channel){unsigned n=seed^(index*747796405u+channel*2891336453u);n=((n>>((n>>28)+4))^n)*277803737u;n=(n>>22)^n;return double(n&0xffffff)/16777216.;};
    const int count=25+int(std::clamp(params["makeup.frecklesDensity"].toDouble(),0.,100.)*2);
    for(int i=0;i<count;++i){const double u=random(i,0),v=.12+random(i,1)*.70;QPointF center;
        if(i%9==0){center=pt(27)*(1-v)+(pt(31)*(1-u)+pt(35)*u)*v;}
        else{const bool right=i%2;const auto upper=pt(right?42:36)*(1-u)+pt(right?45:39)*u+down*(span*.06),lower=pt(right?35:3)*(1-u)+pt(right?13:31)*u;center=upper*(1-v)+lower*v;}
        const double radius=span*(.006+params["makeup.frecklesSize"].toDouble()*.00018)*(.55+random(i,2)*.9),flatten=.65+random(i,3)*.35;
        const QRect bounds=QRectF(center-QPointF(radius*1.5+1,radius*1.5+1),QSizeF(radius*3+2,radius*3+2)).toAlignedRect().intersected(region);
        for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){const auto delta=QPointF(x+.5,y+.5)-center;const QPointF uv(QPointF::dotProduct(delta,axis)/radius,QPointF::dotProduct(delta,down)/(radius*flatten));const double alpha=mole_coverage(uv,1,.6,1/std::max(.25,radius))*(.35+random(i,4)*.6);auto& pixel=field.scanLine(y-region.y())[x-region.x()];pixel=std::max(pixel,uchar(std::lround(alpha*255)));}
    }
    return field;
}
// Landmark-bound low-frequency exposure fields. Each stroke has tapered ends;
// max-union prevents overlapping anatomical regions from doubling the exposure.
QImage contour_light(const QJsonArray& lm,QSize size,bool shadow,const QImage& skin,const QJsonObject& automatic){
    QImage result(size,QImage::Format_Grayscale8);result.fill(0);
    if(lm.size()<60 || skin.isNull())return result;
    const auto pt=[&](int i){return point(lm,i,size);};
    const auto left=mean(lm,36,41,size),right=mean(lm,42,47,size);
    const double span=QLineF(left,right).length();if(span<4)return result;
    const auto horizontal=(right-left)/span;QPointF down(-horizontal.y(),horizontal.x());
    if(QPointF::dotProduct(pt(8)-pt(27),down)<0)down=-down;
    const auto stroke=[&](QPointF a,QPointF b,double widthA,double widthB,double strength){
        const auto d=b-a;const double length2=QPointF::dotProduct(d,d);if(length2<1)return;
        const double radius=std::max(widthA,widthB);
        const QRect bounds=QRectF(a,b).normalized().adjusted(-radius,-radius,radius,radius).toAlignedRect().intersected(result.rect());
        for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){
            const QPointF q(x+.5,y+.5);const double t=QPointF::dotProduct(q-a,d)/length2;if(t<=0 || t>=1)continue;
            const double width=(widthA*(1-t)+widthB*t)*std::sin(3.141592653589793*t);
            const double distance=QLineF(q,a+d*t).length()/std::max(.001,width);if(distance>=1)continue;
            const double smooth=(1-distance*distance)*(1-distance*distance);
            auto& pixel=result.scanLine(y)[x];pixel=std::max(pixel,uchar(std::lround(255*strength*smooth)));
        }
    };
    if(!shadow){
        const auto brow=mean(lm,17,26,size)-down*(span*.30);
        stroke(brow-horizontal*(span*.44),brow+horizontal*(span*.44),span*.22,span*.22,.65);
        stroke(pt(27)+down*(span*.08),pt(30),span*.065,span*.10,.85);
        const auto chin=pt(8)*.55+pt(57)*.45;
        stroke(chin-horizontal*(span*.28),chin+horizontal*(span*.28),span*.11,span*.11,.55);
    }else{
        for(int side=0;side<2;++side){
            const double sign=side?1.:-1.;
            stroke(pt(27)+horizontal*(sign*span*.10)+down*(span*.08),pt(side?35:31),span*.065,span*.09,.65);
            stroke(pt(side?14:2)*.7+pt(side?35:31)*.3+down*(span*.08),pt(side?54:48)*.65+pt(side?12:4)*.35,span*.20,span*.08,.7);
            stroke(pt(side?12:4)*.9+pt(33)*.1,pt(side?9:7)*.9+pt(57)*.1,span*.12,span*.07,.45);
        }
    }
    QImage allowed(size,QImage::Format_RGB32);allowed.fill(Qt::white);QPainter protect(&allowed);protect.setRenderHint(QPainter::Antialiasing);protect.setPen(QPen(Qt::black,span*.035));protect.setBrush(Qt::black);
    for(const auto& range:std::array<std::pair<int,int>,3>{{{36,41},{42,47},{48,59}}}){QPolygonF polygon;for(int i=range.first;i<=range.second;++i)polygon<<pt(i);protect.drawPolygon(polygon);}protect.end();
    allowed=allowed.convertToFormat(QImage::Format_Grayscale8);const auto glasses=decoded(automatic,"eyeglassFrame").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){double weight=skin.constScanLine(y)[x]/255.*allowed.constScanLine(y)[x]/255.;if(!glasses.isNull())weight*=1-glasses.constScanLine(y)[x]/255.;result.scanLine(y)[x]=uchar(std::lround(result.constScanLine(y)[x]*weight));}
    return result;
}
// Eyelid-local pigment field: the lid is the lower boundary and the brow bounds the socket.
// Re-evaluating those boundaries also fits a closed or rotated eye without an elliptical stamp.
QImage eye_shadow(const QJsonArray& lm,int eye,int brow,QSize size,bool accent,bool left){
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);
    const auto origin=point(lm,eye,size),chord=point(lm,eye+3,size)-origin;const double width=std::hypot(chord.x(),chord.y());if(width<2)return mask;
    const auto axis=chord/width;QPointF up(axis.y(),-axis.x());if(up.y()>0)up=-up;
    const auto local=[&](QPointF p){p-=origin;return QPointF(QPointF::dotProduct(p,axis)/width,QPointF::dotProduct(p,up)/width);};
    std::array<QPointF,4> lid;for(int i=0;i<4;++i)lid[i]=local(point(lm,eye+i,size));
    std::array<QPointF,5> browLine;for(int i=0;i<5;++i)browLine[i]=local(point(lm,brow+i,size));
    const auto interpolate=[](const auto& nodes,double u){for(std::size_t i=1;i<nodes.size();++i)if(u<=nodes[i].x()){const double t=std::clamp((u-nodes[i-1].x())/std::max(.0001,nodes[i].x()-nodes[i-1].x()),0.,1.);return nodes[i-1].y()+(nodes[i].y()-nodes[i-1].y())*t;}return nodes.back().y();};
    std::sort(lid.begin(),lid.end(),[](QPointF a,QPointF b){return a.x()<b.x();});std::sort(browLine.begin(),browLine.end(),[](QPointF a,QPointF b){return a.x()<b.x();});
    const auto smooth=[](double v){v=std::clamp(v,0.,1.);return v*v*(3-2*v);};
    const QRect bounds=QRectF(origin-QPointF(width,width),QSizeF(width*3,width*2)).toAlignedRect().intersected(mask.rect());
    for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){const auto q=local(QPointF(x+.5,y+.5));const double u=q.x();if(u<=0 || u>=1)continue;
        const double edge=interpolate(lid,u),browEdge=interpolate(browLine,u)-.09;
        const double height=std::min(.48,(browEdge-edge)*.78);if(height<=.015)continue;
        const double v=(q.y()-edge)/height;if(v<=0 || v>=1)continue;
        double weight=smooth(u/.16)*smooth((1-u)/.16)*smooth(v/.10)*smooth((1-v)/.55);
        if(accent){const double outer=left?u:1-u;weight*=smooth((outer-.35)/.45)*smooth((.85-v)/.45);}
        mask.scanLine(y)[x]=uchar(std::lround(weight*255));
    }
    return mask;
}
QImage eye_liner(const QJsonArray& lm,int eye,QSize size,bool lower,bool left,const QString& style){
    QImage image(size,QImage::Format_ARGB32);image.fill(Qt::black);
    const auto a=point(lm,eye,size),b=point(lm,eye+3,size),chord=b-a;const double width=std::hypot(chord.x(),chord.y());if(width<2)return image.convertToFormat(QImage::Format_Grayscale8);
    QPointF up(chord.y()/width,-chord.x()/width);if(up.y()>0)up=-up;
    std::array<QPointF,4> anchors{a,point(lm,eye+(lower?5:1),size),point(lm,eye+(lower?4:2),size),b};
    const auto curve=[&](double t){const double q=std::clamp(t,0.,1.)*3;const int i=std::min(2,int(q));const double u=q-i;const auto p0=anchors[std::max(0,i-1)],p1=anchors[i],p2=anchors[i+1],p3=anchors[std::min(3,i+2)];return (p1*2+(p2-p0)*u+(p0*2-p1*5+p2*4-p3)*(u*u)+(-p0+p1*3-p2*3+p3)*(u*u*u))*.5;};
    QPolygonF shape;std::vector<QPointF> outside;
    for(int i=0;i<=80;++i){const double t=i/80.,outer=left?t:1-t;double thickness=width*(style=="round"?.012+.045*std::pow(std::sin(t*3.141592653589793),2):style=="wing"?.008+.05*outer*outer:.022);
        thickness*=std::min(1.,std::min(t,1-t)*14);if(lower)thickness*=.48*std::clamp(outer*2,0.,1.);
        const auto root=curve(t);shape<<root;outside.push_back(root+up*(lower?-thickness:thickness));
    }
    for(auto it=outside.rbegin();it!=outside.rend();++it)shape<<*it;
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawPolygon(shape);
    if(!lower && style=="wing"){
        const auto end=left?b:a,axis=chord/width*(left?1.:-1.);const auto root=curve(left?.88:.12),tip=end+axis*(width*.22)+up*(width*.10);
        QPainterPath wing;wing.moveTo(root);wing.quadTo(end+up*(width*.075),tip);wing.quadTo(end+axis*(width*.05),end);wing.closeSubpath();painter.drawPath(wing);
    }
    // The pigment stays on the lid skin, including near the inner corner.
    QPolygonF opening;for(int i=0;i<6;++i)opening<<point(lm,eye+i,size);painter.setBrush(Qt::black);painter.drawPolygon(opening);painter.end();return image.convertToFormat(QImage::Format_Grayscale8);
}
QImage eye_lashes(const QJsonArray& lm,int eye,QSize size,bool left,const QString& style){
    QImage image(size,QImage::Format_ARGB32);image.fill(Qt::black);
    std::array<QPointF,4> anchors;for(int i=0;i<4;++i)anchors[i]=point(lm,eye+i,size);
    const auto chord=anchors[3]-anchors[0];const double width=std::hypot(chord.x(),chord.y());if(width<2)return image.convertToFormat(QImage::Format_Grayscale8);
    const auto axis=chord/width;QPointF up(axis.y(),-axis.x());if(up.y()>0)up=-up;
    const double opening=QLineF(point(lm,eye+1,size),point(lm,eye+5,size)).length()/width;
    const double projection=std::clamp(.6+opening,.6,1.);
    const auto curve=[&](double t){const double q=std::clamp(t,0.,1.)*3;const int i=std::min(2,int(q));const double u=q-i;const auto a=anchors[std::max(0,i-1)],b=anchors[i],c=anchors[i+1],d=anchors[std::min(3,i+2)];return (b*2+(c-a)*u+(a*2-b*5+c*4-d)*(u*u)+(-a+b*3-c*3+d)*(u*u*u))*.5;};
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(Qt::NoBrush);
    const int clusters=style=="volume"?25:19,strands=style=="volume"?3:2;
    for(int n=0;n<clusters;++n){const double jitter=double((unsigned(n)*2654435761u)>>24)/255.,t=.06+.88*(n+.25+jitter*.5)/clusters,outer=left?t:1-t;
        const double envelope=.35+.65*std::pow(std::sin(t*3.141592653589793),.8);
        const double length=width*(style=="long"?.23:style=="volume"?.18:.14)*envelope*projection*(.65+.6*jitter);
        for(int strand=0;strand<strands;++strand){const double fan=(strand-(strands-1)*.5)*.28;const auto root=curve(t+fan*.012);const double sweep=(t-.5)*1.5+fan+(left?1:-1)*outer*.18+(jitter-.5)*.2;const auto tip=root+up*(length*.8)+axis*(length*sweep);
            QPainterPath hair;hair.moveTo(root);hair.cubicTo(root+up*(length*.70),root+up*length+axis*(length*sweep*.7),tip);
            for(int segment=0;segment<12;++segment){const double u=segment/12.;painter.setPen(QPen(QColor(255,255,255,210),std::max(.16,width*.010*(1-u)*(.8+.2*jitter)),Qt::SolidLine,Qt::RoundCap));painter.drawLine(hair.pointAtPercent(u),hair.pointAtPercent((segment+1)/12.));}
        }
    }
    // Keep roots on the lid and avoid painting the eye surface after curved interpolation.
    painter.setPen(Qt::NoPen);painter.setBrush(Qt::black);QPolygonF eyeOpening;for(int i=0;i<6;++i)eyeOpening<<point(lm,eye+i,size);painter.drawPolygon(eyeOpening);painter.end();return image.convertToFormat(QImage::Format_Grayscale8);
}
QImage lower_lid_volume(const QJsonArray& lm,int eye,QSize size,bool shade){
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);const auto a=point(lm,eye,size),chord=point(lm,eye+3,size)-a;const double width=std::hypot(chord.x(),chord.y());if(width<2)return mask;
    const auto axis=chord/width;QPointF down(-axis.y(),axis.x());if(down.y()<0)down=-down;
    const auto local=[&](QPointF p){p-=a;return QPointF(QPointF::dotProduct(p,axis)/width,QPointF::dotProduct(p,down)/width);};
    std::array<QPointF,4> lid{local(a),local(point(lm,eye+5,size)),local(point(lm,eye+4,size)),local(point(lm,eye+3,size))};
    std::sort(lid.begin(),lid.end(),[](QPointF p,QPointF q){return p.x()<q.x();});
    const QRect bounds=QRectF(a-QPointF(width,width),QSizeF(width*3,width*2)).toAlignedRect().intersected(mask.rect());
    for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){const auto q=local(QPointF(x+.5,y+.5));const double u=q.x();if(u<=.08 || u>=.92)continue;double edge=0;
        for(int i=1;i<4;++i)if(u<=lid[i].x()){const double t=std::clamp((u-lid[i-1].x())/std::max(.0001,lid[i].x()-lid[i-1].x()),0.,1.);edge=lid[i-1].y()*(1-t)+lid[i].y()*t;break;}
        const double v=q.y()-edge;if(v<=.04 || v>=.25)continue;
        const double taper=std::pow(std::sin((u-.08)/.84*3.141592653589793),2),fade=std::clamp((v-.04)/.025,0.,1.)*std::clamp((.25-v)/.035,0.,1.);
        const double field=taper*fade*(std::exp(-std::pow((v-.095)/.045,2))-.55*std::exp(-std::pow((v-.185)/.035,2)));
        mask.scanLine(y)[x]=uchar(std::lround(std::clamp(shade?std::max(0.,-field):std::abs(field),0.,1.)*255));
    }
    return mask;
}
QImage lid_crease(const QJsonArray& lm,int eye,int brow,QSize size,bool left,const QString& style){
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);const auto a=point(lm,eye,size),chord=point(lm,eye+3,size)-a;const double width=std::hypot(chord.x(),chord.y());if(width<10)return mask;
    const auto axis=chord/width;QPointF up(axis.y(),-axis.x());if(up.y()>0)up=-up;
    const auto local=[&](QPointF p){p-=a;return QPointF(QPointF::dotProduct(p,axis)/width,QPointF::dotProduct(p,up)/width);};
    std::array<QPointF,4> lid;for(int i=0;i<4;++i)lid[i]=local(point(lm,eye+i,size));
    const double browHeight=local(mean(lm,brow,brow+4,size)).y(),openness=QLineF(point(lm,eye+1,size),point(lm,eye+5,size)).length()/width;
    if(browHeight<.18)return mask;
    const auto bounds=QRectF(a-QPointF(width,width),QSizeF(width*3,width*2)).toAlignedRect().intersected(mask.rect());
    for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){const auto q=local(QPointF(x+.5,y+.5));const double u=q.x();if(u<=.04 || u>=.96)continue;double edge=0;
        for(int i=1;i<4;++i)if(u<=lid[i].x()){const double t=std::clamp((u-lid[i-1].x())/std::max(.0001,lid[i].x()-lid[i-1].x()),0.,1.);edge=lid[i-1].y()*(1-t)+lid[i].y()*t;break;}
        const double outer=left?u:1-u,offset=(style=="fan"?.015+.105*std::sin(outer*1.570796326794897):.095)*std::clamp(.6+openness,.6,1.);
        if(edge+offset+.04>browHeight-.08 || q.y()<=edge+.012)continue;
        const double v=q.y()-edge-offset,taper=std::pow(std::sin((u-.04)/.92*3.141592653589793),.6);
        const double coverage=taper*std::exp(-std::pow(v/.018,2));mask.scanLine(y)[x]=uchar(std::lround(coverage*255));
    }
    return mask;
}
QImage lip_pigment(const QJsonArray& lm,QSize size,const QString& style,double feather){
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);std::vector<cv::Point2f> outer,inner;QPolygonF bounds;
    for(int i:portrait_landmarks::outer_lip){const auto p=point(lm,i,size);outer.emplace_back(float(p.x()),float(p.y()));bounds<<p;}
    for(int i:portrait_landmarks::inner_lip){const auto p=point(lm,i,size);inner.emplace_back(float(p.x()),float(p.y()));}
    const auto box=bounds.boundingRect().toAlignedRect().intersected(mask.rect());const double width=bounds.boundingRect().width(),softness=std::max(.6,width*(.004+feather*.00035));
    const auto smooth=[](double v){v=std::clamp(v,0.,1.);return v*v*(3-2*v);};
    for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){const cv::Point2f q(float(x)+.5F,float(y)+.5F);const double outside=cv::pointPolygonTest(outer,q,true),oral=cv::pointPolygonTest(inner,q,true);if(outside<=0 || oral>=0)continue;
        double alpha=smooth(outside/softness)*std::clamp(-oral+.5,0.,1.);
        if(style=="bitten"){const double depth=outside/std::max(.0001,outside-oral);alpha*=smooth((depth-.12)/.75);}
        mask.scanLine(y)[x]=uchar(std::lround(alpha*255));
    }return mask;
}
QImage lip_bleed(const QJsonArray& lm,QSize size,double spread){
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);std::vector<cv::Point2f> outline;QPolygonF polygon;
    for(int i:portrait_landmarks::outer_lip){const auto p=point(lm,i,size);outline.emplace_back(float(p.x()),float(p.y()));polygon<<p;}
    const auto a=point(lm,58,size),chord=point(lm,62,size)-a;const double width=std::hypot(chord.x(),chord.y());if(width<2)return mask;
    const double reach=width*(.004+std::clamp(spread,0.,100.)*.0003);const auto bounds=polygon.boundingRect().adjusted(-reach,-reach,reach,reach).toAlignedRect().intersected(mask.rect());
    for(int y=bounds.top();y<=bounds.bottom();++y)for(int x=bounds.left();x<=bounds.right();++x){const QPointF q(x+.5,y+.5);const double distance=-cv::pointPolygonTest(outline,cv::Point2f(float(q.x()),float(q.y())),true);if(distance<=0 || distance>=reach)continue;
        const double u=QPointF::dotProduct(q-a,chord)/(width*width),corner=std::clamp(u/.12,0.,1.)*std::clamp((1-u)/.12,0.,1.),t=1-distance/reach;
        mask.scanLine(y)[x]=uchar(std::lround(corner*t*t*(3-2*t)*255));
    }return mask;
}
QImage lip_sparkle(const QJsonArray& lm,QSize size,double density,double particleSize){
    QImage image(size,QImage::Format_ARGB32);image.fill(Qt::black);const auto a=point(lm,58,size),b=point(lm,62,size),center=(a+b)*.5,chord=b-a;const double width=std::hypot(chord.x(),chord.y());if(width<2)return image.convertToFormat(QImage::Format_Grayscale8);
    const auto axis=chord/width;QPointF down(-axis.y(),axis.x());if(down.y()<0)down=-down;
    const auto random=[](unsigned n){n=n*747796405u+2891336453u;n=((n>>((n>>28)+4))^n)*277803737u;n=(n>>22)^n;return double(n&65535)/65535.;};
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);
    const double fraction=std::clamp(.35+density*.003,.05,.65),radius=width*(.0025+std::clamp(particleSize,0.,100.)*.000075);
    for(unsigned i=0;i<160;++i){if(random(i*4+3)>fraction)continue;const auto q=center+axis*((random(i*4)-.5)*width)+down*((random(i*4+1)-.5)*width*.42);const double variation=.65+.7*random(i*4+2);p.setBrush(QColor(255,255,255,int(150+105*random(i*4+7))));p.drawEllipse(q,radius*variation,radius*variation*.65);}
    p.end();auto result=image.convertToFormat(QImage::Format_Grayscale8);const auto lip=lip_pigment(lm,size,"sheer",0);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*lip.constScanLine(y)[x]/255);return result;
}
}
std::array<double,3> portrait_accessory_color(const QString& material,QPointF uv,const QJsonArray& color,double roughness){
    const double u=uv.x(),v=uv.y(),r=std::clamp(roughness,0.,1.);double diffuse=.7,specular=0;
    if(material=="bow"){
        const double wing=std::clamp(std::abs(u),0.,1.),fold=std::exp(-std::pow(v/(.035+wing*.12),2));
        diffuse=.48+.40*std::sin(wing*2.5)+.18*std::clamp(-v,0.,.5)-.20*fold;
        specular=.20*std::exp(-std::pow((v+.15*wing)/(.025+r*.12),2))*(1-r*.65);
        if(std::abs(u)<.18){diffuse=.5+.45*std::sqrt(std::max(0.,1-u*u/.0324));specular=.12*std::exp(-std::pow((u+.04)/(.025+r*.06),2));}
    }else if(material=="flower"){
        double height=0;for(int i=0;i<5;++i){const double a=i*6.283185307179586/5,px=u*std::cos(a)+v*std::sin(a),py=-u*std::sin(a)+v*std::cos(a);height=std::max(height,1-std::pow(px/.22,2)-std::pow((py+.525)/.325,2));}
        height=std::sqrt(std::max(0.,height));diffuse=.5+.45*height-.12*v;specular=.22*std::pow(height,8+30*(1-r))*(1-r*.6);
        if(u*u+v*v<.04){const double z=std::sqrt(std::max(0.,1-(u*u+v*v)/.04));diffuse=.45+.5*z;specular=.42*std::exp(-((u+.055)*(u+.055)+(v+.06)*(v+.06))/(.001+r*.003));}
    }else{
        const double z=std::sqrt(std::max(0.,1-std::pow(v/.12,2)));diffuse=.3+.6*z-.4*v;specular=.48*std::exp(-std::pow((v+.038)/(.008+r*.045),2))*(1-r*.5);
        diffuse*=.75+.25*std::clamp((1-std::abs(u))/.12,0.,1.);
    }
    std::array<double,3> result{};for(int c=0;c<3;++c)result[c]=std::clamp(color[c].toDouble()*std::clamp(diffuse,0.,1.)+specular,0.,1.);return result;
}
QPainterPath portrait_sticker_path(const QString& material){
    // Original project vector assets; also used for the material-card thumbnails.
    QPainterPath path;
    if(material=="star"){for(int i=0;i<10;++i){const double a=-1.570796326794897+i*.628318530717959,r=i%2?.43:1.;const QPointF p(std::cos(a)*r,std::sin(a)*r);if(i==0)path.moveTo(p);else path.lineTo(p);}path.closeSubpath();}
    else if(material=="flower"){for(int i=0;i<5;++i){QPainterPath petal;petal.addEllipse(QRectF(-.25,-.95,.5,.72));QTransform rotate;rotate.rotate(i*72);path.addPath(rotate.map(petal));}path.addEllipse(QRectF(-.23,-.23,.46,.46));path=path.simplified();}
    else{path.moveTo(0,.95);path.cubicTo(-.3,.55,-1,.08,-.95,-.45);path.cubicTo(-.9,-1.05,-.2,-1.1,0,-.55);path.cubicTo(.2,-1.1,.9,-1.05,.95,-.45);path.cubicTo(1,.08,.3,.55,0,.95);path.closeSubpath();}return path;
}
QTransform portrait_sticker_surface(const QJsonObject& person,QSize size){
    const auto lm=person["landmarks"].toArray();if(lm.size()<60)return QTransform(0,0,0,0,0,0);const auto pt=[&](int i){return point(lm,i,size);};const bool right=person["settings"].toObject()["stickerSide"].toString("left")=="left";
    const auto chord=mean(lm,42,47,size)-mean(lm,36,41,size);QPointF down(-chord.y(),chord.x());if(QPointF::dotProduct(pt(8)-pt(27),down)<0)down=-down;
    const QPolygonF uv{{-1,-1},{1,-1},{1,1},{-1,1}},cheek{pt(right?42:36)+down*.18,pt(right?45:39)+down*.18,pt(right?13:31),pt(right?35:3)};
    QTransform surface;if(!QTransform::quadToQuad(uv,cheek,surface))return QTransform(0,0,0,0,0,0);return surface;
}
QTransform portrait_sticker_transform(const QJsonObject& person,QSize size){
    const auto p=person["settings"].toObject()["parameters"].toObject();QTransform local;local.translate(p["makeup.stickerX"].toDouble()*.01,p["makeup.stickerY"].toDouble()*.01);local.rotate(p["makeup.stickerAngle"].toDouble()*1.8);const double scale=.12+std::clamp(p["makeup.stickerSize"].toDouble(),0.,100.)*.004;local.scale(scale,scale);return local*portrait_sticker_surface(person,size);
}
QTransform portrait_mole_transform(const QJsonObject& person,QSize size){
    const auto lm=person["landmarks"].toArray();if(lm.size()<60)return QTransform(0,0,0,0,0,0);const auto left=mean(lm,36,41,size),right=mean(lm,42,47,size),axis=right-left;const double span=QLineF(left,right).length();if(span<1)return QTransform(0,0,0,0,0,0);
    QPointF down(-axis.y(),axis.x());if(QPointF::dotProduct(point(lm,8,size)-point(lm,27,size),down)<0)down=-down;
    const auto params=person["settings"].toObject()["parameters"].toObject();const auto center=point(lm,30,size)+axis*(.45+params["makeup.moleX"].toDouble()*.01)+down*(.65+params["makeup.moleY"].toDouble()*.01);
    return QTransform(axis.x(),axis.y(),down.x(),down.y(),center.x(),center.y());
}
QJsonObject portrait_contour_lighting(const QImage& source,const QJsonObject& person){
    if(source.isNull() || person["face"].toArray().size()!=4)return {};
    const auto size=source.size().scaled(256,256,Qt::KeepAspectRatio);
    const auto linear=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA32FPx4);
    auto skin=decoded(person["settings"].toObject()["automaticMasks"].toObject(),"skin").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(skin.isNull())return {};
    const auto f=person["face"].toArray();const QRectF face(f[0].toDouble()*size.width(),f[1].toDouble()*size.height(),f[2].toDouble()*size.width(),f[3].toDouble()*size.height());
    cv::Mat weight(size.height(),size.width(),CV_32F,cv::Scalar(0)),light(weight.size(),CV_32F,cv::Scalar(0));double sum=0,count=0;
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto* p=reinterpret_cast<const float*>(linear.constScanLine(y))+x*4;if(!face.contains(QPointF(x+.5,y+.5)) || p[3]<.01F)continue;
        const double w=skin.constScanLine(y)[x]/255.,l=(.2627*p[0]+.678*p[1]+.0593*p[2])/p[3];if(!std::isfinite(l) || l<0)continue;weight.at<float>(y,x)=float(w);light.at<float>(y,x)=float(l*w);sum+=l*w;count+=w;}
    if(count<16 || sum/count<1e-5)return {};
    const double average=sum/count,sigma=std::max(1.,face.width()*.08);cv::GaussianBlur(weight,weight,{0,0},sigma);cv::GaussianBlur(light,light,{0,0},sigma);
    QImage highlight(size,QImage::Format_Grayscale8),shadow(size,QImage::Format_Grayscale8);highlight.fill(0);shadow.fill(0);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){if(weight.at<float>(y,x)<.05F)continue;const double l=light.at<float>(y,x)/weight.at<float>(y,x),ratio=l/average;
        // Do not invent a new light direction: lift existing light-facing areas,
        // shade existing dark-facing areas, and suppress already crushed shadows.
        const double high=std::clamp((ratio-.65)/.55,0.,1.)*std::clamp((1-l)/.25,0.,1.);
        const double low=std::clamp((1.3-ratio)/.55,0.,1.)*std::clamp(ratio/.35,0.,1.);
        highlight.scanLine(y)[x]=uchar(std::lround(high*255));shadow.scanLine(y)[x]=uchar(std::lround(low*255));}
    return {{"version",1},{"highlight",portrait_mask_png(highlight)},{"shadow",portrait_mask_png(shadow)}};
}
QImage portrait_style_mask(const QJsonObject& person,const QString& key,QSize size) {
    if(key=="makeup.tattoo")return portrait_tattoo_coverage(person,size);
    if(key=="makeup.nails")return portrait_nail_coverage(person,size);
    const auto settings=person["settings"].toObject(),params=settings["parameters"].toObject(),automatic=settings["automaticMasks"].toObject();const auto lm=person["landmarks"].toArray();
    const auto skin=decoded(automatic,"skin").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(key=="face.doubleChin" && settings["doubleChinVersion"].toInt()>=2){auto mask=decoded(settings["doubleChinAnalysis"].toObject(),"mask");if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(0);return mask;}return mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);}

    if(key=="makeup.sticker"){auto field=sticker_field(person,size,QRect(QPoint(0,0),size));const auto support=decoration_support(person,size);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)field.scanLine(y)[x]=uchar(field.constScanLine(y)[x]*support.constScanLine(y)[x]/255);return field;}
    if(key=="makeup.freckles"){auto field=freckle_field(person,size,QRect(QPoint(0,0),size));const auto support=decoration_support(person,size);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)field.scanLine(y)[x]=uchar(field.constScanLine(y)[x]*support.constScanLine(y)[x]/255);return field;}
    if(key=="makeup.mole"){
        auto support=decoration_support(person,size);bool valid=false;const auto transform=portrait_mole_transform(person,size),inverse=transform.inverted(&valid);if(!valid){support.fill(0);return support;}
        const double span=std::hypot(transform.m11(),transform.m12()),radius=.012+params["makeup.moleSize"].toDouble()*.0004;
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)support.scanLine(y)[x]=uchar(std::lround(support.constScanLine(y)[x]*mole_coverage(inverse.map(QPointF(x+.5,y+.5)),radius,params["makeup.moleFeather"].toDouble()*.01,1/span)));return support;
    }
    if(settings["contourLightVersion"].toInt()>=2 && (key=="contour.highlight" || key=="contour.shadow")){
        auto field=contour_light(lm,size,key=="contour.shadow",skin,automatic);
        if(settings["contourAutoLighting"].toBool()){const auto gate=decoded(settings["contourLighting"].toObject(),key=="contour.shadow"?"shadow":"highlight").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)field.scanLine(y)[x]=gate.isNull()?0:uchar(field.constScanLine(y)[x]*gate.constScanLine(y)[x]/255);}
        return field;
    }
    if(key.startsWith("hair.") && key!="hair.accessory"){
        auto mask=decoded(automatic,"hair").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        if(settings["hairEdgeVersion"].toInt()>=1){const auto refined=decoded(settings["hairLighting"].toObject(),"edgeAlpha");if(!refined.isNull())mask=refined.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);}
        const auto manualEdge=decoded(settings["masks"].toObject(),"hair.edge");if(!manualEdge.isNull())mask=manualEdge.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        if(key=="hair.roots" && !mask.isNull()){
            const auto scalp=settings["scalpGeometry"].toObject();const auto line=scalp["hairline"].toArray();
            if(!scalp["valid"].toBool() || line.size()<2){mask.fill(0);return mask;}
            const double width=std::max(1.,person["face"].toArray()[2].toDouble()*size.width()*(.02+params["hair.rootWidth"].toDouble()*.0013));
            for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){double distance=width;const QPointF p(x+.5,y+.5);
                for(int i=1;i<line.size();++i){const auto aa=line[i-1].toArray(),bb=line[i].toArray();const QPointF a(aa[0].toDouble()*size.width(),aa[1].toDouble()*size.height()),b(bb[0].toDouble()*size.width(),bb[1].toDouble()*size.height()),d=b-a;const double length=QPointF::dotProduct(d,d),t=length>1e-8?std::clamp(QPointF::dotProduct(p-a,d)/length,0.,1.):0;distance=std::min(distance,std::hypot((p-a-d*t).x(),(p-a-d*t).y()));}
                const double t=std::clamp(1-distance/width,0.,1.);mask.scanLine(y)[x]=uchar(mask.constScanLine(y)[x]*t*t*(3-2*t));
            }
        }
        if(key.startsWith("hair.zone_") && !mask.isNull())for(const auto& value:settings["hairZones"].toArray()){const auto zone=value.toObject();if(zone["key"].toString()!=key || zone["mode"].toString()!="highlights" || zone["strandVersion"].toInt()<2)continue;
            QImage strands(size,QImage::Format_ARGB32);strands.fill(Qt::black);QPainter painter(&strands);painter.setRenderHint(QPainter::Antialiasing);const double width=std::max(.5,person["face"].toArray()[2].toDouble()*size.width()*(.002+zone["strandWidth"].toDouble(35)*.00025)),density=zone["strandDensity"].toDouble(40)*.01;
            for(const auto& c:settings["hairLighting"].toObject()["strands"].toArray()){const auto curve=c.toObject();if(curve["rank"].toDouble()>=density)continue;const auto points=curve["points"].toArray();for(int i=1;i<points.size();++i){const double t=double(i)/(points.size()-1),taper=std::pow(std::max(0.,std::sin(t*3.141592653589793)),.4);painter.setPen(QPen(QColor(255,255,255,int(210*taper)),width*taper,Qt::SolidLine,Qt::RoundCap));const auto a=points[i-1].toArray(),b=points[i].toArray();painter.drawLine(QPointF(a[0].toDouble()*size.width(),a[1].toDouble()*size.height()),QPointF(b[0].toDouble()*size.width(),b[1].toDouble()*size.height()));}}
            painter.end();const auto field=strands.convertToFormat(QImage::Format_Grayscale8);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)mask.scanLine(y)[x]=uchar(mask.constScanLine(y)[x]*field.constScanLine(y)[x]/255);
        }
        if(key=="hair.oil" && !mask.isNull()){const auto field=decoded(settings["hairLighting"].toObject(),"oil").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)mask.scanLine(y)[x]=field.isNull()?0:uchar(mask.constScanLine(y)[x]*field.constScanLine(y)[x]/255);}
        if(key=="hair.shine" && settings["hairShineVersion"].toInt()>=2 && !mask.isNull()){const auto field=decoded(settings["hairLighting"].toObject(),"highlight").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)mask.scanLine(y)[x]=field.isNull()?0:uchar(mask.constScanLine(y)[x]*field.constScanLine(y)[x]/255);}return mask;
    }
    const auto protectBrow=[&](QImage mask){for(const auto* key:{"hair","eyeglassFrame"}){const auto obstruction=decoded(automatic,key).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(!obstruction.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)mask.scanLine(y)[x]=uchar(mask.constScanLine(y)[x]*(255-obstruction.constScanLine(y)[x])/255);}return mask;};
    if(key=="makeup.foundation"){
        auto mask=decoded(automatic,"faceSkin");const bool fallback=mask.isNull();if(fallback)mask=decoded(automatic,"skin");if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(0);return mask;}
        mask=protectBrow(mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
        if(fallback){const auto f=person["face"].toArray();if(f.size()!=4){mask.fill(0);return mask;}const double left=f[0].toDouble()*size.width(),top=f[1].toDouble()*size.height(),right=left+f[2].toDouble()*size.width(),bottom=top+f[3].toDouble()*size.height(),feather=std::max(1.,(right-left)*.04);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const double d=std::min({x-left,right-x,y-top,bottom-y}),t=std::clamp(d/feather,0.,1.);mask.scanLine(y)[x]=uchar(mask.constScanLine(y)[x]*t*t*(3-2*t));}}
        const auto keep=decoded(settings["masks"].toObject(),"repair.preserve").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        if(!keep.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)mask.scanLine(y)[x]=uchar(mask.constScanLine(y)[x]*(255-keep.constScanLine(y)[x])/255);return mask;
    }
    QImage image(size,QImage::Format_ARGB32);image.fill(Qt::black);if(lm.size()!=66)return image.convertToFormat(QImage::Format_Grayscale8);
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::white);p.setPen(Qt::NoPen);const auto f=person["face"].toArray();const double fw=f[2].toDouble()*size.width(),fh=f[3].toDouble()*size.height();const QPointF fc((f[0].toDouble()+f[2].toDouble()*.5)*size.width(),(f[1].toDouble()+f[3].toDouble()*.5)*size.height());
    const bool left=key.endsWith(".left");const QString base=key.startsWith("makeup.irisTexture")?QStringLiteral("makeup.iris"):key.section('.',0,1);const int eye=left?42:36,brow=left?22:17;const auto ec=mean(lm,eye,eye+5,size);const double ew=QLineF(point(lm,eye,size),point(lm,eye+3,size)).length();
    if(base=="makeup.blushGloss" || base=="makeup.blushSparkle"){
        p.end();const QString suffix=left?".left":".right";if(portrait_side_value(settings,"blushMaterialVersion",left).toInt()<2)return image.convertToFormat(QImage::Format_Grayscale8);
        auto result=portrait_style_mask(person,"makeup.blush"+suffix,size);const bool matte=base=="makeup.blushGloss" && params[key].toDouble()<0;
        const auto field=decoded(settings["cheekLighting"].toObject(),matte?"matte":"gloss").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        const auto inner=point(lm,left?35:31,size),outer=point(lm,left?13:3,size),chord=outer-inner;const double width=std::hypot(chord.x(),chord.y());if(width<2){result.fill(0);return result;}
        const auto axis=chord/width;QPointF down(-axis.y(),axis.x());if(down.y()<0)down=-down;
        const auto center=(inner+outer)*.5+QPointF(fw*params["makeup.blushX"+suffix].toDouble()*.01,fh*params["makeup.blushY"+suffix].toDouble()*.01);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){double material=field.isNull()?0:field.constScanLine(y)[x]/255.;
            if(base=="makeup.blushSparkle"){const auto d=QPointF(x+.5,y+.5)-center;const double u=QPointF::dotProduct(d,axis)/width*70+1000,v=QPointF::dotProduct(d,down)/width*70+1000;const unsigned ix=unsigned(std::max(0.,std::floor(u))),iy=unsigned(std::max(0.,std::floor(v)));unsigned hash=(ix*73856093u)^(iy*19349663u);hash^=hash>>13;hash*=1274126177u;const double px=.2+double(hash&255)/255.*.6,py=.2+double((hash>>8)&255)/255.*.6;const double distance=std::hypot(u-std::floor(u)-px,v-std::floor(v)-py);material*=((hash>>16)&255)<45?std::clamp((.22-distance)*width/70.,0.,1.):0;}
            else if(!matte){const auto d=QPointF(x+.5,y+.5)-center;const double u=QPointF::dotProduct(d,axis)/width,v=QPointF::dotProduct(d,down)/width;material*=std::exp(-std::pow((u-.1)/.4,2)-std::pow((v+.12)/.13,2));}
            result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*material);
        }return result;
    }
    if(base=="makeup.lipGloss" && settings["lipFinishVersion"].toInt()>=2){p.end();const auto field=decoded(settings["lipLighting"].toObject(),params["makeup.lipGloss"].toDouble()<0?"matte":"gloss").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);auto result=protectBrow(lip_pigment(lm,size,"sheer",0));for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=field.isNull()?0:uchar(result.constScanLine(y)[x]*field.constScanLine(y)[x]/255);return result;}
    if(base=="makeup.lip" && settings["lipMaterialVersion"].toInt()>=2){p.end();return protectBrow(lip_pigment(lm,size,settings["lipMaterial"].toString("sheer"),params["makeup.lipFeather"].toDouble()));}
    if(base=="makeup.lipSparkle"){p.end();return protectBrow(lip_sparkle(lm,size,params["makeup.lipSparkleDensity"].toDouble(),params["makeup.lipSparkleSize"].toDouble()));}
    const auto eyelidSkin=[&]{
        auto result=decoded(automatic,"makeupSkin").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        if(result.isNull() && !skin.isNull()){
            // Older projects stored only the smoothing mask, with a wide eye exclusion.
            // Recover adjacent skin coverage here; the material geometry still clips the eye.
            result=skin.copy();const int radius=std::max(1,int(std::ceil(fw*.035)));cv::Mat m(result.height(),result.width(),CV_8U,result.bits(),result.bytesPerLine());cv::dilate(m,m,cv::getStructuringElement(cv::MORPH_ELLIPSE,{radius*2+1,radius*2+1}));
        }return result;
    };
    if(base=="makeup.lipBleed"){
        p.end();auto result=protectBrow(lip_bleed(lm,size,params["makeup.lipBleedWidth"].toDouble()));const auto materialSkin=eyelidSkin();
        if(!materialSkin.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*materialSkin.constScanLine(y)[x]/255);return result;
    }
    if((base=="makeup.shadow" || base=="makeup.shadowAccent") && portrait_side_value(settings,"eyeShadowVersion",left).toInt()>=2){
        p.end();auto result=protectBrow(eye_shadow(lm,eye,brow,size,base=="makeup.shadowAccent",left));
        const auto materialSkin=eyelidSkin();if(!materialSkin.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*materialSkin.constScanLine(y)[x]/255);
        return result;
    }
    if((base=="makeup.liner" || base=="makeup.linerLower") && portrait_side_value(settings,"eyeLinerVersion",left).toInt()>=2){p.end();return protectBrow(eye_liner(lm,eye,size,base=="makeup.linerLower",left,portrait_side_value(settings,"linerMaterial",left).toString("soft")));}
    if(base=="makeup.lash" && portrait_side_value(settings,"eyeLashVersion",left).toInt()>=2){p.end();return protectBrow(eye_lashes(lm,eye,size,left,portrait_side_value(settings,"lashMaterial",left).toString("light")));}
    if(base=="makeup.aegyosal" || base=="makeup.aegyosalShade"){
        p.end();auto result=protectBrow(lower_lid_volume(lm,eye,size,base=="makeup.aegyosalShade"));
        const auto materialSkin=eyelidSkin();if(!materialSkin.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*materialSkin.constScanLine(y)[x]/255);return result;
    }
    if(base=="makeup.crease"){
        p.end();auto result=protectBrow(lid_crease(lm,eye,brow,size,left,portrait_side_value(settings,"creaseMaterial",left).toString("fan")));
        const auto materialSkin=eyelidSkin();if(!materialSkin.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*materialSkin.constScanLine(y)[x]/255);return result;
    }
    if(base=="makeup.catchlight" && portrait_side_value(settings,"irisMaterialVersion",left).toInt()>=2){
        const QString suffix=left?".left":".right";const auto materials=settings["irisMaterials"].toObject(),geometry=materials["geometry"+suffix].toObject();const auto ellipse=geometry["ellipse"].toArray();
        if(geometry["valid"].toBool() && ellipse.size()==5){const double shortSide=std::min(size.width(),size.height()),rx=ellipse[2].toDouble()*shortSide,ry=ellipse[3].toDouble()*shortSide;
            p.translate(ellipse[0].toDouble()*size.width(),ellipse[1].toDouble()*size.height());p.rotate(ellipse[4].toDouble());p.scale(rx,ry);const QPointF center(-.28+params["makeup.catchX"+suffix].toDouble()*.01,-.35+params["makeup.catchY"+suffix].toDouble()*.01);const double radius=.16+params["makeup.catchSize"+suffix].toDouble()*.0035;const auto material=portrait_side_value(settings,"catchMaterial",left).toString("circle");
            if(material=="window"){p.drawRect(QRectF(center-QPointF(radius,radius),QSizeF(radius*2,radius*2)));p.setPen(QPen(Qt::black,radius*.18));p.drawLine(center-QPointF(radius,0),center+QPointF(radius,0));p.drawLine(center-QPointF(0,radius),center+QPointF(0,radius));}
            else if(material=="ring"){p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,radius*.3));p.drawEllipse(center,radius,radius);}else p.drawEllipse(center,radius,radius);
        }
        p.end();auto result=image.convertToFormat(QImage::Format_Grayscale8);const auto visible=decoded(materials,left?"cornea.left":"cornea.right").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation),frame=decoded(automatic,"eyeglassFrame").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){double coverage=visible.isNull()?0:visible.constScanLine(y)[x]/255.;if(!frame.isNull())coverage*=1-frame.constScanLine(y)[x]/255.;result.scanLine(y)[x]=uchar(result.scanLine(y)[x]*coverage);}return result;
    }
    if(base=="makeup.iris" && portrait_side_value(settings,"irisMaterialVersion",left).toInt()>=2){p.end();const auto mask=decoded(settings["irisMaterials"].toObject(),left?"iris.left":"iris.right");if(mask.isNull())return image.convertToFormat(QImage::Format_Grayscale8);auto result=mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);const auto frame=decoded(automatic,"eyeglassFrame").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(!frame.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.scanLine(y)[x]*(255-frame.constScanLine(y)[x])/255);return result;}
    auto polygon=[&](int first,int last){QPolygonF shape;for(int i=first;i<=last;++i)shape<<point(lm,i,size);p.drawPolygon(shape);};
    double blur=std::max(.5,fw*.009);
    if(base=="makeup.lip"){
        if(settings["mouthTopologyVersion"].toInt(1)>=2){
            QPolygonF outer,inner;for(int i:portrait_landmarks::outer_lip)outer<<point(lm,i,size);for(int i:portrait_landmarks::inner_lip)inner<<point(lm,i,size);
            p.drawPolygon(outer);p.setBrush(Qt::black);p.drawPolygon(inner);
        }else{polygon(48,59);p.setBrush(Qt::black);polygon(60,65);}
        blur+=params["makeup.lipFeather"].toDouble()*.0003*fw;
    }
    else if((base=="makeup.brow" || base=="makeup.browShape") && portrait_side_value(settings,"browMaterialVersion",left).toInt()>=2){
        if(base=="makeup.brow"){p.end();const auto material=decoded(settings["browMaterials"].toObject(),left?"left":"right");if(material.isNull())return image.convertToFormat(QImage::Format_Grayscale8);return protectBrow(material.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));}
        std::array<QPointF,5> anchors;for(int i=0;i<5;++i)anchors[i]=point(lm,left?22+i:21-i,size);const auto chord=anchors[4]-anchors[0];const double length=std::hypot(chord.x(),chord.y());if(length<2){p.end();return image.convertToFormat(QImage::Format_Grayscale8);}QPointF normal(-chord.y()/length,chord.x()/length);if(normal.y()>0)normal=-normal;
        const auto material=portrait_side_value(settings,"browMaterial",left).toString("natural");
        const auto curve=[&](double t){const double q=std::clamp(t,0.,1.)*4;const int i=std::min(3,int(q));const double u=q-i;const auto a=anchors[std::max(0,i-1)],b=anchors[i],c=anchors[i+1],d=anchors[std::min(4,i+2)];auto position=(b*2+(c-a)*u+(a*2-b*5+c*4-d)*(u*u)+(-a+b*3-c*3+d)*(u*u*u))*.5;
            const auto line=anchors[0]+chord*t;if(material=="straight")position=position*.35+line*.65;else if(material=="arched")position+=normal*(length*.075*std::pow(std::sin(t*3.141592653589793),2));return position;};
        p.setBrush(Qt::NoBrush);
        for(int i=0;i<90;++i){const double t=(i+.5)/90.,jitter=double((unsigned(i)*2654435761u)>>24)/255.;const auto c=curve(t);const double width=length*(.018+.055*std::pow(std::sin(t*3.141592653589793),.7))*(1-.55*t);const auto root=c-normal*(width*(.4+jitter*.4));const double advance=.025+.045*t;const auto tip=curve(std::min(1.,t+advance))+normal*(width*(.3+.5*jitter));QPainterPath hair;hair.moveTo(root);hair.cubicTo(root+normal*width,tip-normal*width*.25,tip);p.setPen(QPen(QColor(255,255,255,int(120+100*jitter)),std::max(.35,length*.009),Qt::SolidLine,Qt::RoundCap));p.drawPath(hair);}
        blur=.2;
    }
    else if(base=="makeup.brow" || base=="makeup.browShape"){QPainterPath path;path.moveTo(point(lm,brow,size));for(int i=brow+1;i<brow+5;++i)path.lineTo(point(lm,i,size));p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,fw*(base=="makeup.brow"?.026:.043),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(path);}
    else if(base=="makeup.blush" && portrait_side_value(settings,"blushMaterialVersion",left).toInt()>=2){
        p.end();auto result=image.convertToFormat(QImage::Format_Grayscale8);const QString suffix=left?".left":".right";
        const auto inner=point(lm,left?35:31,size),outer=point(lm,left?13:3,size),chord=outer-inner;const double width=std::hypot(chord.x(),chord.y());if(width<2)return result;
        const auto axis=chord/width;QPointF down(-axis.y(),axis.x());if(down.y()<0)down=-down;
        const double spread=params.contains("makeup.blushSpread"+suffix)?params["makeup.blushSpread"+suffix].toDouble():params["makeup.blushSpread"].toDouble();
        const double scale=1+std::clamp(spread,0.,100.)*.006;
        const auto center=(inner+outer)*.5+QPointF(fw*params["makeup.blushX"+suffix].toDouble()*.01,fh*params["makeup.blushY"+suffix].toDouble()*.01);
        const auto material=portrait_side_value(settings,"blushMaterial",left).toString("underEye");
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const auto d=QPointF(x+.5,y+.5)-center;const double u=QPointF::dotProduct(d,axis)/(width*scale),v=QPointF::dotProduct(d,down)/(width*scale);double field=0;
            if(material=="horizontal"){const double a=std::abs(u)/.62,b=std::abs(v+.04)/.19;field=std::exp(-2*(a*a*a*a+b*b));}
            else if(material=="diagonal"){const double a=u/.6,b=(v+.22*u+.03)/.16;field=std::exp(-2*(a*a+b*b))*std::clamp(1-.45*u,.5,1.3);}
            else{const double a=u/.51,b=(v+.16-.24*u*u)/.16;field=std::exp(-2*(a*a+b*b));}
            result.scanLine(y)[x]=uchar(std::clamp(field,0.,1.)*255);
        }
        result=protectBrow(result);if(!skin.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*skin.constScanLine(y)[x]/255);return result;
    }
    else if(base=="makeup.blush"){const auto c=(point(lm,left?13:3,size)+point(lm,left?35:31,size))*.5;p.drawEllipse(c,fw*(.14+params["makeup.blushSpread"].toDouble()*.0009),fh*.085);blur=fw*.045;}
    else if(base=="makeup.shadow"){p.drawEllipse(ec-QPointF(0,ew*.18),ew*.6,ew*.3);p.setBrush(Qt::black);polygon(eye,eye+5);blur=fw*.018;}
    else if(base=="makeup.liner" || base=="makeup.lash"){QPainterPath path;path.moveTo(point(lm,eye,size));for(int i=1;i<=3;++i)path.lineTo(point(lm,eye+i,size));p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,std::max(.8,ew*.035),Qt::SolidLine,Qt::RoundCap));p.drawPath(path);if(base=="makeup.lash")for(int i=0;i<8;++i){const double t=(i+.5)/8;const auto a=point(lm,eye,size)*(1-t)+point(lm,eye+3,size)*t-QPointF(0,std::sin(t*3.14159265)*ew*.13);p.drawLine(a,a+QPointF((t-.5)*ew*.12,-ew*.14));}blur=.5;}
    else if(base=="makeup.iris" || base=="makeup.catchlight"){
        const double openness=QLineF(point(lm,eye+1,size),point(lm,eye+5,size)).length()/std::max(1.,ew);
        if(openness>.08){QPainterPath clip;QPolygonF outline;for(int i=eye;i<eye+6;++i)outline<<point(lm,i,size);clip.addPolygon(outline);p.setClipPath(clip);
            if(base=="makeup.iris"){p.drawEllipse(ec,ew*.17,ew*.17);p.setBrush(Qt::black);p.drawEllipse(ec,ew*.075,ew*.075);}
            else{const QString suffix=left?".left":".right";const auto center=ec+QPointF(ew*(-.05+params["makeup.catchX"+suffix].toDouble()*.002),ew*(-.07+params["makeup.catchY"+suffix].toDouble()*.002));const double radius=ew*(.032+params["makeup.catchSize"+suffix].toDouble()*.0007);const auto material=portrait_side_value(settings,"catchMaterial",left).toString("circle");if(material=="window")p.drawRect(QRectF(center-QPointF(radius,radius),QSizeF(radius*2,radius*2)));else if(material=="ring"){p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,std::max(.6,radius*.35)));p.drawEllipse(center,radius,radius);}else p.drawEllipse(center,radius,radius);}
        }blur=.4;
    }
    else if(key=="contour.highlight"){p.drawEllipse(fc+QPointF(0,-fh*.10),fw*.08,fh*.25);p.drawEllipse(point(lm,8,size)-QPointF(0,fh*.1),fw*.14,fh*.06);blur=fw*.035;}
    else if(key=="contour.shadow" || key=="face.volume"){for(int side=0;side<2;++side)p.drawEllipse((point(lm,side?13:3,size)+point(lm,side?35:31,size))*.5+QPointF(0,key=="contour.shadow"?fh*.08:0),fw*.15,fh*.10);blur=fw*.05;}
    else if(key=="face.doubleChin"){p.drawEllipse(point(lm,8,size)+QPointF(0,fh*.13),fw*.4,fh*.13);blur=fw*.05;}
    else if(key=="light.face")p.drawEllipse(fc,fw*.65,fh*.75);
    else if(key=="light.body"){p.fillRect(image.rect(),Qt::white);p.setBrush(Qt::black);p.drawEllipse(fc,fw*.65,fh*.75);}
    else if(key=="hair.accessory"){
        p.setTransform(portrait_accessory_transform(person,params,size));
        const auto material=settings["hairMaterial"].toString("clip");if(material=="flower"){for(int i=0;i<5;++i){p.save();p.rotate(i*72);p.drawEllipse(QRectF(-.22,-.85,.44,.65));p.restore();}p.drawEllipse(QRectF(-.2,-.2,.4,.4));}else if(material=="bow"){if(settings["hairAccessoryVersion"].toInt()<2){QPolygonF a{{0,0},{-.95,-.5},{-.95,.5}},b{{0,0},{.95,-.5},{.95,.5}};p.drawPolygon(a);p.drawPolygon(b);}else for(int side:{-1,1}){QPainterPath wing;wing.moveTo(0,0);wing.cubicTo(side*.35,-.12,side*.85,-.72,side*.95,-.45);wing.cubicTo(side*1.03,-.12,side*1.03,.15,side*.95,.45);wing.cubicTo(side*.8,.7,side*.3,.1,0,0);p.drawPath(wing);}p.drawEllipse(QRectF(-.18,-.22,.36,.44));}else p.drawRoundedRect(QRectF(-1,-.12,2,.24),.1,.1);blur=.3;
    }
    else if(key=="contour.lines" && settings["bodyLinesQuad"].toArray().size()==4){
        QPolygonF destination;for(const auto& v:settings["bodyLinesQuad"].toArray()){const auto a=v.toArray();destination<<QPointF(a[0].toDouble()*size.width(),a[1].toDouble()*size.height());}
        QTransform transform;const QPolygonF unit{QPointF(0,0),QPointF(1,0),QPointF(1,1),QPointF(0,1)};
        if(QTransform::quadToQuad(unit,destination,transform)){
            const QString material=settings["bodyMaterial"].toString();const double width=QLineF(destination[0],destination[1]).length();
            p.setTransform(transform);p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,.025,Qt::SolidLine,Qt::RoundCap));
            QPainterPath shape;
            if(material=="collarbone"){shape.moveTo(.48,.4);shape.cubicTo(.35,.65,.12,.25,.04,.55);shape.moveTo(.52,.4);shape.cubicTo(.65,.65,.88,.25,.96,.55);}
            else if(material=="abdomen"){shape.moveTo(.5,.06);shape.cubicTo(.47,.35,.53,.65,.5,.94);for(double y:{.28,.52,.76}){shape.moveTo(.18,y);shape.quadTo(.33,y+.08,.43,y);shape.moveTo(.57,y);shape.quadTo(.72,y+.08,.82,y);}}
            else if(material=="waist"){shape.moveTo(.12,.05);shape.cubicTo(.4,.35,.4,.65,.12,.95);shape.moveTo(.88,.05);shape.cubicTo(.6,.35,.6,.65,.88,.95);}
            p.drawPath(shape);blur=width*.018;
        }
    }
    else if(key=="contour.lines"){
        const auto pose=settings["pose"].toArray();if(pose.size()==33){const auto shoulders=(point(pose,11,size)+point(pose,12,size))*.5,hips=(point(pose,23,size)+point(pose,24,size))*.5;const double width=QLineF(point(pose,11,size),point(pose,12,size)).length(),height=QLineF(shoulders,hips).length();p.translate((shoulders+hips)*.5+QPointF(width*params["contour.linesX"].toDouble()*.01,height*params["contour.linesY"].toDouble()*.01));p.rotate(params["contour.linesAngle"].toDouble()*.9);p.scale(1+params["contour.linesWidth"].toDouble()*.005,1+params["contour.linesHeight"].toDouble()*.005);p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,width*.035,Qt::SolidLine,Qt::RoundCap));const auto material=settings["bodyMaterial"].toString("waist");for(int side:{-1,1}){QPainterPath path;if(material=="collarbone"){path.moveTo(0,-height*.35);path.quadTo(side*width*.18,-height*.45,side*width*.35,-height*.32);}else{path.moveTo(side*width*.22,-height*.2);path.quadTo(side*width*.1,0,side*width*.22,height*.3);}p.drawPath(path);}blur=width*.017;}
    }
    p.end();auto gray=image.convertToFormat(QImage::Format_Grayscale8);cv::Mat m(gray.height(),gray.width(),CV_8U,gray.bits(),gray.bytesPerLine());if(blur>.1)cv::GaussianBlur(m,m,{0,0},std::max(.3,blur));
    if(base=="makeup.lip" && settings["mouthTopologyVersion"].toInt(1)>=2){
        // Feather the pigment outward, never back into the oral cavity/teeth.
        image.fill(Qt::white);QPainter clip(&image);clip.setRenderHint(QPainter::Antialiasing);clip.setPen(Qt::NoPen);clip.setBrush(Qt::black);
        QPolygonF inner;for(int i:portrait_landmarks::inner_lip)inner<<point(lm,i,size);clip.drawPolygon(inner);clip.end();
        const auto allowed=image.convertToFormat(QImage::Format_Grayscale8);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)gray.scanLine(y)[x]=uchar(gray.constScanLine(y)[x]*allowed.constScanLine(y)[x]/255);
    }
    if(key=="hair.accessory"){
        const auto mode=settings["hairAccessoryOcclusion"].toString("manual");
        if(mode=="skin" || mode=="behindHair"){
            const auto skinOccluder=decoded(automatic,"makeupSkin").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation),frameOccluder=decoded(automatic,"eyeglassFrame").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            auto hairOccluder=decoded(automatic,"hair").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(settings["hairEdgeVersion"].toInt()>=1){const auto edge=decoded(settings["hairLighting"].toObject(),"edgeAlpha");if(!edge.isNull())hairOccluder=edge.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);}
            for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){int obstruction=skinOccluder.isNull()?0:skinOccluder.constScanLine(y)[x];if(!frameOccluder.isNull())obstruction=std::max(obstruction,int(frameOccluder.constScanLine(y)[x]));if(mode=="behindHair" && !hairOccluder.isNull())obstruction=std::max(obstruction,int(hairOccluder.constScanLine(y)[x]));gray.scanLine(y)[x]=uchar(gray.constScanLine(y)[x]*(255-obstruction)/255);}
        }
    }
    if(base=="makeup.browShape" && portrait_side_value(settings,"browMaterialVersion",left).toInt()>=2)gray=protectBrow(gray);
    const bool skin_only=key=="face.volume" || key=="face.doubleChin" || key.startsWith("contour.") || key.startsWith("light.") || base=="makeup.blush" || base=="makeup.shadow";
    if(key=="contour.lines" && skin.isNull())gray.fill(0);
    if(skin_only && !skin.isNull())for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)gray.scanLine(y)[x]=uchar(gray.constScanLine(y)[x]*skin.constScanLine(y)[x]/255);
    return gray;
}
QImage portrait_style_region(QImage base,QSize source_size,const QJsonObject& recipe,QRect region,double amount,const std::atomic_bool* cancelled,const QImage& effect_mask) {
    const auto people=recipe["people"].toArray();for(const auto& value:people)base=portrait_volume_region(std::move(base),source_size,value.toObject(),region,amount,effect_mask,cancelled);const auto size=source_size.scaled(768,768,Qt::KeepAspectRatio);std::vector<QImage> bodies;for(const auto& v:people)bodies.push_back(decoded(v.toObject(),"bodyMask"));
    for(int index=0;index<people.size();++index){const auto person=people[index].toObject(),settings=person["settings"].toObject(),params=settings["parameters"].toObject(),enabled=settings["enabled"].toObject();if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool())continue;
        const auto disabled=settings["disabledParameters"].toObject();
        const bool autoContourTone=settings["contourAutoLighting"].toBool() && settings["contourLighting"].toObject()["version"].toInt()==1 && enabled["contour"].toBool(true) &&
            ((params["contour.highlight"].toDouble()!=0 && !disabled["contour.highlight"].toBool()) || (params["contour.shadow"].toDouble()!=0 && !disabled["contour.shadow"].toBool()));
        auto descriptors=portrait_parameters(11);descriptors.push_back({"face.doubleChin",QStringLiteral("双下巴明暗")});for(int panel=12;panel<=14;++panel){const auto extra=portrait_parameters(panel);descriptors.insert(descriptors.end(),extra.begin(),extra.end());}for(const auto& zone:settings["hairZones"].toArray())descriptors.push_back({zone.toObject()["key"].toString(),QStringLiteral("分区染发"),false,0,100});
        for(const auto& descriptor:descriptors){
            // The automatic combination supplies the tonal component; retain
            // face.volume geometry but do not add its legacy cheek exposure again.
            if(descriptor.key=="face.volume" && (autoContourTone || settings["cheekVolumeVersion"].toInt()>=2))continue;
            const QString root=descriptor.key;if(root.startsWith("paint.") || root.startsWith("makeup.tattoo") || root.startsWith("makeup.nails") || root.startsWith("head.") || root=="makeup.foundation" || root=="hair.detail" || root.contains("Size") || root.endsWith("X") || root.endsWith("Y") || root.endsWith("Angle") || root.endsWith("Width") || root.endsWith("Height") || root.endsWith("Feather") || root.endsWith("Spread") || (root.endsWith("Gloss") && !((root=="makeup.lipGloss" && settings["lipFinishVersion"].toInt()>=2) || root=="makeup.blushGloss")) || root.endsWith("Roughness") || root.endsWith("Coverage") || root.endsWith("Density") || root=="light.highlights")continue;
            if(!enabled[root.section('.',0,0)].toBool(true) || settings["disabledParameters"].toObject()[root].toBool())continue;
            for(int side=0;side<(descriptor.bilateral?2:1);++side){const QString key=root+(descriptor.bilateral?(side==0?".left":".right"):"");const double value=params[key].toDouble()*.01*amount*settings["strength"].toDouble(100)*.01;if(value==0)continue;
                if(!base.isDetached()){auto detached=allocate_image(base.size(),base.format());detached.setColorSpace(base.colorSpace());for(int y=0;y<base.height();++y)std::memcpy(detached.scanLine(y),base.constScanLine(y),std::size_t(base.width())*16);base=std::move(detached);}
                const bool fineEyeMaterial=(root.startsWith("hair.") && settings["hairEdgeVersion"].toInt()>=1) || root=="makeup.blushSparkle" || root=="makeup.lipBleed" || root=="makeup.lipSparkle" || root=="makeup.lipGloss" || (root=="makeup.lip" && settings["lipMaterialVersion"].toInt()>=2) || root=="makeup.crease" || (root.startsWith("makeup.liner") && portrait_side_value(settings,"eyeLinerVersion",side==0).toInt()>=2) || (root=="makeup.lash" && portrait_side_value(settings,"eyeLashVersion",side==0).toInt()>=2);const auto maskSize=fineEyeMaterial?(std::max(source_size.width(),source_size.height())>2048?source_size.scaled(2048,2048,Qt::KeepAspectRatio):source_size):size;
                const bool decoration=root=="makeup.mole" || root=="makeup.freckles" || root=="makeup.sticker";const auto sticker=root=="makeup.sticker"?sticker_field(person,source_size,region):QImage{};const auto freckles=root=="makeup.freckles"?freckle_field(person,source_size,region):QImage{};auto mask=decoration?decoration_support(person,maskSize):portrait_effect_mask(person,key,maskSize);if(decoration){const auto manual=decoded(settings["masks"].toObject(),root.toUtf8().constData()).scaled(maskSize,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(!manual.isNull())for(int y=0;y<maskSize.height();++y)for(int x=0;x<maskSize.width();++x)mask.scanLine(y)[x]=uchar(mask.constScanLine(y)[x]*manual.constScanLine(y)[x]/255);}const auto moleTransform=portrait_mole_transform(person,source_size),moleInverse=moleTransform.inverted();const double moleSpan=std::max(1.,std::hypot(moleTransform.m11(),moleTransform.m12()));const QString colorKey=root=="makeup.lipBleed"?QStringLiteral("makeup.lip"):root=="makeup.linerLower"?QStringLiteral("makeup.liner"):root=="makeup.browShape" && portrait_side_value(settings,"browMaterialVersion",side==0).toInt()>=2?QStringLiteral("makeup.brow"):root;QJsonArray color=(descriptor.bilateral?portrait_side_value(settings["colors"].toObject(),colorKey,side==0):settings["colors"].toObject().value(colorKey)).toArray();if(color.size()!=3){if(root=="makeup.sticker")color={.5,.035,.08};else if(decoration)color={.10,.04,.02};else if(root.startsWith("hair."))color={.10,.035,.014};else if(root=="makeup.brow" || root=="makeup.browShape" || root.startsWith("makeup.liner") || root=="makeup.lash")color={.015,.008,.005};else if(root=="makeup.iris")color={.025,.08,.11};else if((root=="makeup.lip" && settings["lipMaterialVersion"].toInt()>=2) || root=="makeup.lipBleed")color={.20,.09,.085};else if(root=="makeup.blush" && portrait_side_value(settings,"blushMaterialVersion",side==0).toInt()>=2)color={.24,.13,.12};else if(root=="makeup.shadow")color={.22,.12,.08};else if(root=="makeup.shadowAccent")color={.12,.065,.045};else color={.42,.07,.09};}
                const auto waterHighlights=root=="hair.water"?decoded(settings["hairLighting"].toObject(),"highlight"):QImage{};
                const auto hairHighlights=settings["hairColorVersion"].toInt()>=2 && (root=="hair.color" || root=="hair.roots" || root.startsWith("hair.zone_"))?decoded(settings["hairLighting"].toObject(),"highlight"):QImage{};
                const auto edgeForeground=settings["hairEdgeVersion"].toInt()>=1 && (root=="hair.color" || root=="hair.roots" || root.startsWith("hair.zone_"))?QImage::fromData(QByteArray::fromBase64(settings["hairLighting"].toObject()["edgeForeground"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_RGBA64):QImage{};
                const auto lowerField=root=="makeup.aegyosal"?portrait_style_mask(person,key,maskSize):QImage{};
                const auto lowerShade=root=="makeup.aegyosal"?portrait_style_mask(person,QString("makeup.aegyosalShade")+(side==0?".left":".right"),maskSize):QImage{};
                const auto accessoryInverse=portrait_accessory_transform(person,params,source_size).inverted();
                const auto irisEllipse=settings["irisMaterials"].toObject()[side==0?"geometry.left":"geometry.right"].toObject()["ellipse"].toArray();
                for(int y=region.top();y<=region.bottom();++y){if(cancelled && cancelled->load())throw EditError("妆发光影已取消");auto* out=reinterpret_cast<float*>(base.scanLine(y-region.y()));for(int x=region.left();x<=region.right();++x){double w=value*coverage(mask,x,y,source_size);if(!effect_mask.isNull())w*=coverage(effect_mask,x,y,source_size);for(int other=0;other<people.size();++other)if(other!=index)w*=1-coverage(bodies[std::size_t(other)],x,y,source_size);if(w==0)continue;auto* pixel=out+(x-region.x())*4;if(pixel[3]<.00001F)continue;const double l=(.2627*pixel[0]+.678*pixel[1]+.0593*pixel[2])/pixel[3];
                    if(root=="makeup.sticker"){const double alpha=sticker.constScanLine(y-region.y())[x-region.x()]/255.*w*.85;for(int c=0;c<3;++c){const double pigment=std::min(1.,std::clamp(color[c].toDouble(),0.,1.)*std::pow(std::clamp(l/.25,0.,1.5),.5));pixel[c]+=float((pigment*pixel[3]-pixel[c])*alpha);}}
                    else if(root=="makeup.freckles"){const double pigment=freckles.constScanLine(y-region.y())[x-region.x()]/255.*w;for(int c=0;c<3;++c)pixel[c]*=float(1-pigment*.65*(1-std::clamp(color[c].toDouble(),0.,1.)));}
                    else if(root=="makeup.mole"){const double pigment=mole_coverage(moleInverse.map(QPointF(x+.5,y+.5)),.012+params["makeup.moleSize"].toDouble()*.0004,params["makeup.moleFeather"].toDouble()*.01,1/moleSpan)*w;for(int c=0;c<3;++c)pixel[c]*=float(1-pigment*.85*(1-std::clamp(color[c].toDouble(),0.,1.)));}
                    else if(root=="hair.accessory" && settings["hairAccessoryVersion"].toInt()>=2){const auto desired=portrait_accessory_color(settings["hairMaterial"].toString("clip"),accessoryInverse.map(QPointF(x+.5,y+.5)),color,params["hair.accessoryRoughness"].toDouble()*.01);for(int c=0;c<3;++c)pixel[c]+=float((desired[c]*pixel[3]-pixel[c])*w);}
                    else if(root=="makeup.lipSparkle" || root=="makeup.blushSparkle"){const double peak=std::max({pixel[0],pixel[1],pixel[2]})/pixel[3],light=std::clamp(l/.25,.15,1.),glint=w*.25*light*std::max(0.,1.-peak);for(int c=0;c<3;++c)pixel[c]+=float(glint*pixel[3]*(c==0?1.:c==1?.93:.82));}
                    else if(root=="makeup.lipGloss" || root=="makeup.blushGloss"){if(w<0){for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*(1+w*.75));}else{const double peak=std::max({pixel[0],pixel[1],pixel[2]})/pixel[3];const double highlight=w*.35*std::max(0.,1.-peak);for(int c=0;c<3;++c)pixel[c]+=float(highlight*pixel[3]);}}
                    else if(root=="makeup.aegyosal"){const double negative=coverage(lowerShade,x,y,source_size),total=coverage(lowerField,x,y,source_size);const double sign=total>0?1-2*std::clamp(negative/total,0.,1.):0;const double gain=std::exp2(w*.25*sign);for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*gain);}
                    else if(root=="makeup.crease"){for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*std::exp2(-w*.35));}
                    else if(root=="light.face" || root=="light.body"){w*=1-std::clamp((l-.5)*1.2,0.,1.)*params["light.highlights"].toDouble()*.01;for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*std::exp2(w));}
                    else if(root=="hair.water"){
                        const double highlight=coverage(waterHighlights,x,y,source_size);const double spec=std::pow(highlight,3)*std::min(.15,l*.8)*w;
                        for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*(1-.12*w)+spec*pixel[3]);
                    }
                    else if(root=="hair.oil"){for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*(1-std::clamp(w,0.,.3)));}
                    else if(root=="hair.shine" && settings["hairShineVersion"].toInt()>=2){const double peak=std::max({pixel[0],pixel[1],pixel[2]})/pixel[3],highlight=w*std::min(l*.35,.08)*std::max(0.,1-peak);for(int c=0;c<3;++c)pixel[c]+=float(highlight*pixel[3]);}
                    else if(root=="contour.highlight" && settings["contourLightVersion"].toInt()>=2){const double peak=std::max({pixel[0],pixel[1],pixel[2]})/pixel[3];const double gain=std::min(std::exp2(w*.35),peak>0?std::max(1.,1/peak):1.);for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*gain);}
                    else if(root=="contour.highlight" || root=="face.volume" || root=="face.doubleChin" || root=="hair.shine"){for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*std::exp2(w*.4));}
                    else if(root=="contour.shadow" || root=="contour.lines"){for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*std::exp2(-w*.5));}
                    else if(root=="makeup.irisTexture"){if(irisEllipse.size()!=5)continue;const double scale=std::min(source_size.width(),source_size.height()),rx=irisEllipse[2].toDouble()*scale,ry=irisEllipse[3].toDouble()*scale;if(rx<1 || ry<1)continue;const double angle=irisEllipse[4].toDouble()*3.141592653589793/180.,dx=x-irisEllipse[0].toDouble()*source_size.width(),dy=y-irisEllipse[1].toDouble()*source_size.height();const double u=(dx*std::cos(angle)+dy*std::sin(angle))/rx,v=(-dx*std::sin(angle)+dy*std::cos(angle))/ry;const double gain=std::exp2(iris_fiber(u,v,1/std::min(rx,ry))*w);for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*gain);}
                    else if(root=="makeup.catchlight"){for(int c=0;c<3;++c)pixel[c]+=float(std::max(0.,.85*pixel[3]-pixel[c])*w);}
                    else if(root!="hair.smooth"){
                        auto tint=color;double zone_weight=1.;for(const auto& item:settings["hairZones"].toArray()){const auto zone=item.toObject();if(zone["key"].toString()!=root)continue;const auto second=zone["secondColor"].toArray();const auto mode=zone["mode"].toString();const auto f=person["face"].toArray();const double nx=(double(x)/source_size.width()-f[0].toDouble())/std::max(.001,f[2].toDouble()),ny=(double(y)/source_size.height()-f[1].toDouble())/std::max(.001,f[3].toDouble());if(mode=="gradient" && second.size()==3){double t=std::clamp((ny+.5)/1.5,0.,1.);const auto axis=zone["gradientAxis"].toArray();if(axis.size()==4){const QPointF a(axis[0].toDouble()*source_size.width(),axis[1].toDouble()*source_size.height()),b(axis[2].toDouble()*source_size.width(),axis[3].toDouble()*source_size.height()),d=b-a;const double length=QPointF::dotProduct(d,d);t=length>1e-8?std::clamp(QPointF::dotProduct(QPointF(x+.5,y+.5)-a,d)/length,0.,1.):0;}const double mid=std::clamp(zone["gradientMidpoint"].toDouble(.5),.05,.95);t=t<mid?.5*t/mid:.5+.5*(t-mid)/(1-mid);t=std::clamp((t-.5)/std::clamp(zone["gradientWidth"].toDouble(100)*.01,.05,1.)+.5,0.,1.);for(int c=0;c<3;++c)tint[c]=color[c].toDouble()*(1-t)+second[c].toDouble()*t;}if(mode=="highlights" && zone["strandVersion"].toInt()<2)zone_weight=std::pow(.5+.5*std::sin(nx*45),6);}
                        if(settings["hairColorVersion"].toInt()>=2 && (root=="hair.color" || root=="hair.roots" || root.startsWith("hair.zone_"))){
                            const double targetY=.2627*tint[0].toDouble()+.678*tint[1].toDouble()+.0593*tint[2].toDouble();if(targetY<=.0001 || l<=.0001)continue;
                            std::array<double,3> foreground{pixel[0]/pixel[3],pixel[1]/pixel[3],pixel[2]/pixel[3]};double edgeConfidence=0;
                            if(!edgeForeground.isNull()){const int ex=std::clamp(int(double(x)/source_size.width()*edgeForeground.width()),0,edgeForeground.width()-1),ey=std::clamp(int(double(y)/source_size.height()*edgeForeground.height()),0,edgeForeground.height()-1);const auto e=reinterpret_cast<const QRgba64*>(edgeForeground.constScanLine(ey))[ex];edgeConfidence=e.alpha()/65535.;const double channels[]{e.red()/65535.,e.green()/65535.,e.blue()/65535.};for(int c=0;c<3;++c)foreground[c]+=edgeConfidence*(channels[c]-foreground[c]);}
                            const double foregroundY=.2627*foreground[0]+.678*foreground[1]+.0593*foreground[2],specular=coverage(hairHighlights,x,y,source_size)*(1-edgeConfidence),reach=foregroundY/(foregroundY+.012);double gain=w*zone_weight*.85*reach*(1-(.85+.15*params["hair.preserveGloss"].toDouble()* .01)*specular);std::array<double,3> delta{};
                            for(int c=0;c<3;++c){delta[c]=foregroundY*tint[c].toDouble()/targetY-foreground[c];if(!edgeForeground.isNull()){if(delta[c]<0)gain=std::min(gain,w*zone_weight*foreground[c]/-delta[c]);else if(delta[c]>0)gain=std::min(gain,w*zone_weight*std::max(0.,1-foreground[c])/delta[c]);}if(delta[c]<0)gain=std::min(gain,double(pixel[c]/pixel[3])/-delta[c]);else if(delta[c]>0)gain=std::min(gain,std::max(0.,1-double(pixel[c]/pixel[3]))/delta[c]);}
                            for(int c=0;c<3;++c)pixel[c]+=float(delta[c]*gain*pixel[3]);continue;
                        }
                        const double target=.2627*tint[0].toDouble()+.678*tint[1].toDouble()+.0593*tint[2].toDouble();const bool pigment=(root=="makeup.brow" && portrait_side_value(settings,"browMaterialVersion",side==0).toInt()<2) || root=="makeup.browShape" || root.startsWith("makeup.liner") || root=="makeup.lash" || root=="hair.accessory";
                        for(int c=0;c<3;++c){double desired=tint[c].toDouble()*(pigment?1:l/std::max(.001,target));if(root=="makeup.lip" && settings["lipMaterialVersion"].toInt()>=2){const double opacity=std::clamp(params["makeup.lipCoverage"].toDouble()*.01,0.,1.);const double covered=tint[c].toDouble()*std::pow(std::max(.001,l)/.25,.65);desired=desired*(1-opacity)+covered*opacity;}if(root=="makeup.shadowAccent")desired*=.72;if(root=="makeup.lip" && settings["lipFinishVersion"].toInt()<2)desired*=1+params["makeup.lipGloss"].toDouble()*.002*std::clamp(l/.4,0.,1.);if(root=="makeup.blush" && portrait_side_value(settings,"blushMaterialVersion",side==0).toInt()<2)desired*=1+params["makeup.blushGloss"].toDouble()*.0015;pixel[c]+=float((desired*pixel[3]-pixel[c])*w*zone_weight*(root=="makeup.lipBleed"?.22:pigment?.85:.6));}
                    }
                }}
            }
        }
    }
    for(const auto& value:people)base=portrait_nails_region(std::move(base),source_size,value.toObject(),region,amount,effect_mask,cancelled);
    for(const auto& value:people)base=portrait_tattoo_region(std::move(base),source_size,value.toObject(),region,amount,effect_mask,cancelled);
    for(const auto& value:people)base=portrait_paint_region(std::move(base),source_size,value.toObject(),region,amount,effect_mask,cancelled);
    for(const auto& value:people)base=portrait_decals_region(std::move(base),source_size,value.toObject(),region,amount,effect_mask,cancelled);
    return base;
}
}
