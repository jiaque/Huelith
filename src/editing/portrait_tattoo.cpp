#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/project.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
QPainterPath portrait_tattoo_path(const QString& material){
    // Original small vector designs; the UI cards render these exact paths.
    QPainterPath line,fill;
    if(material=="wave"){
        for(int i=0;i<3;++i){const double y=(i-1)*.32;line.moveTo(-.82,y);line.cubicTo(-.45,y-.5,-.1,y+.5,.25,y);line.cubicTo(.45,y-.3,.62,y-.25,.82,y-.12);}
    }else if(material=="compass"){
        line.addEllipse(QPointF{},.5,.5);for(int i=0;i<4;++i){const double a=i*1.5707963267948966;const QPointF axis(std::sin(a),-std::cos(a)),side(-axis.y(),axis.x());QPolygonF ray;ray<<axis*.88<<side*.13<<QPointF{};fill.addPolygon(ray);fill.closeSubpath();line.moveTo(axis*.88);line.lineTo(-side*.13);line.lineTo(QPointF{});}
    }else{
        line.moveTo(-.3,.88);line.cubicTo(-.24,.35,.2,-.2,.05,-.85);
        for(int i=0;i<6;++i){const double t=.14+i*.13,u=1-t,x=u*u*u*(-.3)+3*u*u*t*(-.24)+3*u*t*t*.2+t*t*t*.05,y=u*u*u*.88+3*u*u*t*.35+3*u*t*t*(-.2)+t*t*t*(-.85),side=i%2?1:-1;QPainterPath leaf;leaf.moveTo(x,y);leaf.cubicTo(x+side*.33,y+.04,x+side*.54,y-.14,x+side*.48,y-.33);leaf.cubicTo(x+side*.2,y-.3,x+side*.05,y-.12,x,y);fill.addPath(leaf);}
    }
    QPainterPathStroker stroke;stroke.setWidth(.035);stroke.setCapStyle(Qt::RoundCap);stroke.setJoinStyle(Qt::RoundJoin);fill.addPath(stroke.createStroke(line));fill.setFillRule(Qt::WindingFill);return fill;
}
QTransform portrait_tattoo_transform(const QJsonObject& person,QSize size){
    const auto q=person["settings"].toObject()["tattooQuad"].toArray();if(q.size()!=4)return QTransform(0,0,0,0,0,0);QPolygonF quad;
    for(const auto& v:q){const auto a=v.toArray();if(a.size()!=2 || !a[0].isDouble() || !a[1].isDouble())return QTransform(0,0,0,0,0,0);const double x=a[0].toDouble(),y=a[1].toDouble();if(!std::isfinite(x) || !std::isfinite(y) || x<0 || x>1 || y<0 || y>1)return QTransform(0,0,0,0,0,0);quad<<QPointF(x*size.width(),y*size.height());}
    double sign=0;for(int i=0;i<4;++i){const auto a=quad[(i+1)%4]-quad[i],b=quad[(i+2)%4]-quad[(i+1)%4];const double cross=a.x()*b.y()-a.y()*b.x();if(std::abs(cross)<1e-5 || (i && sign*cross<=0))return QTransform(0,0,0,0,0,0);sign=cross;}
    QTransform result;if(!QTransform::quadToQuad(QPolygonF{{-1,-1},{1,-1},{1,1},{-1,1}},quad,result))return QTransform(0,0,0,0,0,0);return result;
}
namespace {
QImage decoded(const QJsonObject& obj,const char* key){return QImage::fromData(QByteArray::fromBase64(obj[key].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);}
double sample(const QImage& im,int x,int y,QSize size,double missing){if(im.isNull())return missing;const double u=std::clamp((x+.5)*im.width()/size.width()-.5,0.,double(im.width()-1)),v=std::clamp((y+.5)*im.height()/size.height()-.5,0.,double(im.height()-1));const int ix=int(u),iy=int(v),jx=std::min(ix+1,im.width()-1),jy=std::min(iy+1,im.height()-1);return ((im.constScanLine(iy)[ix]*(1-u+ix)+im.constScanLine(iy)[jx]*(u-ix))*(1-v+iy)+(im.constScanLine(jy)[ix]*(1-u+ix)+im.constScanLine(jy)[jx]*(u-ix))*(v-iy))/255.;}
QImage field(const QJsonObject& person,QSize size,QRect& box){
    const auto transform=portrait_tattoo_transform(person,size);if(!transform.isInvertible())return {};
    const auto settings=person["settings"].toObject();const double bend=std::clamp(settings["parameters"].toObject()["makeup.tattooCurve"].toDouble()*.013,0.,1.3);
    QPainterPath warped;warped.setFillRule(Qt::WindingFill);QTransform flatten;flatten.scale(512,512);
    for(const auto& polygon:portrait_tattoo_path(settings["tattooMaterial"].toString("leaf")).toSubpathPolygons(flatten)){
        QPolygonF output;for(const auto& p:polygon){const double u=p.x()/512,v=p.y()/512;output<<transform.map(QPointF(bend<1e-5?u:std::sin(bend*u)/std::sin(bend),v));}warped.addPolygon(output);warped.closeSubpath();
    }
    box=warped.boundingRect().adjusted(-2,-2,2,2).toAlignedRect().intersected(QRect(QPoint{},size));if(box.isEmpty())return {};
    auto result=allocate_image(box.size(),QImage::Format_RGB32);result.fill(Qt::black);QPainter p(&result);p.setRenderHint(QPainter::Antialiasing);p.translate(-box.topLeft());p.setPen(Qt::NoPen);p.setBrush(Qt::white);p.drawPath(warped);p.end();return result.convertToFormat(QImage::Format_Grayscale8);
}
bool allowed(const QJsonObject& person){return !person["unmatched"].toBool() && !person["matchAmbiguous"].toBool() && (!person["bodyAmbiguous"].toBool() || person["settings"].toObject()["partialSkin"].toBool());}
}
QImage portrait_tattoo_coverage(const QJsonObject& person,QSize size){
    auto result=allocate_image(size,QImage::Format_Grayscale8);result.fill(0);if(!allowed(person))return result;QRect box;const auto ink=field(person,size,box);const auto skin=decoded(person["settings"].toObject()["automaticMasks"].toObject(),"bodySkin");if(ink.isNull() || skin.isNull())return result;
    for(int y=0;y<box.height();++y)for(int x=0;x<box.width();++x)result.scanLine(y+box.y())[x+box.x()]=uchar(std::lround(ink.constScanLine(y)[x]*sample(skin,x+box.x(),y+box.y(),size,0)));return result;
}
QImage portrait_tattoo_region(QImage base,QSize size,const QJsonObject& person,QRect region,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled){
    const auto s=person["settings"].toObject();if(!allowed(person) || !s["enabled"].toObject()["makeup"].toBool(true) || s["disabledParameters"].toObject()["makeup.tattoo"].toBool())return base;
    const double strength=std::clamp(s["parameters"].toObject()["makeup.tattoo"].toDouble()*.01*amount*s["strength"].toDouble(100)*.01,0.,1.);if(strength==0)return base;
    const auto skin=decoded(s["automaticMasks"].toObject(),"bodySkin");if(skin.isNull())return base;QRect box;const auto ink=field(person,size,box);if(ink.isNull())return base;const auto draw=box.intersected(region);const auto erase=decoded(s["masks"].toObject(),"makeup.tattoo");auto color=s["colors"].toObject()["makeup.tattoo"].toArray();if(color.size()!=3)color={.03,.035,.04};
    for(int y=draw.top();y<=draw.bottom();++y){if(cancelled && cancelled->load())throw EditError("纹身材质已取消");auto* row=reinterpret_cast<float*>(base.scanLine(y-region.y()));for(int x=draw.left();x<=draw.right();++x){const double a=strength*ink.constScanLine(y-box.y())[x-box.x()]/255.*sample(skin,x,y,size,0)*sample(erase,x,y,size,1)*sample(effect_mask,x,y,size,1);if(a==0)continue;auto* pixel=row+(x-region.x())*4;
        // Absorptive pigment changes transmission instead of painting a flat RGB
        // decal. Original skin shading, texture and premultiplied alpha survive.
        for(int c=0;c<3;++c)pixel[c]*=float(1-a*.87*(1-std::clamp(color[c].toDouble(),0.,1.)));
    }}return base;
}
}
