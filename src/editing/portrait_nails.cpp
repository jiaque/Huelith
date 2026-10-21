#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/project.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
QPainterPath portrait_nail_path(const QJsonObject& nail,QSize size){
    QPainterPath path;const auto points=nail["outline"].toArray();
    if(points.size()<3 || points.size()>2048 || !nail["enabled"].toBool(true))return path;
    QPolygonF contour;for(int i=0;i<points.size();++i){const auto p=points[i].toArray();if(p.size()!=2 || !p[0].isDouble() || !p[1].isDouble())return {};
        const double x=p[0].toDouble(),y=p[1].toDouble();if(!std::isfinite(x) || !std::isfinite(y) || x<0 || x>1 || y<0 || y>1)return {};
        contour<<QPointF(x*size.width(),y*size.height());
    }path.moveTo((contour.last()+contour.first())*.5);for(int i=0;i<contour.size();++i)path.quadTo(contour[i],(contour[i]+contour[(i+1)%contour.size()])*.5);path.closeSubpath();return path;
}
namespace {
// Always rasterize the same source-space bounding box, including when rendering
// a tile. This keeps antialiasing independent of tile boundaries.
QImage raster(const QPainterPath& path,QSize size,QRect& box){
    box=path.boundingRect().adjusted(-1,-1,1,1).toAlignedRect().intersected(QRect(QPoint{},size));if(path.isEmpty() || box.isEmpty())return {};
    auto image=allocate_image(box.size(),QImage::Format_RGB32);image.fill(Qt::black);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.translate(-box.topLeft());p.setPen(Qt::NoPen);p.setBrush(Qt::white);p.drawPath(path);p.end();return image.convertToFormat(QImage::Format_Grayscale8);
}
QImage nail_raster(const QJsonObject& nail,QSize size,QRect& box){
    if(!nail["enabled"].toBool(true))return {};
    if(!nail["mask"].toString().isEmpty()){
        auto mask=QImage::fromData(QByteArray::fromBase64(nail["mask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
        if(mask.isNull() || mask.width()>2048 || mask.height()>2048)return {};
        // Use the same complete source mask for tiled and full-image rendering.
        box=portrait_nail_path(nail,size).boundingRect().adjusted(-2,-2,2,2).toAlignedRect().intersected(QRect(QPoint{},size));
        if(box.isEmpty())return {};
        QImage local(box.size(),QImage::Format_Grayscale8);for(int y=0;y<box.height();++y)for(int x=0;x<box.width();++x)local.scanLine(y)[x]=mask.constScanLine(std::clamp(int((y+box.y()+.5)*mask.height()/size.height()),0,mask.height()-1))[std::clamp(int((x+box.x()+.5)*mask.width()/size.width()),0,mask.width()-1)];return local;
    }return raster(portrait_nail_path(nail,size),size,box);
}
double coverage(const QImage& mask,int x,int y,QSize size){if(mask.isNull())return 1;return mask.constScanLine(std::clamp(int(double(y)*mask.height()/size.height()),0,mask.height()-1))[std::clamp(int(double(x)*mask.width()/size.width()),0,mask.width()-1)]/255.;}
}
QImage portrait_nail_coverage(const QJsonObject& person,QSize size){
    auto result=allocate_image(size,QImage::Format_Grayscale8);result.fill(0);const auto nails=person["settings"].toObject()["nails"].toArray();
    for(int i=0;i<std::min(40,int(nails.size()));++i){QRect box;const auto mask=nail_raster(nails[i].toObject(),size,box);if(mask.isNull())continue;
        for(int y=0;y<box.height();++y)for(int x=0;x<box.width();++x)result.scanLine(y+box.y())[x+box.x()]=std::max(result.constScanLine(y+box.y())[x+box.x()],mask.constScanLine(y)[x]);
    }return result;
}
QImage portrait_nails_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled){
    const auto s=person["settings"].toObject(),params=s["parameters"].toObject();
    if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool() || !s["enabled"].toObject()["makeup"].toBool(true) || s["disabledParameters"].toObject()["makeup.nails"].toBool())return base;
    const double opacity=std::clamp(params["makeup.nails"].toDouble()*.01*amount*s["strength"].toDouble(100)*.01,0.,1.);if(opacity==0)return base;
    const auto nails=s["nails"].toArray();const auto correction=QImage::fromData(QByteArray::fromBase64(s["masks"].toObject()["makeup.nails"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
    for(int i=0;i<std::min(40,int(nails.size()));++i){if(cancelled && cancelled->load())throw EditError("美甲已取消");const auto nail=nails[i].toObject();QRect box;const auto mask=nail_raster(nail,size,box);const auto intersection=box.intersected(region);if(mask.isNull() || intersection.isEmpty())continue;
        auto color=nail["color"].toArray();if(color.size()!=3)color={.32,.018,.035};const double reference=std::clamp(nail["referenceLuminance"].toDouble(.25),.001,4.);
        for(int y=intersection.top();y<=intersection.bottom();++y){if(cancelled && cancelled->load())throw EditError("美甲已取消");auto* row=reinterpret_cast<float*>(base.scanLine(y-region.y()));
            for(int x=intersection.left();x<=intersection.right();++x){const double a=opacity*mask.constScanLine(y-box.y())[x-box.x()]/255.*coverage(correction,x,y,size)*coverage(effect_mask,x,y,size);if(a==0)continue;auto* pixel=row+(x-region.x())*4;if(pixel[3]<.00001F)continue;
                const double l=(.2627*pixel[0]+.678*pixel[1]+.0593*pixel[2])/pixel[3];
                // Original illumination supplies curvature and fine surface texture.
                // Only existing bright reflections are retained; no painted fake light.
                const bool calibrated=nail["materialVersion"].toInt()>=2;
                const double shade=std::pow(std::clamp(l/reference,0.,calibrated?1.45:4.),.65);
                const double reflection=calibrated?std::clamp((l-reference)/std::max(.05,1-reference),0.,1.):std::clamp((l/reference-1.05)/.35,0.,1.)*std::clamp(l,0.,1.);
                const double highlight=reflection*std::clamp(params["makeup.nailsGloss"].toDouble()*.01,0.,1.);
                for(int c=0;c<3;++c){const double pigment=std::clamp(color[c].toDouble(),0.,1.)*shade;const double desired=std::min(1.,pigment*(1-highlight)+highlight);pixel[c]+=float((desired*pixel[3]-pixel[c])*a);}
            }
        }
    }return base;
}
}
