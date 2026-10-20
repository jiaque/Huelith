#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace huelith::editing {
namespace {
double coverage(const QImage& image,int x,int y,QSize size) {
    if(image.isNull())return 0;
    return image.constScanLine(std::clamp(y*image.height()/size.height(),0,image.height()-1))
        [std::clamp(x*image.width()/size.width(),0,image.width()-1)]/255.;
}
std::uint32_t hash(std::uint32_t value) {
    value^=value>>16;value*=0x7feb352dU;value^=value>>15;value*=0x846ca68bU;return value^(value>>16);
}
}
QImage portrait_paint_region(QImage base,QSize size,const QJsonObject& person,QRect region,
                            double amount,const QImage& effect,const std::atomic_bool* cancelled) {
    const auto settings=person["settings"].toObject(),params=settings["parameters"].toObject();
    if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool() ||
       !settings["enabled"].toObject()["paint"].toBool(true))return base;
    const QStringList keys{"paint.highlight","paint.shadow","paint.makeup","paint.skin","paint.glitter","paint.light","paint.dark"};
    std::array<QImage,7> masks;std::array<double,7> strengths{};
    bool active=false;
    for(int i=0;i<7;++i) {
        strengths[i]=settings["disabledParameters"].toObject()[keys[i]].toBool()?0:
            std::clamp(params[keys[i]].toDouble()*.01,0.,1.);
        active|=strengths[i]>0;
        if(strengths[i]>0)masks[i]=portrait_effect_mask(person,keys[i],size.scaled(768,768,Qt::KeepAspectRatio));
    }
    if(!active)return base;
    const auto colors=settings["colors"].toObject();
    const auto pigment=colors["paint.makeup"].toArray(),skin=colors["paint.skin"].toArray(),glitter=colors["paint.glitter"].toArray();
    const double strength=amount*std::clamp(settings["strength"].toDouble(100)*.01,0.,1.);
    const double protect=std::clamp(params["paint.protect"].toDouble(80)*.01,0.,1.);
    const double faceWidth=std::max(.02,(person["face"].toArray().size()==4?person["face"].toArray()[2].toDouble():.2));
    // Grain locations are tied to source coordinates and the saved seed. Amount
    // affects opacity only; preview resolution and ROI never alter the pattern.
    const double spacing=std::max(1.,size.width()*faceWidth*.025);
    const double grainRadius=.08+.20*params["paint.grainSize"].toDouble(35)*.01;
    const double density=std::clamp(params["paint.grainDensity"].toDouble(35)*.01,0.,1.);
    const auto seed=std::uint32_t(settings["paintSeed"].toDouble(173));
    for(int y=region.top();y<=region.bottom();++y) {
        if(cancelled && cancelled->load())throw EditError("修容笔已取消");
        auto* row=reinterpret_cast<float*>(base.scanLine(y-region.y()));
        for(int x=region.left();x<=region.right();++x) {
            auto* pixel=row+(x-region.x())*4;if(pixel[3]<.00001F)continue;
            const double global=strength*(effect.isNull()?1:coverage(effect,x,y,size));
            std::array<double,7> w{};for(int i=0;i<7;++i)w[i]=coverage(masks[i],x,y,size)*strengths[i]*global;
            const double luminance=(.2627*pixel[0]+.678*pixel[1]+.0593*pixel[2])/pixel[3];
            const double highlightGuard=1-std::clamp((luminance-.45)/.55,0.,1.)*protect;
            const double exposure=(w[0]*.55-w[1]*.65+w[5]*(1-w[6])-w[6])*highlightGuard;
            for(int c=0;c<3;++c)pixel[c]=float(pixel[c]*std::exp2(exposure));
            const double light=(.2627*pixel[0]+.678*pixel[1]+.0593*pixel[2])/pixel[3];
            for(int kind=0;kind<2;++kind) {
                const auto tint=kind?skin:pigment;if(tint.size()!=3)continue;
                const double target=.2627*tint[0].toDouble()+.678*tint[1].toDouble()+.0593*tint[2].toDouble();
                for(int c=0;c<3;++c) {
                    // Skin color changes chroma only; cosmetic pigment also
                    // covers some of the underlying tone, retaining texture.
                    const double desired=tint[c].toDouble()*(kind?light/std::max(.001,target):std::pow(std::max(.001,light)/.25,.8));
                    pixel[c]+=float((desired*pixel[3]-pixel[c])*w[kind?3:2]*(kind?.45:.55));
                    if(!kind)pixel[c]+=float(std::max(0.,light-.4)*.15*w[2]*params["paint.gloss"].toDouble()*.01*pixel[3]);
                }
            }
            if(w[4]>0) {
                const int gx=int(std::floor(x/spacing)),gy=int(std::floor(y/spacing));
                const auto h=hash(std::uint32_t(gx)*73856093U ^ std::uint32_t(gy)*19349663U ^ seed);
                if(double(h&65535)/65535<density) {
                    const double cx=(gx+.2+((h>>16)&255)/255.*.6)*spacing,cy=(gy+.2+((h>>24)&255)/255.*.6)*spacing;
                    const double d=std::hypot(x+.5-cx,y+.5-cy)/std::max(.6,spacing*grainRadius);
                    if(d<1)for(int c=0;c<3;++c) {
                        const double tint=glitter.size()==3?glitter[c].toDouble():1.;
                        pixel[c]+=float(std::max(0.,tint*pixel[3]-pixel[c])*w[4]*(1-d)*(1-d)*.85);
                    }
                }
            }
        }
    }
    return base;
}
}
