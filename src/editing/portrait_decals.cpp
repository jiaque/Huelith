#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QPolygonF>
#include <QTransform>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
namespace huelith::editing {
namespace {
QImage texture(const QString& hash,const QString& encoded,int extent){
    static QMutex mutex;static QCache<QString,QImage> cache(192*1024);const QString key=hash+":"+QString::number(extent);
    {QMutexLocker lock(&mutex);if(auto* found=cache.object(key))return *found;}
    QByteArray bytes;if(encoded.startsWith("resource:")){QFile file(encoded.mid(9));if(!file.open(QIODevice::ReadOnly))return {};bytes=file.readAll();}else bytes=QByteArray::fromBase64(encoded.toLatin1());QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");const QSize original=reader.size();if(original.isEmpty() || qint64(original.width())*original.height()>64000000)return {};
    if(std::max(original.width(),original.height())>extent)reader.setScaledSize(original.scaled(extent,extent,Qt::KeepAspectRatio));auto image=reader.read();if(image.isNull())return {};if(!image.colorSpace().isValid())image.setColorSpace(QColorSpace::SRgb);image=to_working(image);
    {QMutexLocker lock(&mutex);cache.insert(key,new QImage(image),int(image.sizeInBytes()/1024));}return image;
}
double mask_at(const QImage& mask,int x,int y,QSize size){if(mask.isNull())return 1;return mask.constScanLine(std::clamp(y*mask.height()/size.height(),0,mask.height()-1))[std::clamp(x*mask.width()/size.width(),0,mask.width()-1)]/255.;}
}
QImage portrait_decals_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect,const std::atomic_bool* cancelled){
    const auto settings=person["settings"].toObject();if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool())return base;const auto assets=settings["decalAssets"].toObject();
    for(const auto& value:settings["decals"].toArray()){
        const auto item=value.toObject();if(!item["enabled"].toBool(true))continue;const QString category=item["category"].toString("hair");if(!settings["enabled"].toObject()[category=="tattoo"?"makeup":category].toBool(true))continue;
        const auto quad=item["quad"].toArray();if(quad.size()!=4)continue;QPolygonF dest;for(const auto& v:quad){const auto a=v.toArray();if(a.size()!=2)return base;dest<<QPointF(a[0].toDouble()*size.width(),a[1].toDouble()*size.height());}QTransform transform;if(!QTransform::quadToQuad(QPolygonF{{0,0},{1,0},{1,1},{0,1}},dest,transform))continue;bool invertible;auto inverse=transform.inverted(&invertible);if(!invertible)continue;
        const auto bounds=dest.boundingRect().toAlignedRect().intersected(region);if(bounds.isEmpty())continue;int extent=std::clamp(int(std::ceil(std::max(dest.boundingRect().width(),dest.boundingRect().height())*1.5)),128,4096);extent=((extent+127)/128)*128;
        const auto image=texture(item["asset"].toString(),assets[item["asset"].toString()].toString(),extent);if(image.isNull())continue;const auto mask=QImage::fromData(QByteArray::fromBase64(settings["masks"].toObject()["decal."+item["id"].toString()].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);const auto tint=item["tint"].toArray();const double opacity=std::clamp(item["opacity"].toDouble(1)*amount*settings["strength"].toDouble(100)*.01,0.,1.);
        const auto maskSize=size.scaled(1024,1024,Qt::KeepAspectRatio);const auto skin=category=="makeup"?portrait_style_mask(person,"makeup.foundation",maskSize):category=="tattoo"?QImage::fromData(QByteArray::fromBase64(settings["automaticMasks"].toObject()["bodySkin"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8):QImage{};
        for(int y=bounds.top();y<=bounds.bottom();++y){if(cancelled && cancelled->load())throw EditError("贴图已取消");auto* row=reinterpret_cast<float*>(base.scanLine(y-region.y()));for(int x=bounds.left();x<=bounds.right();++x){auto uv=inverse.map(QPointF(x+.5,y+.5));if(uv.x()<0 || uv.x()>1 || uv.y()<0 || uv.y()>1)continue;const double tx=uv.x()*(image.width()-1),ty=uv.y()*(image.height()-1);const int ix=int(tx),iy=int(ty),jx=std::min(ix+1,image.width()-1),jy=std::min(iy+1,image.height()-1);const double fx=tx-ix,fy=ty-iy;const auto* a=reinterpret_cast<const float*>(image.constScanLine(iy));const auto* b=reinterpret_cast<const float*>(image.constScanLine(jy));double color[4];for(int c=0;c<4;++c)color[c]=(a[ix*4+c]*(1-fx)+a[jx*4+c]*fx)*(1-fy)+(b[ix*4+c]*(1-fx)+b[jx*4+c]*fx)*fy;
            const double w=opacity*mask_at(mask,x,y,size)*mask_at(effect,x,y,size)*((category=="makeup"||category=="tattoo")?(skin.isNull()?0:mask_at(skin,x,y,size)):1);auto* pixel=row+(x-region.x())*4;const double alpha=color[3]*w;for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*(1-alpha)+color[c]*w*(category=="tattoo"?pixel[c]:pixel[3])*(tint.size()==3?tint[c].toDouble(1):1));
        }}
    }return base;
}
}
