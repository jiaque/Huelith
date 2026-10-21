#include "portrait_back.h"
#include "portrait_straight_leg.h"
#include "portrait_leg_length.h"
#include "portrait_height.h"
#include "portrait_manual_height.h"
#include "portrait_manual_slim.h"
#include "portrait_manual_scale.h"
#include "portrait_manual_distort.h"
#include "portrait_neck.h"
#include "portrait_neck_length.h"
#include "portrait_shoulder_width.h"
#include "portrait_shoulder_contour.h"
#include "portrait_abdomen.h"
#include "portrait_chest.h"
#include "portrait_pelvis.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/portrait.h"
#include "huelith/editing/project.h"
#include "huelith/editing/liquify.h"
#include "huelith/editing/image_storage.h"
#include "portrait_contour.h"
#include "portrait_orientation.h"
#include "portrait_limb.h"
#include <opencv2/imgproc.hpp>
#include <QLineF>
#include <QBuffer>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

namespace huelith::editing {
QJsonValue portrait_side_value(const QJsonObject& settings,const QString& key,bool left){
    const auto side=key+(left?".left":".right");return settings.contains(side)?settings.value(side):settings.value(key);
}
void portrait_set_side_value(QJsonObject& settings,const QString& key,const QJsonValue& value){
    if(settings.value("linked").toBool(true)){settings[key]=value;settings.remove(key+".left");settings.remove(key+".right");}
    else settings[key+(settings.value("side").toInt()==0?".left":".right")]=value;
}
void portrait_set_style_color(QJsonObject& settings,const QString& key,const QJsonValue& value){
    if(key=="hair.accessory")settings["hairAccessoryVersion"]=2;
    if(key=="hair.color" || key=="hair.roots" || key.startsWith("hair.zone_"))settings["hairColorVersion"]=2;
    auto colors=settings.value("colors").toObject();const auto parameters=portrait_parameters(11);
    const auto found=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.key==key;});
    if(found!=parameters.end() && found->bilateral){
        if(settings.value("linked").toBool(true)){colors[key]=value;colors.remove(key+".left");colors.remove(key+".right");}
        else colors[key+(settings.value("side").toInt()==0?".left":".right")]=value;
    }else colors[key]=value;settings["colors"]=colors;
}

std::vector<PortraitParameter> portrait_parameters(int panel) {
    switch(panel) {
    case 0:return {{"face.small",QStringLiteral("小脸"),false,0,100},{"face.vShape",QStringLiteral("V 脸"),false,0,100},{"face.width",QStringLiteral("脸型")},{"face.cheek",QStringLiteral("颧骨")},{"face.jaw",QStringLiteral("下颌")},{"face.jawAngle",QStringLiteral("下颌角")},{"face.chin",QStringLiteral("下巴")},{"face.chinWidth",QStringLiteral("下巴宽窄")},{"face.smoothContour",QStringLiteral("轮廓流畅"),false,0,100},{"face.forehead",QStringLiteral("额头")},{"face.foreheadFill",QStringLiteral("额头丰盈（明暗）"),false,0,100},{"face.templeFill",QStringLiteral("太阳穴丰盈"),true,0,100},{"face.tearFill",QStringLiteral("泪沟丰盈"),true,0,100},{"face.malarShape",QStringLiteral("苹果肌（形态）"),true,0,100},{"face.malarTone",QStringLiteral("苹果肌（明暗）"),true,0,100},{"face.volume",QStringLiteral("面颊丰盈（轮廓）")},{"face.cheekTone",QStringLiteral("面颊丰盈（明暗）"),true,0,100},{"face.contourLift",QStringLiteral("轮廓提拉"),true,0,100},{"face.foreheadLift",QStringLiteral("额头提拉"),false,0,100},{"face.orbitalLift",QStringLiteral("眼周提拉（形态）"),true,0,100},{"face.orbitalTone",QStringLiteral("眼周提拉（明暗）"),true,0,100},{"face.midfaceLift",QStringLiteral("面中提拉"),true,0,100},{"face.perioralLift",QStringLiteral("口周提拉（形态）"),true,0,100},{"face.perioralTone",QStringLiteral("口周提拉（明暗）"),true,0,100},{"face.doubleChin",QStringLiteral("双下巴")}};
    case 1:return {{"feature.eyeSize",QStringLiteral("眼睛大小"),true},{"feature.eyeSpace",QStringLiteral("眼睛间距"),true},{"feature.eyeHeight",QStringLiteral("眼睛位置"),true},{"feature.eyeTilt",QStringLiteral("眼睛倾斜"),true},{"feature.eyeLength",QStringLiteral("眼睛长度"),true},{"feature.eyeAperture",QStringLiteral("眼高"),true},{"feature.eyeUpperLift",QStringLiteral("上睑提肌"),true,0,100},{"feature.eyeLowerLid",QStringLiteral("眼睑下至"),true,0,100},{"feature.eyeOuterLift",QStringLiteral("眼尾上扬"),true},{"feature.eyeInnerCorner",QStringLiteral("内眼角"),true},{"feature.eyeOuterCorner",QStringLiteral("外眼角"),true},{"feature.noseSize",QStringLiteral("鼻子大小")},{"feature.noseRootWidth",QStringLiteral("山根宽度")},{"feature.noseBridgeWidth",QStringLiteral("鼻梁宽度")},{"feature.noseTipWidth",QStringLiteral("鼻尖宽度")},{"feature.noseWidth",QStringLiteral("鼻宽")},{"feature.noseLength",QStringLiteral("鼻长")},{"feature.lipSize",QStringLiteral("嘴唇大小")},{"feature.lipThickness",QStringLiteral("嘴唇厚度")},{"feature.lipWidth",QStringLiteral("嘴宽")},{"feature.lipHeight",QStringLiteral("嘴部上下位置")},{"feature.lipOffset",QStringLiteral("嘴部左右位置")},{"feature.lipTilt",QStringLiteral("嘴部倾斜")},{"feature.lipUpper",QStringLiteral("上唇厚度")},{"feature.lipLower",QStringLiteral("下唇厚度")},{"feature.lipCupid",QStringLiteral("唇峰形状")},{"feature.browHeight",QStringLiteral("眉毛位置"),true},{"feature.browThickness",QStringLiteral("眉毛粗细"),true},{"feature.browTilt",QStringLiteral("眉毛倾斜"),true},{"feature.browLength",QStringLiteral("眉毛长度"),true},{"feature.browSpace",QStringLiteral("眉间距"),true},{"feature.browHeadSpace",QStringLiteral("眉头间距"),true},{"feature.browArchHeight",QStringLiteral("眉峰高度"),true},{"feature.browArchPosition",QStringLiteral("眉峰位置"),true},{"feature.smile",QStringLiteral("嘴角"),true}};
    case 2:return {{"head.yaw",QStringLiteral("左右朝向")},{"head.pitch",QStringLiteral("上下朝向")},{"head.size",QStringLiteral("头部大小")},{"head.roll",QStringLiteral("头部倾斜")},{"head.crown",QStringLiteral("颅顶高度")},{"head.hairVolume",QStringLiteral("头包脸")},{"head.hairline",QStringLiteral("发际线位置")},{"head.midface",QStringLiteral("中庭")},{"head.philtrum",QStringLiteral("人中")},{"head.lowerface",QStringLiteral("下庭")}};
    case 3:return {{"body.manualRotate",QStringLiteral("局部旋转")},{"body.manualShear",QStringLiteral("局部剪切")},{"body.manualScale",QStringLiteral("手动缩放")},{"body.manualSlim",QStringLiteral("手动瘦身")},{"body.manualHeight",QStringLiteral("手动增高")},{"body.hipWidth",QStringLiteral("臀胯宽度"),true},{"body.chest",QStringLiteral("胸部轮廓"),true},{"body.squareShoulder",QStringLiteral("直角肩"),true,0,100},{"body.shoulderWidth",QStringLiteral("肩宽"),true},{"body.shoulder",QStringLiteral("肩线"),true},{"body.neck",QStringLiteral("颈部粗细")},{"body.neckLength",QStringLiteral("颈部长度")},{"body.trapezius",QStringLiteral("斜方肌"),true},{"body.waist",QStringLiteral("腰部")},{"body.abdomen",QStringLiteral("收腹"),false,0,100},{"body.belly",QStringLiteral("局部瘦肚子"),false,0,100},{"body.back",QStringLiteral("改善驼背"),false,0,100},{"body.arm",QStringLiteral("手臂"),true},{"body.upperArm",QStringLiteral("大臂"),true},{"body.legWidth",QStringLiteral("腿部"),true},{"body.thigh",QStringLiteral("大腿"),true},{"body.calf",QStringLiteral("小腿"),true},{"body.straightLeg",QStringLiteral("直腿"),true,0,100},{"body.legLength",QStringLiteral("腿长"),true},{"body.height",QStringLiteral("身高")},{"head.size",QStringLiteral("头身比例")}};
    case 5:return {{"skin.smooth",QStringLiteral("面部磨皮"),false,0,100},{"skin.bodySmooth",QStringLiteral("身体磨皮"),false,0,100},{"skin.retention",QStringLiteral("纹理保留"),false,0,100},{"skin.texture",QStringLiteral("纹理")},{"skin.uniform",QStringLiteral("均匀度"),false,0,100},{"skin.oil",QStringLiteral("油光抑制"),false,0,100},{"skin.oilGloss",QStringLiteral("保留光泽"),false,0,100}};
    case 6:return {{"color.light",QStringLiteral("明暗")},{"color.warmth",QStringLiteral("冷暖")},{"color.rosy",QStringLiteral("红润")},{"color.redness",QStringLiteral("去泛红"),false,0,100},{"color.balance",QStringLiteral("脸颈身体协调"),false,0,100},{"color.custom",QStringLiteral("自定义肤色"),false,0,100}};
    case 7:return {{"repair.conceal",QStringLiteral("遮瑕颜色协调"),false,0,100}};
    case 9:{std::vector<PortraitParameter> result{{"eye.circles",QStringLiteral("黑眼圈淡化"),true,0,100},{"eye.bags",QStringLiteral("眼袋阴影淡化"),true,0,100},{"eye.white",QStringLiteral("眼白亮度"),true,0,100},{"eye.red",QStringLiteral("红眼修正"),true,0,100},{"eye.deRed",QStringLiteral("眼白去红"),true,0,100}};for(const auto& parameter:portrait_parameters(11))if(parameter.key.startsWith("makeup.iris") || parameter.key.startsWith("makeup.catch"))result.push_back(parameter);return result;}
    case 10:return {{"teeth.yellow",QStringLiteral("去黄"),false,0,100},{"teeth.light",QStringLiteral("提亮"),false,0,100},{"teeth.natural",QStringLiteral("自然色"),false,-100,100}};
    case 11:return {{"makeup.shadowFeather",QStringLiteral("边缘柔化"),true,0,100},{"makeup.linerWidth",QStringLiteral("眼线宽度"),true,-100,100},{"makeup.linerTail",QStringLiteral("眼尾长度"),true,-100,100},{"makeup.lashLength",QStringLiteral("睫毛长度"),true,-100,100},{"makeup.lashCurl",QStringLiteral("卷翘"),true,-100,100},{"makeup.aegyosalY",QStringLiteral("卧蚕位置"),true,-100,100},{"makeup.aegyosalWidth",QStringLiteral("卧蚕宽度"),true,-100,100},{"makeup.irisX",QStringLiteral("虹膜水平位置"),true,-100,100},{"makeup.irisY",QStringLiteral("虹膜垂直位置"),true,-100,100},{"makeup.irisSize",QStringLiteral("虹膜大小"),true,-100,100},{"makeup.irisFeather",QStringLiteral("虹膜边缘柔化"),true,0,100},{"makeup.tattoo",QStringLiteral("纹身浓度"),false,0,100},{"makeup.tattooCurve",QStringLiteral("曲面贴合"),false,0,100},{"makeup.nails",QStringLiteral("甲油浓度"),false,0,100},{"makeup.nailsGloss",QStringLiteral("保留原反光"),false,0,100},{"makeup.sticker",QStringLiteral("装饰浓度"),false,0,100},{"makeup.stickerSize",QStringLiteral("装饰大小"),false,0,100},{"makeup.stickerAngle",QStringLiteral("旋转")},{"makeup.stickerX",QStringLiteral("水平位置")},{"makeup.stickerY",QStringLiteral("垂直位置")},{"makeup.freckles",QStringLiteral("雀斑浓度"),false,0,100},{"makeup.frecklesSize",QStringLiteral("斑点大小"),false,0,100},{"makeup.frecklesDensity",QStringLiteral("斑点密度"),false,0,100},{"makeup.mole",QStringLiteral("装饰痣浓度"),false,0,100},{"makeup.moleSize",QStringLiteral("痣大小"),false,0,100},{"makeup.moleFeather",QStringLiteral("边缘柔和"),false,0,100},{"makeup.moleX",QStringLiteral("水平位置")},{"makeup.moleY",QStringLiteral("垂直位置")},{"makeup.foundation",QStringLiteral("底妆均匀度"),false,0,100},{"makeup.lip",QStringLiteral("口红强度"),false,0,100},{"makeup.lipCoverage",QStringLiteral("色素覆盖"),false,0,100},{"makeup.lipGloss",QStringLiteral("口红光泽／哑光")},{"makeup.lipFeather",QStringLiteral("唇缘晕染"),false,0,100},{ "makeup.lipBleed",QStringLiteral("唇外晕染"),false,0,100},{"makeup.lipBleedWidth",QStringLiteral("外溢宽度"),false,0,100},{"makeup.lipSparkle",QStringLiteral("细闪"),false,0,100},{"makeup.lipSparkleSize",QStringLiteral("粒子大小"),false,0,100},{"makeup.lipSparkleDensity",QStringLiteral("粒子疏密")},{"makeup.brow",QStringLiteral("眉色"),true,0,100},{"makeup.browShape",QStringLiteral("眉形浓淡"),true,0,100},{"makeup.blush",QStringLiteral("腮红"),true,0,100},{"makeup.blushSpread",QStringLiteral("腮红范围"),true,0,100},{"makeup.blushX",QStringLiteral("腮红水平位置"),true},{"makeup.blushY",QStringLiteral("腮红垂直位置"),true},{"makeup.blushGloss",QStringLiteral("腮红光泽／雾面"),true},{"makeup.blushSparkle",QStringLiteral("腮红细闪"),true,0,100},{"makeup.shadow",QStringLiteral("眼影底色"),true,0,100},{"makeup.shadowAccent",QStringLiteral("眼尾深色"),true,0,100},{"makeup.liner",QStringLiteral("上眼线"),true,0,100},{"makeup.linerLower",QStringLiteral("下眼线"),true,0,100},{"makeup.lash",QStringLiteral("睫毛"),true,0,100},{"makeup.aegyosal",QStringLiteral("卧蚕"),true,0,100},{"makeup.crease",QStringLiteral("双眼皮折痕"),true,0,100},{"makeup.iris",QStringLiteral("美瞳颜色"),true,0,100},{"makeup.irisTexture",QStringLiteral("虹膜纹理"),true,0,100},{"makeup.catchlight",QStringLiteral("眼神光"),true,0,100},{"makeup.catchX",QStringLiteral("眼神光水平位置"),true},{"makeup.catchY",QStringLiteral("眼神光垂直位置"),true},{"makeup.catchSize",QStringLiteral("眼神光大小"),true,0,100}};
    case 12:return {{"hair.color",QStringLiteral("染发强度"),false,0,100},{"hair.roots",QStringLiteral("发根补染"),false,0,100},{"hair.rootWidth",QStringLiteral("发根范围宽度"),false,0,100},{"hair.preserveGloss",QStringLiteral("保留光泽"),false,0,100},{"hair.shine",QStringLiteral("光泽"),false,0,100},{"hair.water",QStringLiteral("水光"),false,0,100},{"hair.detail",QStringLiteral("细节保留"),false,0,100},{"hair.smooth",QStringLiteral("局部柔顺"),false,0,100},{"hair.oil",QStringLiteral("减弱油亮"),false,0,100},{"head.hairVolume",QStringLiteral("头发蓬松度")},{"hair.accessory",QStringLiteral("发饰不透明度"),false,0,100},{"hair.accessoryRoughness",QStringLiteral("发饰哑光程度"),false,0,100},{"hair.accessoryX",QStringLiteral("发饰水平位置")},{"hair.accessoryY",QStringLiteral("发饰垂直位置")},{"hair.accessorySize",QStringLiteral("发饰大小"),false,0,100},{"hair.accessoryAngle",QStringLiteral("发饰旋转")}};
    case 13:return {{"paint.highlight",QStringLiteral("高光笔"),false,0,100},{"paint.shadow",QStringLiteral("阴影笔"),false,0,100},{"paint.makeup",QStringLiteral("彩妆笔"),false,0,100},{"paint.skin",QStringLiteral("肤色笔"),false,0,100},{"paint.glitter",QStringLiteral("亮片笔"),false,0,100},{"paint.light",QStringLiteral("提亮"),false,0,100},{"paint.dark",QStringLiteral("压暗"),false,0,100},{"paint.protect",QStringLiteral("高光保护"),false,0,100},{"paint.gloss",QStringLiteral("光泽"),false,0,100},{"paint.grainSize",QStringLiteral("粒径"),false,0,100},{"paint.grainDensity",QStringLiteral("密度"),false,0,100},{"contour.highlight",QStringLiteral("高光"),false,0,100},{"contour.shadow",QStringLiteral("阴影"),false,0,100},{"face.volume",QStringLiteral("面颊丰盈（轮廓，与面部共用）")},{"contour.paint",QStringLiteral("手绘上色"),false,0,100},{"contour.lines",QStringLiteral("身体线条"),false,0,100},{"contour.linesX",QStringLiteral("线条水平位置")},{"contour.linesY",QStringLiteral("线条垂直位置")},{"contour.linesWidth",QStringLiteral("线条宽度")},{"contour.linesHeight",QStringLiteral("线条长度")},{"contour.linesAngle",QStringLiteral("线条旋转")}};
    case 14:return {{"light.face",QStringLiteral("面部补光")},{"light.body",QStringLiteral("身体补光")},{"light.highlights",QStringLiteral("亮部保护"),false,0,100}};
    default:return {};
    }
}
std::optional<PortraitParameter> portrait_parameter(const QString& key) {
    static const auto descriptors=[] {
        std::vector<PortraitParameter> result;
        for(int panel=0;panel<=14;++panel)for(const auto& item:portrait_parameters(panel))
            if(std::none_of(result.begin(),result.end(),[&](const auto& saved){return saved.key==item.key;}))result.push_back(item);
        return result;
    }();
    const auto found=std::find_if(descriptors.begin(),descriptors.end(),[&](const auto& item){return item.key==key;});
    if(found==descriptors.end())return std::nullopt;
    return *found;
}
namespace {
struct Kernel { QPointF center,radius,shift; double sx{},sy{},angle{},axis{}; };
QPointF point(const QJsonArray& points,int index) {
    if(index<0 || index>=points.size())return {};
    const auto a=points[index].toArray();return {a[0].toDouble(),a[1].toDouble()};
}
QPointF mean(const QJsonArray& points,int first,int last) {
    QPointF result;for(int i=first;i<=last;++i)result+=point(points,i);return result/(last-first+1);
}
double mask_value(const QImage& mask,QPointF p) {
    if(mask.isNull())return 0;
    const int x=std::clamp(int(p.x()*mask.width()),0,mask.width()-1),y=std::clamp(int(p.y()*mask.height()),0,mask.height()-1);
    return mask.constScanLine(y)[x]/255.;
}
QImage mask_from(const QJsonObject& record,const char* key) {
    const auto text=record[key].toString();if(text.size()>8*1024*1024)throw EditError(QStringLiteral("人像范围数据过大"));
    if(text.isEmpty())return {};
    const auto image=QImage::fromData(QByteArray::fromBase64(text.toLatin1()),"PNG");
    if(image.isNull() || image.width()>4096 || image.height()>4096)throw EditError(QStringLiteral("人像范围数据损坏"));
    return image.convertToFormat(QImage::Format_Grayscale8);
}
}
QString portrait_mask_png(const QImage& image){QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");return QString::fromLatin1(bytes.toBase64());}
QTransform portrait_accessory_transform(const QJsonObject& person,const QJsonObject& params,QSize size){
    const auto face=person["face"].toArray();if(face.size()!=4)return {};const double fw=face[2].toDouble()*size.width(),fh=face[3].toDouble()*size.height();QTransform t;t.translate((face[0].toDouble()+face[2].toDouble()*.5)*size.width()+fw*(.35+params["hair.accessoryX"].toDouble()*.01),(face[1].toDouble()+face[3].toDouble()*.5)*size.height()+fh*(-.65+params["hair.accessoryY"].toDouble()*.01));t.rotate(params["hair.accessoryAngle"].toDouble()*1.8);const double scale=fw*(.12+params["hair.accessorySize"].toDouble()*.003);t.scale(scale,scale);return t;
}
QImage portrait_accessory_coverage(const QJsonObject& person,QSize size){
    const auto settings=person["settings"].toObject();auto mask=QImage::fromData(QByteArray::fromBase64(settings["masks"].toObject()["hair.accessory"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
    if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(255);return mask;}
    mask=mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(!settings["hairAccessoryMaskFollows"].toBool() || !settings.contains("hairAccessoryMaskAnchor"))return mask;
    const auto old=portrait_accessory_transform(person,settings["hairAccessoryMaskAnchor"].toObject(),size),current=portrait_accessory_transform(person,settings["parameters"].toObject(),size);bool valid=false;const auto inverse=old.inverted(&valid);if(!valid)return mask;
    QImage moved(size,QImage::Format_RGB32);moved.fill(Qt::white);QPainter painter(&moved);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.setTransform(inverse*current);painter.drawImage(QPoint(0,0),mask);painter.end();return moved.convertToFormat(QImage::Format_Grayscale8);
}
QImage portrait_effect_mask(const QJsonObject& person,const QString& key,QSize size) {
    const auto settings=person["settings"].toObject();const auto edited=settings["masks"].toObject();
    if(key.startsWith("eye.") && (key.endsWith(".left") || key.endsWith(".right") || edited.contains(key+".left") || edited.contains(key+".right"))){
        const QString root=key.section('.',0,1);const bool sided=key!=root;
        auto clean=person;auto cleanSettings=settings;auto cleanMasks=edited;cleanMasks.remove(root+".left");cleanMasks.remove(root+".right");cleanSettings["masks"]=cleanMasks;clean["settings"]=cleanSettings;
        const auto base=portrait_effect_mask(clean,root,size);
        auto mask=sided && edited.contains(key)?mask_from(edited,key.toUtf8().constData()).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation):base;
        const auto lm=person["landmarks"].toArray();if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(0);}if(lm.size()!=66){mask.fill(0);return mask;}
        const auto point=[&](int i){const auto p=lm[i].toArray();return QPointF(p[0].toDouble()*size.width(),p[1].toDouble()*size.height());};
        const auto left=(point(42)+point(45))*.5,right=(point(36)+point(39))*.5,axis=left-right,mid=(left+right)*.5;
        const auto leftMask=edited.contains(root+".left")?mask_from(edited,(root+".left").toUtf8().constData()).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation):base;
        const auto rightMask=edited.contains(root+".right")?mask_from(edited,(root+".right").toUtf8().constData()).scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation):base;
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){const bool leftSide=QPointF::dotProduct(QPointF(x+.5,y+.5)-mid,axis)>=0;if(sided){if(leftSide!=key.endsWith(".left"))mask.scanLine(y)[x]=0;}else{const auto& source=leftSide?leftMask:rightMask;mask.scanLine(y)[x]=source.isNull()?0:source.constScanLine(y)[x];}}
        return mask;
    }
    if(settings["imageScope"].toBool() && key=="skin"){QImage area(size,QImage::Format_Grayscale8);area.fill(255);return area;}
    if((key=="skin.smooth" || key=="skin.bodySmooth") && settings["smoothManual"].toObject()[key].toBool()){
        auto mask=mask_from(settings["smoothManualMasks"].toObject(),key.toUtf8().constData());if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(0);}return mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    }
    if(key.startsWith("paint.")){
        auto mask=mask_from(edited,key.toUtf8().constData());if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(0);return mask;}mask=mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);auto skin=mask_from(settings["automaticMasks"].toObject(),"skin").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(skin.isNull())mask.fill(0);else for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)mask.scanLine(y)[x]=std::min(mask.constScanLine(y)[x],skin.constScanLine(y)[x]);return mask;
    }
    if(key=="repair.preserve"){
        auto mask=mask_from(edited,"repair.preserve");if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(0);}else mask=mask.scaled(size);
        return mask;
    }
    if(key.startsWith("repair.item.")){
        QImage shape(size,QImage::Format_ARGB32);shape.fill(Qt::black);QPainter painter(&shape);painter.setRenderHint(QPainter::Antialiasing);
        for(const auto& v:settings["repairs"].toArray()){const auto stroke=v.toObject();if("repair.item."+stroke["id"].toString()!=key)continue;const auto points=stroke["points"].toArray();if(points.isEmpty())continue;QPainterPath path;const auto first=points[0].toArray();path.moveTo(first[0].toDouble()*size.width(),first[1].toDouble()*size.height());for(int i=1;i<points.size();++i){const auto p=points[i].toArray();path.lineTo(p[0].toDouble()*size.width(),p[1].toDouble()*size.height());}const double radius=stroke["radius"].toDouble() *std::min(size.width(),size.height());painter.setPen(QPen(Qt::white,std::max(1.,radius*2),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));if(points.size()==1)painter.drawPoint(path.currentPosition());else painter.drawPath(path);}
        painter.end();auto result=shape.convertToFormat(QImage::Format_Grayscale8);const auto coverage=mask_from(edited,key.toUtf8().constData());if(!coverage.isNull()){const auto scaled=coverage.scaled(size);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)result.scanLine(y)[x]=uchar(result.constScanLine(y)[x]*scaled.constScanLine(y)[x]/255);}return result;
    }
    QImage mask;
    if(key.startsWith("color.") && edited.contains("color.scope"))return mask_from(edited,"color.scope").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(key=="hair.edge")return portrait_style_mask(person,key,size);
    if(key.startsWith("decal.") && !edited.contains(key)){QImage full(size,QImage::Format_Grayscale8);full.fill(255);return full;}
    if(key=="__protection")mask=mask_from(settings,"protection");
    else if(key.startsWith("teeth.") && edited.contains("teeth.scope"))mask=mask_from(edited,"teeth.scope");
    else if(edited.contains(key)){
        mask=key=="hair.accessory"?portrait_accessory_coverage(person,size):mask_from(edited,key.toUtf8().constData());
        if((key=="face.doubleChin" && settings["doubleChinVersion"].toInt()>=2) || key.startsWith("hair.") || key=="contour.lines" || key.startsWith("makeup.") || (settings["contourLightVersion"].toInt()>=2 && (key=="contour.highlight" || key=="contour.shadow"))){
            auto material=portrait_style_mask(person,key,size);mask=mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)material.scanLine(y)[x]=uchar(material.constScanLine(y)[x]*mask.constScanLine(y)[x]/255);mask=material;
        }
    }
    else if(key=="skin.bodySmooth")mask=mask_from(settings["automaticMasks"].toObject(),"bodySkin");
    else if(key=="skin.smooth" && settings["smoothScopeVersion"].toInt(1)>=2)mask=mask_from(settings["automaticMasks"].toObject(),"faceSkin");
    else if(key.startsWith("teeth."))mask=mask_from(settings["automaticMasks"].toObject(),"teeth");
    else if(key.startsWith("eye."))mask=mask_from(settings["automaticMasks"].toObject(),key.toUtf8().constData());
    else if(key=="face.volume" || key=="face.doubleChin" || key.startsWith("makeup.") || key.startsWith("contour.") || key.startsWith("light.") || key.startsWith("hair."))mask=portrait_style_mask(person,key,size);
    else if(!key.startsWith("repair."))mask=mask_from(settings["automaticMasks"].toObject(),"skin");
    if(mask.isNull()){mask=QImage(size,QImage::Format_Grayscale8);mask.fill(0);return mask;}
    mask=mask.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(key=="skin.oil" && edited.contains(key)){
        auto skin=mask_from(settings["automaticMasks"].toObject(),"skin").scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        if(skin.isNull())mask.fill(0);
        else for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)mask.scanLine(y)[x]=std::min(mask.constScanLine(y)[x],skin.constScanLine(y)[x]);
    }
    return mask;
}
QString portrait_arm_unavailable(const QJsonObject& person,QSize size,int side){try{
    const auto pose=person["settings"].toObject()["pose"].toArray();
    if(person["bodyAmbiguous"].toBool() || pose.size()!=33)throw EditError("身体关联不足，请先确认手臂范围");
    const auto point=[&](int i){const auto q=pose[i].toArray();if(q.size()!=3 || q[2].toDouble()<=.5)throw EditError("手臂定位不足，请修正肩肘腕关键点");return QPointF(q[0].toDouble(),q[1].toDouble());};
    const auto body=QImage::fromData(QByteArray::fromBase64(person["bodyMask"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
    PortraitLimb upper(size,point(11+side),point(13+side),body,1.,.45,0.,1.,true);
    PortraitLimb lower(size,point(13+side),point(15+side),body,1.,.45,0.,1.,true);return {};
}catch(const std::exception& e){return QString::fromUtf8(e.what());}}
QString portrait_neck_unavailable(const QJsonObject& person,QSize size){try{PortraitNeck field(size,person,1);return {};}catch(const std::exception& e){return QString::fromUtf8(e.what());}}
QString portrait_height_unavailable(const QJsonObject& person,QSize size,double amount){try{PortraitHeight field(size,person,amount);return {};}catch(const std::exception& e){return QString::fromUtf8(e.what());}}
QString portrait_leg_length_unavailable(const QJsonObject& person,QSize size,int side,double amount,bool paired){try{PortraitLegLength field(size,person,side,amount,paired);return {};}catch(const std::exception& e){return QString::fromUtf8(e.what());}}
QString portrait_straight_leg_unavailable(const QJsonObject& person,QSize size,int side){try{PortraitStraightLeg field(size,person,side,1);return {};}catch(const std::exception& e){return QString::fromUtf8(e.what());}}
QString portrait_back_unavailable(const QJsonObject& person,QSize size){try{PortraitBack field(size,person,1);return {};}catch(const std::exception& e){return QString::fromUtf8(e.what());}}
QString portrait_abdomen_unavailable(const QJsonObject& person,QSize size,bool local){try{PortraitAbdomen field(size,person,local?0:1,local?1:0);return {};}catch(const std::exception& e){return QString::fromUtf8(e.what());}}
bool portrait_orbital_lift_available(const QJsonObject& person,QSize size,int side){
    const auto masks=person["settings"].toObject()["automaticMasks"].toObject();const PortraitContour field(size,person["landmarks"].toArray(),0,0,0,0,{},mask_from(masks,"eyeglassFrame"),{},{},{},{},{},{},{},0,{},{},{{1,1},mask_from(masks,"faceSkin")});return field.has_local_lift(side);
}
bool portrait_perioral_lift_available(const QJsonObject& person,QSize size,int side){
    const auto masks=person["settings"].toObject()["automaticMasks"].toObject();const PortraitContour field(size,person["landmarks"].toArray(),0,0,0,0,{},mask_from(masks,"eyeglassFrame"),{},{},{},{},{},{},{},0,{},{},{{0,0,1,1},mask_from(masks,"faceSkin")});return field.has_local_lift(side+2);
}
bool portrait_forehead_lift_available(const QJsonObject& person,QSize size){
    const auto masks=person["settings"].toObject()["automaticMasks"].toObject();
    const PortraitContour field(size,person["landmarks"].toArray(),0,0,0,0,{},mask_from(masks,"eyeglassFrame"),{},{},{},{},{},{},{},0,{},
        {1,mask_from(masks,"faceSkin")});
    return field.has_forehead_lift();
}
static QPointF protection_field(const QImage& mesh,QPointF q) {
    if(mesh.isNull())return {};
    const double x=std::clamp(q.x(),0.,1.)*(mesh.width()-1),y=std::clamp(q.y(),0.,1.)*(mesh.height()-1);
    const int ix=int(x),iy=int(y),jx=std::min(ix+1,mesh.width()-1),jy=std::min(iy+1,mesh.height()-1);
    const auto* a=reinterpret_cast<const float*>(mesh.constScanLine(iy));const auto* b=reinterpret_cast<const float*>(mesh.constScanLine(jy));
    const auto component=[&](int c){return (a[ix*4+c]*(1-(x-ix))+a[jx*4+c]*(x-ix))*(1-(y-iy))+(b[ix*4+c]*(1-(x-ix))+b[jx*4+c]*(x-ix))*(y-iy);};
    return {component(0),component(1)};
}
QImage portrait_protection_weights(const QJsonObject& settings,QSize grid) {
    const auto protection=mask_from(settings,"protection");
    if(protection.isNull())return {};
    const auto coordinates=liquify_from_json(settings["protectionCoordinates"].toObject());
    QImage weights(grid,QImage::Format_Grayscale8);
    for(int y=0;y<grid.height();++y)for(int x=0;x<grid.width();++x){
        const QPointF q(double(x)/std::max(1,grid.width()-1),double(y)/std::max(1,grid.height()-1));
        weights.scanLine(y)[x]=uchar(std::round(mask_value(protection,q+protection_field(coordinates,q))*255));
    }
    return weights;
}
static QImage portrait_deformation_impl(QSize size,const QJsonObject& recipe,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled,bool validate_only,int only_person=-1,bool ignore_protection=false,int component=0,int manual_person=-1,QImage* manual_output=nullptr,PortraitManualInputs* manual_inputs=nullptr) {
    if(recipe.isEmpty())return {};
    if(recipe["version"].toInt()!=1 || !std::isfinite(amount) || amount<0 || amount>1 || recipe["people"].toArray().size()>128)
        throw EditError(QStringLiteral("人像效果参数无效"));
    auto mesh=validate_only?QImage{}:create_liquify_mesh(size);bool changed=false,semantic_contour=false;
    if(manual_output)*manual_output=create_liquify_mesh(size);
    const auto records=recipe["people"].toArray();std::vector<QImage> masks;
    for(const auto& v:records)masks.push_back(mask_from(v.toObject(),"bodyMask"));
    for(int index=0;index<records.size();++index) {
        if(only_person>=0 && index!=only_person)continue;
        const auto record=records[index].toObject();const auto p=portrait_person_from_json(record);
        if(p.unmatched || p.match_ambiguous)continue;
        const bool partial=p.settings["partialSkin"].toBool();
        if(partial && p.settings["backContour"].toArray().size()!=3 && p.settings["liquify"].toObject().isEmpty())continue;
        const auto parameters=p.settings["parameters"].toObject();
        const auto enabled=p.settings["enabled"].toObject();
        const double strength=std::clamp(p.settings["strength"].toDouble(100.),0.,100.)*.01*amount;
        auto value=[&](const QString& key,int side=-1) {
            if(partial && key!="body.back")return 0.;
            if(!enabled[key.section('.',0,0)].toBool(true) || p.settings["disabledParameters"].toObject()[key].toBool())return 0.;
            const auto name=side<0?key:key+(side==0?".left":".right");const auto parameter=parameters[name];
            const double number=parameter.toDouble();if(!std::isfinite(number) || std::abs(number)>100)throw EditError(QStringLiteral("人像参数超出允许范围"));return number*.01*strength;
        };
        const auto f=p.face;const double w=f.width(),h=f.height();const auto& lm=p.landmarks;
        const bool use_scalp=p.settings["scalpGeometryVersion"].toInt(1)>=2;
        const bool use_head=p.settings["headGeometryVersion"].toInt(1)>=2;
        const bool use_contour=p.settings["contourVersion"].toInt(1)>=2;
        const bool use_eyes=p.settings["eyeGeometryVersion"].toInt(1)>=2;
        const bool use_nose=p.settings["noseGeometryVersion"].toInt(1)>=2;
        const bool use_mouth=p.settings["mouthGeometryVersion"].toInt(1)>=2;
        const bool use_proportions=p.settings["proportionGeometryVersion"].toInt(1)>=2;
        const bool use_brows=p.settings["browGeometryVersion"].toInt(1)>=2;
        std::array<PortraitEyeTarget,2> eye_targets{};
        if(use_eyes)for(int side=0;side<2;++side)eye_targets[side]={value("feature.eyeSize",side),value("feature.eyeSpace",side),value("feature.eyeHeight",side),value("feature.eyeTilt",side),value("feature.eyeLength",side),value("feature.eyeInnerCorner",side),value("feature.eyeOuterCorner",side),value("feature.eyeAperture",side),value("feature.eyeUpperLift",side),value("feature.eyeLowerLid",side),value("feature.eyeOuterLift",side)};
        const auto frames=mask_from(p.settings["automaticMasks"].toObject(),"eyeglassFrame");
        const PortraitNoseTarget nose_target{use_nose?value("feature.noseWidth"):0,use_nose?value("feature.noseLength"):0,use_nose?value("feature.noseSize"):0,use_nose?value("feature.noseRootWidth"):0,use_nose?value("feature.noseBridgeWidth"):0,use_nose?value("feature.noseTipWidth"):0};
        PortraitMouthTarget mouth_target{use_mouth?value("feature.lipSize"):0,use_mouth?value("feature.lipThickness"):0,{use_mouth?value("feature.smile",0):0,use_mouth?value("feature.smile",1):0}};
        if(use_mouth){mouth_target.width=value("feature.lipWidth");mouth_target.height=value("feature.lipHeight");mouth_target.offset=value("feature.lipOffset");mouth_target.tilt=value("feature.lipTilt");mouth_target.upper=value("feature.lipUpper");mouth_target.lower=value("feature.lipLower");mouth_target.cupid=value("feature.lipCupid");}
        std::array<PortraitBrowTarget,2> brow_targets{};
        if(use_brows)for(int side=0;side<2;++side)brow_targets[side]={value("feature.browHeight",side),value("feature.browTilt",side),value("feature.browLength",side),value("feature.browSpace",side),value("feature.browHeadSpace",side),value("feature.browArchHeight",side),value("feature.browArchPosition",side),value("feature.browThickness",side)};
        const PortraitProportionTarget proportion_targets{use_proportions?value("head.midface"):0,use_proportions?value("head.philtrum"):0,use_proportions?value("head.lowerface"):0};
        const PortraitOutlineTarget outline_targets{use_contour?value("face.chinWidth"):0,use_contour?value("face.smoothContour"):0,use_contour?value("face.jawAngle"):0};
        PortraitHeadTarget head_target{use_head?value("head.size"):0,mask_from(p.settings["automaticMasks"].toObject(),"head"),p.settings["pose"].toArray()};
        if(p.settings["headManualEnabled"].toBool()){head_target.region=portrait_head_region(record);head_target.manual=p.settings["headManual"].toObject();}
        if(value("face.width")!=0)head_target.scalp=portrait_scalp_geometry(portrait_person_json(p));
        head_target.roll=use_head?value("head.roll"):0;
        if(use_scalp){head_target.forehead=value("face.forehead");head_target.crown=value("head.crown");head_target.hairline=value("head.hairline");head_target.hairVolume=value("head.hairVolume");head_target.hairRegion=mask_from(p.settings["automaticMasks"].toObject(),"hair");if(p.settings.contains("scalpGeometry"))head_target.scalp=p.settings["scalpGeometry"].toObject();}
        if(head_target.active() && p.settings["protectBackground"].toBool(true)){
            for(const auto& line:p.settings["backgroundLines"].toArray()){
                const auto a=line.toArray();if(a.size()!=4)continue;
                const QPointF start(a[0].toDouble(),a[1].toDouble()),end(a[2].toDouble(),a[3].toDouble());
                const double pixels=std::hypot((end.x()-start.x())*size.width(),(end.y()-start.y())*size.height());
                const int count=std::max(1,int(std::ceil(pixels/std::max(2.,w*size.width()*.06))));
                for(int j=0;j<=count;++j){const auto q=start+(end-start)*(double(j)/count);
                    bool nearHead=false;for(int dy=-2;dy<=2 && !nearHead;++dy)for(int dx=-2;dx<=2;++dx)if(mask_value(head_target.region,q+QPointF(dx*w*.14,dy*w*.14*size.width()/size.height()))>.1){nearHead=true;break;}
                    if(!nearHead && mask_value(masks[std::size_t(index)],q)<.1 && std::hypot((q.x()-f.center().x())*size.width(),(q.y()-f.center().y())*size.height())<w*size.width()*2)
                        head_target.background.push_back(q);
                }
            }
        }
        // Combine semantic contributions before the single contour solve. A
        // preset must never deform the already-deformed image a second time.
        const double small=use_contour?value("face.small"):0,vShape=use_contour?value("face.vShape"):0;
        const auto combined=[](double base,double extra){return std::clamp(base+extra,-1.,1.);};
        auto combinedOutline=outline_targets;
        combinedOutline.temple={value("face.templeFill",0),value("face.templeFill",1)};
        combinedOutline.chinWidth=combined(combinedOutline.chinWidth,-.12*small-.30*vShape);
        // Budget the target amplitudes before applying overall strength. The
        // combination edits one source-space field, never blends warped images.
        double liftBudget=0;
        if(strength>0){
            liftBudget=.020*std::abs(value("face.foreheadLift"))/strength;
            for(const auto& item:std::array<std::pair<const char*,double>,4>{{{"face.contourLift",.037},{"face.orbitalLift",.014},{"face.midfaceLift",.020},{"face.perioralLift",.011}}})
                liftBudget+=item.second*std::max(std::abs(value(item.first,0)),std::abs(value(item.first,1)))/strength;
        }
        const double liftScale=liftBudget>.060?.060/liftBudget:1.;
        const auto liftValue=[&](const QString& key,int side=-1){return value(key,side)*liftScale;};
        const auto makeContour=[&](const QString& excluded){
            const auto regionalValue=[&](const QString& key,int side=-1){return key==excluded?0.:value(key,side);};
            const auto regionalLift=[&](const QString& key,int side=-1){return key==excluded?0.:liftValue(key,side);};
            auto regionalOutline=combinedOutline;if(excluded=="face.templeFill")regionalOutline.temple={};
        return PortraitContour(size,lm,use_contour?combined(value("face.width"),-.35*small):0,use_contour?value("face.cheek"):0,
            use_contour?combined(value("face.jaw"),-.22*small-.45*vShape):0,use_contour?combined(value("face.chin"),.08*vShape):0,
            eye_targets,frames,nose_target,mouth_target,brow_targets,proportion_targets,regionalOutline,head_target,{regionalValue("face.malarShape",0),regionalValue("face.malarShape",1)},p.settings["cheekVolumeVersion"].toInt()>=2?regionalValue("face.volume"):0,{regionalLift("face.contourLift",0),regionalLift("face.contourLift",1)},{regionalLift("face.foreheadLift"),mask_from(p.settings["automaticMasks"].toObject(),"faceSkin")},{{regionalLift("face.orbitalLift",0),regionalLift("face.orbitalLift",1),regionalLift("face.perioralLift",0),regionalLift("face.perioralLift",1)},mask_from(p.settings["automaticMasks"].toObject(),"faceSkin")},{regionalLift("face.midfaceLift",0),regionalLift("face.midfaceLift",1)});
        };
        const auto contour=makeContour({});
        std::vector<std::pair<PortraitContour,QImage>> regionalCorrections;
        const auto manualMasks=p.settings["masks"].toObject();
        for(const auto& key:QStringList{"face.templeFill","face.malarShape","face.volume","face.contourLift","face.foreheadLift","face.orbitalLift","face.perioralLift","face.midfaceLift"}){
            const auto mask=mask_from(manualMasks,key.toUtf8().constData());
            if(!mask.isNull() && (value(key)!=0 || value(key,0)!=0 || value(key,1)!=0))regionalCorrections.emplace_back(makeContour(key),mask);
        }
        const bool orienting=value("head.yaw")!=0 || value("head.pitch")!=0;
        const auto orientationRegion=orienting?(head_target.region.isNull()?portrait_head_region(portrait_person_json(p)):head_target.region):QImage{};
        const PortraitOrientation orientation(size,p.settings["faceSurface"].toObject(),value("head.yaw"),value("head.pitch"),orientationRegion,p.settings["headOrientationVersion"].toInt(1));
        semantic_contour|=!contour.empty() || !orientation.empty();
        std::vector<Kernel> kernels;
        auto add=[&](QPointF c,QPointF r,QPointF shift,double sx=0.,double sy=0.,double angle=0.) {if(QLineF({},shift).length()+std::abs(sx)+std::abs(sy)+std::abs(angle)>1e-9)kernels.push_back({c,r,shift,sx,sy,angle});};
        for(int side=0;side<2;++side) {
            const int direction=side==0?1:-1;const auto cheek=point(lm,side==0?13:3);
            // Preserve rendering of saved projects until a contour control is
            // explicitly edited. Undo restores both values and engine version.
            if(!use_contour){
                add(cheek,{w*.55,h*.55},{-direction*w*.09*value("face.width"),0});
                add(point(lm,side==0?14:2),{w*.35,h*.3},{-direction*w*.065*value("face.cheek"),0});
                add(point(lm,side==0?11:5),{w*.4,h*.4},{-direction*w*.07*value("face.jaw"),0});
            }
            if(p.settings["cheekVolumeVersion"].toInt()<2)add(cheek,{w*.35,h*.3},{},-.06*value("face.volume"),-.04*value("face.volume"));
            const auto eye=mean(lm,side==0?42:36,side==0?47:41),brow=mean(lm,side==0?22:17,side==0?26:21);
            if(!use_eyes)add(eye,{w*.26,h*.23},{direction*w*.035*value("feature.eyeSpace",side),-h*.025*value("feature.eyeHeight",side)},-.12*value("feature.eyeSize",side),-.12*value("feature.eyeSize",side),direction*.12*value("feature.eyeTilt",side));
            if(!use_brows)add(brow,{w*.3,h*.15},{0,-h*.035*value("feature.browHeight",side)},0,0,direction*.12*value("feature.browTilt",side));
            if(!use_mouth)add(point(lm,side==0?54:48),{w*.18,h*.18},{0,h*.035*value("feature.smile",side)});
        }
        if(!use_contour)add(point(lm,8),{w*.38,h*.35},{0,-h*.08*value("face.chin")});
        if(!use_scalp)add({f.center().x(),f.top()+h*.05},{w*.7,h*.5},{0,h*.06*value("face.forehead")});
        const auto chinAnalysis=p.settings["doubleChinAnalysis"].toObject();
        const auto chinMask=mask_from(chinAnalysis,"mask");const auto chinShift=chinAnalysis["shift"].toArray();
        const auto chinRange=mask_from(p.settings["masks"].toObject(),"face.doubleChin");
        if(p.settings["doubleChinVersion"].toInt()<2)add(point(lm,8)+QPointF(0,h*.18),{w*.6,h*.4},{0,h*.045*value("face.doubleChin")});
        if(!use_nose)add(mean(lm,31,35),{w*.25,h*.35},{0,-h*.035*value("feature.noseLength")},.12*value("feature.noseWidth"),0);
        if(!use_mouth)add(mean(lm,48,59),{w*.42,h*.25},{},-.1*value("feature.lipSize"),-.1*value("feature.lipSize")-.12*value("feature.lipThickness"));
        if(!use_head)add(f.center()-QPointF(0,h*.12),{w*1.1,h*1.2},{},.06*(value("head.size")+value("head.proportion")),.06*(value("head.size")+value("head.proportion")));
        if(!use_proportions)add(mean(lm,27,35),{w*.6,h*.5},{},0,.1*value("head.midface"));
        if(!use_proportions)add((point(lm,33)+point(lm,51))*.5,{w*.3,h*.2},{},0,.12*value("head.philtrum"));
        if(!use_proportions)add((point(lm,8)+point(lm,57))*.5,{w*.6,h*.35},{},0,.1*value("head.lowerface"));
        const auto eye_axis=mean(lm,42,47)-mean(lm,36,41);const double face_angle=std::atan2(eye_axis.y()*size.height(),eye_axis.x()*size.width());for(auto& kernel:kernels)kernel.axis=face_angle;
        const bool use_waist=p.settings["waistGeometryVersion"].toInt(1)>=2;
        const bool use_limb=p.settings["limbGeometryVersion"].toInt(1)>=2;
        std::vector<PortraitLimb> limbs;std::vector<PortraitAbdomen> abdomens;std::vector<PortraitChest> chests;std::vector<PortraitPelvis> pelvis;std::vector<PortraitBack> backs;std::vector<PortraitStraightLeg> straightLegs;std::vector<PortraitLegLength> longLegs;std::vector<PortraitHeight> heights;std::vector<PortraitManualHeight> manualHeights;std::vector<PortraitManualSlim> manualSlims;std::vector<PortraitManualScale> manualScales;std::vector<PortraitManualDistort> manualDistorts;std::vector<PortraitNeck> necks;std::vector<PortraitNeckLength> neckLengths;std::vector<PortraitShoulderWidth> shoulderWidths;std::vector<PortraitShoulderContour> shoulderContours;
        for(int side=0;side<2;++side)if(value("body.squareShoulder",side)!=0)shoulderContours.emplace_back(size,record,side,value("body.squareShoulder",side));
        const bool use_trapezius=p.settings["trapeziusGeometryVersion"].toInt(1)>=2;
        if(use_trapezius)for(int side=0;side<2;++side)if(value("body.trapezius",side)!=0)shoulderContours.emplace_back(size,record,side,value("body.trapezius",side),PortraitShoulderContour::Profile::Trapezius);
        const bool use_shoulder=p.settings["shoulderGeometryVersion"].toInt(1)>=2;
        if(value("body.shoulderWidth",0)!=0 || value("body.shoulderWidth",1)!=0 || (use_shoulder && (value("body.shoulder",0)!=0 || value("body.shoulder",1)!=0)))shoulderWidths.emplace_back(size,record,value("body.shoulderWidth",0),value("body.shoulderWidth",1),use_shoulder?value("body.shoulder",0):0,use_shoulder?value("body.shoulder",1):0);
        if(value("body.neckLength")!=0)neckLengths.emplace_back(size,record,value("body.neckLength"));
        const bool use_neck=p.settings["neckGeometryVersion"].toInt(1)>=2;
        if(use_neck && value("body.neck")!=0)necks.emplace_back(size,record,value("body.neck"));
        const bool use_height=p.settings["heightVersion"].toInt(1)>=2;
        if(use_height && value("body.height")!=0)heights.emplace_back(size,record,value("body.height"));
        const bool use_leg_length=p.settings["legLengthVersion"].toInt(1)>=2;
        if(use_leg_length){const double left=value("body.legLength",0),right=value("body.legLength",1);
            if(left!=0 && std::abs(left-right)<1e-9)longLegs.emplace_back(size,record,0,left,true);
            else for(int side=0;side<2;++side)if(value("body.legLength",side)!=0)longLegs.emplace_back(size,record,side,value("body.legLength",side));
        }
        for(int side=0;side<2;++side)if(value("body.straightLeg",side)!=0)straightLegs.emplace_back(size,record,side,value("body.straightLeg",side));
        if(value("body.back")!=0)backs.emplace_back(size,record,value("body.back"));
        if(value("body.abdomen")!=0 || value("body.belly")!=0)abdomens.emplace_back(size,record,value("body.abdomen"),value("body.belly"));
        for(int side=0;side<2;++side)if(value("body.hipWidth",side)!=0)pelvis.emplace_back(size,record,side,value("body.hipWidth",side));
        if(value("body.manualRotate")!=0 || value("body.manualShear")!=0)manualDistorts.emplace_back(size,record,value("body.manualRotate"),value("body.manualShear"));
        if(value("body.manualScale")!=0)manualScales.emplace_back(size,record,value("body.manualScale"));
        if(value("body.manualSlim")!=0)manualSlims.emplace_back(size,record,value("body.manualSlim"));
        if(value("body.manualHeight")!=0)manualHeights.emplace_back(size,record,value("body.manualHeight"));
        const auto chestPaths=p.settings["chestContourPaths"].toObject();
        for(int side=0;side<2;++side)if(value("body.chest",side)!=0){
            auto chestRecord=record;auto chestSettings=p.settings;const auto path=chestPaths[side==0?"left":"right"].toArray();
            if(path.size()==3)chestSettings["chestContourPath"]=path;
            chestRecord["settings"]=chestSettings;chests.emplace_back(size,chestRecord,value("body.chest",side),side);
        }
        const auto pose=p.settings["pose"].toArray();
        auto reliable=[&](int i){return pose.size()==33 && pose[i].toArray().size()==3 && pose[i].toArray()[2].toDouble()>.5;};
        if(!p.body_ambiguous && reliable(11) && reliable(12) && reliable(23) && reliable(24)) {
            const auto shoulders=(point(pose,11)+point(pose,12))*.5,hips=(point(pose,23)+point(pose,24))*.5;
            const double bw=std::max(w*1.6,std::abs(point(pose,11).x()-point(pose,12).x())),bh=std::max(h*3.,p.body.height());
            if(!use_waist)add((shoulders+hips)*.5,{bw*.7,bh*.25},{},.1*value("body.waist"),0);
            if(!use_neck)add((shoulders+point(lm,8))*.5,{w*.5,h*.45},{},.1*value("body.neck"),0);
            if(!use_height)add(p.body.center(),{p.body.width(),bh*.7},{},0,.04*value("body.height"));
            for(int side=0;side<2;++side) {
                const int shoulder=11+side,elbow=13+side,hip=23+side,knee=25+side,ankle=27+side;
                if(!use_shoulder)add(point(pose,shoulder),{bw*.35,bh*.13},{0,bh*.025*value("body.shoulder",side)});
                if(!use_trapezius)add((point(pose,shoulder)+point(lm,8))*.5,{bw*.35,bh*.12},{0,bh*.025*value("body.trapezius",side)});
                if(!use_limb && reliable(elbow))add((point(pose,shoulder)+point(pose,elbow))*.5,{bw*.28,bh*.22},{},.1*value("body.arm",side),0);
                if(!use_limb && reliable(knee))add((point(pose,hip)+point(pose,knee))*.5,{bw*.35,bh*.3},{},.09*value("body.legWidth",side),0);
                if(!use_leg_length && reliable(ankle))add((point(pose,hip)+point(pose,ankle))*.5,{bw*.35,bh*.35},{0,-bh*.03*value("body.legLength",side)},0,.045*value("body.legLength",side));
            }
        }
        if(use_waist && value("body.waist")!=0){
            if(p.body_ambiguous || !reliable(11) || !reliable(12) || !reliable(23) || !reliable(24))throw EditError("肩髋定位不足，不能可靠调整腰部，请修正定位");
            const auto shoulders=(point(pose,11)+point(pose,12))*.5,hips=(point(pose,23)+point(pose,24))*.5;
            // The lower torso band leaves the chest and pelvis untouched.
            limbs.emplace_back(size,shoulders,hips,masks[std::size_t(index)],value("body.waist"),1.,.4,.85);
        }
        std::array<QImage,2> legMasks{masks[std::size_t(index)],masks[std::size_t(index)]};
        bool editLegs=false;
        if(use_limb)for(int side=0;side<2;++side)editLegs|=value("body.legWidth",side)!=0 || value("body.thigh",side)!=0 || value("body.calf",side)!=0;
        if(editLegs && !legMasks[0].isNull() && reliable(23) && reliable(24) && reliable(25) && reliable(26) && reliable(27) && reliable(28)){
            // Joined trousers are one person mask, not one leg. Assign each
            // foreground pixel to its closest hip-knee-ankle chain; keep the
            // observed outer silhouette and protect the other chain below.
            std::array<std::array<QPointF,3>,2> chains;
            for(int side=0;side<2;++side)for(int j=0;j<3;++j){const auto q=point(pose,23+2*j+side);chains[side][j]={q.x()*(size.width()-1),q.y()*(size.height()-1)};}
            const auto distance=[](QPointF q,const std::array<QPointF,3>& chain){
                double result=1e30;
                for(int j=0;j<2;++j){const auto d=chain[j+1]-chain[j];const double t=std::clamp(QPointF::dotProduct(q-chain[j],d)/std::max(1.,QPointF::dotProduct(d,d)),0.,1.);const auto r=q-chain[j]-d*t;result=std::min(result,QPointF::dotProduct(r,r));}
                return result;
            };
            legMasks[0]=legMasks[0].copy();legMasks[1]=legMasks[1].copy();
            for(int y=0;y<legMasks[0].height();++y){auto* left=legMasks[0].scanLine(y);auto* right=legMasks[1].scanLine(y);
                for(int x=0;x<legMasks[0].width();++x){if(left[x]<=127)continue;const QPointF q(double(x)*(size.width()-1)/legMasks[0].width(),double(y)*(size.height()-1)/legMasks[0].height());
                    if(distance(q,chains[0])<=distance(q,chains[1]))right[x]=0;else left[x]=0;
                }
            }
        }
        if(use_limb)for(int side=0;side<2;++side){
            const auto append=[&](int a,int b,double strengthValue){
                if(std::abs(strengthValue)<1e-9)return;
                if(p.body_ambiguous || !reliable(a) || !reliable(b))throw EditError("肢体定位不完整，请修正关键点后再调整粗细");
                limbs.emplace_back(size,point(pose,a),point(pose,b),a<23?masks[std::size_t(index)]:legMasks[side],std::clamp(strengthValue,-1.,1.),.45,0.,1.,a<23);
            };
            append(11+side,13+side,value("body.arm",side));
            append(13+side,15+side,value("body.arm",side));
            if(std::abs(value("body.upperArm",side))>1e-9){
                if(!reliable(15+side))throw EditError("小臂定位不足，无法保护肘腕段；请修正定位后调整大臂");
                append(11+side,13+side,value("body.upperArm",side));
                limbs.back().protect_segment(point(pose,13+side),point(pose,15+side));
            }
            const auto legBegin=limbs.size();
            append(23+side,25+side,value("body.legWidth",side));
            if(std::abs(value("body.thigh",side))>1e-9){
                if(!reliable(27+side))throw EditError("小腿定位不足，无法保护膝踝段；请修正定位后调整大腿");
                const PortraitLimb calf(size,point(pose,25+side),point(pose,27+side),legMasks[side],0.);
                append(23+side,25+side,value("body.thigh",side));
                limbs.back().protect_segment(point(pose,25+side),point(pose,27+side),calf.maximum_radius());
            }
            append(25+side,27+side,value("body.legWidth",side));
            if(std::abs(value("body.calf",side))>1e-9){
                if(!reliable(23+side))throw EditError("大腿定位不足，无法保护髋膝段；请修正定位后调整小腿");
                const PortraitLimb thigh(size,point(pose,23+side),point(pose,25+side),legMasks[side],0.);
                append(25+side,27+side,value("body.calf",side));
                limbs.back().protect_segment(point(pose,23+side),point(pose,25+side),thigh.maximum_radius());
            }
            if(limbs.size()>legBegin){
                const int other=1-side;
                for(int a:{23,25})if(reliable(a+other) && reliable(a+2+other)){
                    // Measure the neighbouring segment, not the edited leg's
                    // width: the two legs can have very different silhouettes.
                    const PortraitLimb neighbour(size,point(pose,a+other),point(pose,a+2+other),legMasks[other],0.);
                    for(auto i=legBegin;i<limbs.size();++i)limbs[i].protect_segment(point(pose,a+other),point(pose,a+2+other),neighbour.maximum_radius());
                }
                if(reliable(27+side) && reliable(31+side))
                    for(auto i=legBegin;i<limbs.size();++i)limbs[i].protect_segment(point(pose,27+side),point(pose,31+side));
            }
        }
        semantic_contour|=!manualDistorts.empty() || !manualScales.empty() || !manualSlims.empty() || !manualHeights.empty() || !pelvis.empty() || !chests.empty() || !limbs.empty() || !abdomens.empty() || !backs.empty() || !straightLegs.empty() || !longLegs.empty() || !heights.empty() || !necks.empty() || !neckLengths.empty() || !shoulderWidths.empty() || !shoulderContours.empty();
        if(validate_only)continue;
        auto manual=liquify_from_json(p.settings["liquify"].toObject());
        const auto anchor=ignore_protection?QImage{}:liquify_from_json(p.settings["protectionAutoAnchor"].toObject());
        const auto residual=ignore_protection?QImage{}:liquify_from_json(p.settings["protectionAutoResidual"].toObject());
        const auto manualAnchor=ignore_protection?QImage{}:liquify_from_json(p.settings["protectionManualAnchor"].toObject());
        const auto manualResidual=ignore_protection?QImage{}:liquify_from_json(p.settings["protectionManualResidual"].toObject());
        if(manual_inputs && index==manual_person)*manual_inputs={manual,manualAnchor,manualResidual};
        const auto coordinates=ignore_protection?QImage{}:liquify_from_json(p.settings["protectionCoordinates"].toObject());
        semantic_contour|=!anchor.isNull() || !manualAnchor.isNull();
        if(anchor.isNull() && residual.isNull() && manualAnchor.isNull() && manualResidual.isNull() && manualDistorts.empty() && manualScales.empty() && manualSlims.empty() && manualHeights.empty() && pelvis.empty() && chests.empty() && kernels.empty() && manual.isNull() && contour.empty() && orientation.empty() && limbs.empty() && abdomens.empty() && backs.empty() && straightLegs.empty() && longLegs.empty() && heights.empty() && necks.empty() && neckLengths.empty() && shoulderWidths.empty() && shoulderContours.empty())continue;
        const auto protection=mask_from(p.settings,"protection");
        const auto background_subject=partial?mask_from(p.settings["automaticMasks"].toObject(),"skin"):masks[std::size_t(index)];
        const bool faceCoverageEdit=!orientation.empty() || value("face.width")!=0;
        const bool smoothBackground=!limbs.empty() || !abdomens.empty() || faceCoverageEdit || !manualHeights.empty() || !pelvis.empty() || !chests.empty() || !backs.empty() || editLegs || !longLegs.empty() || !heights.empty() || !neckLengths.empty() || !shoulderWidths.empty() || !shoulderContours.empty();
        QJsonArray backgroundLines;
        for(const auto& entry:p.settings["backgroundLines"].toArray()){
            const auto a=entry.toArray();if(a.size()!=4)continue;bool crossesSubject=false;
            if(smoothBackground)for(int sample=0;sample<=8;++sample){const double t=sample/8.;if(mask_value(background_subject,{a[0].toDouble()*(1-t)+a[2].toDouble()*t,a[1].toDouble()*(1-t)+a[3].toDouble()*t})>=.1){crossesSubject=true;break;}}
            if(!crossesSubject)backgroundLines.append(entry);
        }
        double backgroundBand=std::max(size.width(),size.height())*.01;
        if(faceCoverageEdit)backgroundBand=std::max(backgroundBand,w*size.width()*.4);
        for(const auto& height:manualHeights)backgroundBand=std::max(backgroundBand,4*height.maximum_displacement());
        for(const auto& hip:pelvis)backgroundBand=std::max(backgroundBand,4*hip.maximum_displacement());
        for(const auto& limb:limbs)backgroundBand=std::max(backgroundBand,limb.maximum_radius()*.8);
        for(const auto& shoulder:shoulderContours)backgroundBand=std::max(backgroundBand,4*shoulder.maximum_displacement());
        for(const auto& shoulder:shoulderWidths)backgroundBand=std::max(backgroundBand,4*shoulder.maximum_displacement());
        for(const auto& neck:neckLengths)backgroundBand=std::max(backgroundBand,4*neck.maximum_displacement());
        for(const auto& height:heights)backgroundBand=std::max(backgroundBand,4*height.maximum_displacement());
        for(const auto& leg:longLegs)backgroundBand=std::max(backgroundBand,4*leg.maximum_displacement());
        for(const auto& back:backs)backgroundBand=std::max(backgroundBand,4*back.maximum_displacement());

        // Kernels have compact support: outside these rotated ellipse bounds the
        // displacement is exactly zero. Preserve the full mesh for interpolation.
        QRect area=mesh.rect();
        if(manual.isNull() && anchor.isNull() && residual.isNull() && manualAnchor.isNull() && manualResidual.isNull()){
            QRectF bounds=contour.bounds();if(!orientation.empty())bounds=bounds.isNull()?orientation.bounds():bounds.united(orientation.bounds());
            for(const auto& shoulder:shoulderContours)bounds=bounds.isNull()?shoulder.bounds():bounds.united(shoulder.bounds());
            for(const auto& shoulder:shoulderWidths)bounds=bounds.isNull()?shoulder.bounds():bounds.united(shoulder.bounds());
            for(const auto& neck:neckLengths)bounds=bounds.isNull()?neck.bounds():bounds.united(neck.bounds());
            for(const auto& neck:necks)bounds=bounds.isNull()?neck.bounds():bounds.united(neck.bounds());
            for(const auto& height:heights)bounds=bounds.isNull()?height.bounds():bounds.united(height.bounds());
            for(const auto& leg:longLegs)bounds=bounds.isNull()?leg.bounds():bounds.united(leg.bounds());
            for(const auto& leg:straightLegs)bounds=bounds.isNull()?leg.bounds():bounds.united(leg.bounds());
            for(const auto& back:backs)bounds=bounds.isNull()?back.bounds():bounds.united(back.bounds());
            for(const auto& distort:manualDistorts)bounds=bounds.isNull()?distort.bounds():bounds.united(distort.bounds());
            for(const auto& scale:manualScales)bounds=bounds.isNull()?scale.bounds():bounds.united(scale.bounds());
            for(const auto& slim:manualSlims)bounds=bounds.isNull()?slim.bounds():bounds.united(slim.bounds());
            for(const auto& height:manualHeights)bounds=bounds.isNull()?height.bounds():bounds.united(height.bounds());
            for(const auto& hip:pelvis)bounds=bounds.isNull()?hip.bounds():bounds.united(hip.bounds());
            for(const auto& chest:chests)bounds=bounds.isNull()?chest.bounds():bounds.united(chest.bounds());
            for(const auto& abdomen:abdomens)bounds=bounds.isNull()?abdomen.bounds():bounds.united(abdomen.bounds());
            for(const auto& limb:limbs)bounds=bounds.isNull()?limb.bounds():bounds.united(limb.bounds());
            if(!chinMask.isNull() && value("face.doubleChin")!=0){const auto a=chinAnalysis["bounds"].toArray();if(a.size()==4){const QRectF b(a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble());bounds=bounds.isNull()?b:bounds.united(b);}}
            for(const auto& k:kernels){const double c=std::cos(k.axis),s=std::sin(k.axis);
                const double rx=std::hypot(k.radius.x()*c,k.radius.y()*size.height()/size.width()*s);
                const double ry=std::hypot(k.radius.x()*size.width()/size.height()*s,k.radius.y()*c);
                const QRectF b(k.center.x()-rx,k.center.y()-ry,2*rx,2*ry);bounds=bounds.isNull()?b:bounds.united(b);}
            area=QRectF(bounds.x()*(mesh.width()-1),bounds.y()*(mesh.height()-1),bounds.width()*(mesh.width()-1),bounds.height()*(mesh.height()-1)).toAlignedRect().adjusted(-1,-1,1,1).intersected(mesh.rect());
        }
        if(area.isEmpty())continue;
        auto* mesh_data=reinterpret_cast<float*>(mesh.bits());const auto mesh_stride=mesh.bytesPerLine()/sizeof(float);
        auto* manual_data=manual_output && index==manual_person?reinterpret_cast<float*>(manual_output->bits()):nullptr;
        std::atomic_bool person_changed{false};
        cv::parallel_for_(cv::Range(area.top(),area.bottom()+1),[&](const cv::Range& rows){
        for(int y=rows.start;y<rows.end;++y){if(cancelled && cancelled->load())throw EditError(QStringLiteral("人像变形已取消"));auto* row=mesh_data+y*mesh_stride;bool row_changed=false;
            for(int x=area.left();x<=area.right();++x){const QPointF q(double(x)/(mesh.width()-1),double(y)/(mesh.height()-1));const auto oriented=orientation.displacement(q);QPointF displacement=oriented+contour.displacement(q+oriented);
                for(const auto& correction:regionalCorrections)displacement-=(contour.displacement(q+oriented)-correction.first.displacement(q+oriented))*(1-mask_value(correction.second,q+oriented));
                double coverage=ignore_protection || !anchor.isNull() || !manualAnchor.isNull()?1:1-mask_value(protection,q);double backgroundWeight=1.;
                if(p.settings["protectBackground"].toBool(true) && (smoothBackground || mask_value(background_subject,q)<.1)) {
                    const QPointF pixel(q.x()*size.width(),q.y()*size.height());
                    for(const auto& line_value:backgroundLines) {
                        const auto a=line_value.toArray();if(a.size()!=4)continue;

                        const QPointF start(a[0].toDouble()*size.width(),a[1].toDouble()*size.height()),end(a[2].toDouble()*size.width(),a[3].toDouble()*size.height());
                        const auto delta=end-start;const double length2=QPointF::dotProduct(delta,delta);const double t=std::clamp(QPointF::dotProduct(pixel-start,delta)/std::max(1.,length2),0.,1.);
                        const double distance=QLineF(pixel,start+delta*t).length();const double band=backgroundBand;
                        const double weight=std::clamp((distance-band*.15)/std::max(1.,band),0.,1.);backgroundWeight=smoothBackground?std::min(backgroundWeight,weight*weight*(3-2*weight)):backgroundWeight*weight*weight*(3-2*weight);
                    }
                }
                for(int other=0;other<records.size();++other)if(other!=index)coverage*=1-mask_value(masks[std::size_t(other)],q);
                if(!effect_mask.isNull())coverage*=mask_value(effect_mask,q);
                if(coverage<.001)continue;
                // Whole-head orientation already has a continuous exterior/neck
                // transition. Multiplying that island by detected background
                // lines can pin hair while the face moves (as with head size).
                if(!head_target.active())displacement=oriented+(displacement-oriented)*backgroundWeight;
                for(const auto& shoulder:shoulderContours)displacement+=shoulder.displacement(q+displacement)*backgroundWeight;
                for(const auto& shoulder:shoulderWidths)displacement+=shoulder.displacement(q+displacement)*backgroundWeight;
                for(const auto& neck:neckLengths)displacement+=neck.displacement(q+displacement)*backgroundWeight;
                for(const auto& neck:necks)displacement+=neck.displacement(q+displacement)*backgroundWeight;
                for(const auto& height:heights)displacement+=height.displacement(q+displacement)*backgroundWeight;
                for(const auto& leg:longLegs)displacement+=leg.displacement(q+displacement)*backgroundWeight;
                for(const auto& leg:straightLegs)displacement+=leg.displacement(q+displacement)*backgroundWeight;
                for(const auto& back:backs)displacement+=back.displacement(q+displacement)*backgroundWeight;
                for(const auto& distort:manualDistorts)displacement+=distort.displacement(q+displacement);
                for(const auto& scale:manualScales)displacement+=scale.displacement(q+displacement);
                for(const auto& slim:manualSlims)displacement+=slim.displacement(q+displacement);
                for(const auto& height:manualHeights)displacement+=height.displacement(q+displacement)*height.background_weight(q+displacement,backgroundWeight);
                for(const auto& hip:pelvis)displacement+=hip.displacement(q+displacement)*backgroundWeight;
                for(const auto& chest:chests)displacement+=chest.displacement(q+displacement)*backgroundWeight;
                for(const auto& abdomen:abdomens)displacement+=abdomen.displacement(q+displacement)*backgroundWeight;
                for(const auto& limb:limbs)displacement+=limb.displacement(q+displacement)*backgroundWeight;
                for(const auto& k:kernels){const auto rotate=[&](QPointF v,double angle){const double px=v.x()*size.width(),py=v.y()*size.height();return QPointF((px*std::cos(angle)-py*std::sin(angle))/size.width(),(px*std::sin(angle)+py*std::cos(angle))/size.height());};const auto delta=rotate(q-k.center,-k.axis);const double radius=std::hypot(delta.x()/k.radius.x(),delta.y()/k.radius.y());if(radius>=1)continue;const double weight=std::pow(1-radius*radius,3);
                    displacement+=rotate(k.shift+QPointF(delta.x()*k.sx-delta.y()*k.angle,delta.y()*k.sy+delta.x()*k.angle),k.axis)*weight*backgroundWeight;}
                if(!chinMask.isNull() && chinShift.size()==2){const auto location=q+displacement;displacement+=QPointF(chinShift[0].toDouble(),chinShift[1].toDouble())*value("face.doubleChin")*mask_value(chinMask,location)*(chinRange.isNull()?1:mask_value(chinRange,location));}
                QPointF manualDisplacement;
                if(!manual.isNull()){const int mx=std::clamp(int(q.x()*(manual.width()-1)),0,manual.width()-1),my=std::clamp(int(q.y()*(manual.height()-1)),0,manual.height()-1);const auto* pixel=reinterpret_cast<const float*>(manual.constScanLine(my))+mx*4;displacement*=1-pixel[2];manualDisplacement=QPointF(pixel[0],pixel[1])*strength*backgroundWeight;}
                displacement*=coverage;manualDisplacement*=coverage;
                const double frozen=mask_value(protection,q+protection_field(coordinates,q));
                const double selection=effect_mask.isNull()?1:mask_value(effect_mask,q);
                const auto protect=[&](QPointF value,const QImage& base,const QImage& correction){
                    return base.isNull()?value:value*(1-frozen)+(protection_field(base,q)*frozen+protection_field(correction,q))*(amount*selection);
                };
                displacement=component==2?QPointF{}:protect(displacement,anchor,residual);
                const auto manualResult=protect(manualDisplacement,manualAnchor,manualResidual);
                if(component!=1)displacement+=manualResult;
                if(manual_data){auto* target=manual_data+y*mesh_stride+x*4;target[0]=float(manualResult.x());target[1]=float(manualResult.y());}
                row[x*4]+=float(displacement.x());row[x*4+1]+=float(displacement.y());row_changed|=std::abs(displacement.x())+std::abs(displacement.y())>1e-9;
            }
            if(row_changed)person_changed.store(true,std::memory_order_relaxed);
        }
        },4);
        changed|=person_changed.load(std::memory_order_relaxed);
    }
    // Reject foldovers after all fields and protection masks have been composed.
    // Checking both triangles also catches sharp mask boundaries which a smooth
    // contour fit alone cannot guarantee safe.
    if(semantic_contour && changed && !ignore_protection && component==0){
        const double dx=1./(mesh.width()-1),dy=1./(mesh.height()-1);
        const auto cross=[](QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();};
        for(int y=0;y<mesh.height()-1;++y){
            if(cancelled && cancelled->load())throw EditError(QStringLiteral("人像变形已取消"));
            const auto* a=reinterpret_cast<const float*>(mesh.constScanLine(y));
            const auto* b=reinterpret_cast<const float*>(mesh.constScanLine(y+1));
            for(int x=0;x<mesh.width()-1;++x){
                const QPointF tl(a[x*4],a[x*4+1]),tr(dx+a[(x+1)*4],a[(x+1)*4+1]);
                const QPointF bl(b[x*4],dy+b[x*4+1]),br(dx+b[(x+1)*4],dy+b[(x+1)*4+1]);
                if(cross(tr-tl,bl-tl)<dx*dy*.05 || cross(br-bl,br-tr)<dx*dy*.05){
                    throw EditError(QStringLiteral("当前修形组合会造成轮廓折叠，请降低强度或柔化保护区边缘"));
                }
            }
        }
    }
    return changed?mesh:QImage{};
}
QImage portrait_deformation(QSize size,const QJsonObject& recipe,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled,bool validate_only) {
    return portrait_deformation_impl(size,recipe,amount,effect_mask,cancelled,validate_only);
}
QImage portrait_manual_deformation(QSize size,const QJsonObject& recipe,int person) {
    return portrait_deformation_impl(size,recipe,1,{},nullptr,false,person,false,2);
}
QImage portrait_deformation_with_manual(QSize size,const QJsonObject& recipe,int person,QImage& manual,const std::atomic_bool* cancelled,PortraitManualInputs* inputs) {
    if(inputs)*inputs={};
    return portrait_deformation_impl(size,recipe,1,{},cancelled,false,-1,false,0,person,&manual,inputs);
}
QJsonObject portrait_capture_protection(QSize size,const QJsonObject& recipe,int person) {
    const auto people=recipe["people"].toArray();
    if(person<0 || person>=people.size())throw EditError("无效的冻结人物");
    auto settings=people[person].toObject()["settings"].toObject();
    // At a mask edit, rebase to the current unprotected field and carry the
    // visible difference as a correction. Thus adding/removing protection is
    // pixel-neutral and thawing never reveals edits made under the freeze.
    // Keep automatic and manual contributions separate so restoring manual
    // liquify can discard its correction without erasing face/body edits.
    for(int component:{1,2}){
        auto current=portrait_deformation_impl(size,recipe,1,{},nullptr,false,person,false,component);
        auto raw=portrait_deformation_impl(size,recipe,1,{},nullptr,false,person,true,component);
        if(raw.isNull())raw=create_liquify_mesh(size);
        if(current.isNull())current=create_liquify_mesh(size);
        auto residual=current;
        for(int y=0;y<residual.height();++y){auto* dst=reinterpret_cast<float*>(residual.scanLine(y));const auto* src=reinterpret_cast<const float*>(raw.constScanLine(y));for(int x=0;x<residual.width();++x)for(int c=0;c<2;++c)dst[x*4+c]-=src[x*4+c];}
        const auto prefix=component==1?QStringLiteral("protectionAuto"):QStringLiteral("protectionManual");
        settings[prefix+"Anchor"]=liquify_json(raw);
        settings[prefix+"Residual"]=liquify_json(residual);
    }
    auto coordinates=portrait_deformation(size,recipe);if(coordinates.isNull())coordinates=create_liquify_mesh(size);
    settings["protectionCoordinates"]=liquify_json(coordinates);
    return settings;
}
QImage apply_portrait(const QImage& source,const QJsonObject& recipe,double amount,const QImage& effect_mask,const std::atomic_bool* cancelled) {
    if(cancelled && cancelled->load())throw EditError(QStringLiteral("人像处理已取消"));
    const auto colored=portrait_color_region(source,recipe,source.rect(),amount,cancelled,effect_mask);
    return apply_liquify(colored,portrait_deformation(source.size(),recipe,amount,effect_mask,cancelled),cancelled);
}
QImage portrait_region(const QImage& source,const QJsonObject& recipe,QRect region,const std::atomic_bool* cancelled) {
    region=region.intersected(source.rect());if(region.isEmpty())return {};
    const auto mesh=portrait_deformation(source.size(),recipe,1,{},cancelled);
    if(mesh.isNull())return portrait_color_region(source,recipe,region,1,cancelled);
    double maximum{};for(int y=0;y<mesh.height();++y){const auto* row=reinterpret_cast<const float*>(mesh.constScanLine(y));for(int x=0;x<mesh.width();++x)maximum=std::max({maximum,std::abs(double(row[x*4]))*source.width(),std::abs(double(row[x*4+1]))*source.height()});}
    const int margin=int(std::ceil(maximum))+2;const auto bounds=region.adjusted(-margin,-margin,margin,margin).intersected(source.rect());
    const auto colored=portrait_color_region(source,recipe,bounds,1,cancelled);
    auto output=allocate_image(region.size(),source.format());output.setColorSpace(source.colorSpace());
    auto sample=[](const QImage& image,double x,double y,int channel){x=std::clamp(x,0.,double(image.width()-1));y=std::clamp(y,0.,double(image.height()-1));const int ix=int(x),iy=int(y),jx=std::min(ix+1,image.width()-1),jy=std::min(iy+1,image.height()-1);const double fx=x-ix,fy=y-iy;const auto* a=reinterpret_cast<const float*>(image.constScanLine(iy));const auto* b=reinterpret_cast<const float*>(image.constScanLine(jy));return float((a[ix*4+channel]*(1-fx)+a[jx*4+channel]*fx)*(1-fy)+(b[ix*4+channel]*(1-fx)+b[jx*4+channel]*fx)*fy);};
    for(int y=0;y<region.height();++y){if(cancelled && cancelled->load())throw EditError(QStringLiteral("人像预览已取消"));auto* row=reinterpret_cast<float*>(output.scanLine(y));
        for(int x=0;x<region.width();++x){const int sx=x+region.x(),sy=y+region.y();const double mx=double(sx)/std::max(1,source.width()-1)*(mesh.width()-1),my=double(sy)/std::max(1,source.height()-1)*(mesh.height()-1);
            const double px=sx+double(sample(mesh,mx,my,0))*(source.width()-1),py=sy+double(sample(mesh,mx,my,1))*(source.height()-1);
            for(int c=0;c<4;++c)row[x*4+c]=sample(colored,px-bounds.x(),py-bounds.y(),c);}}
    return output;
}
}
