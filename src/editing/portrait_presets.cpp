#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <cmath>
#include <algorithm>
namespace huelith::editing {
QJsonObject portrait_makeup_combination(int style) {
    style=std::clamp(style,0,2);QJsonObject parameters;
    // A look consists of the same editable component parameters as individual tools.
    // Do not replace positioning, coverage edits, other retouching or person identity.
    for(const auto& p:portrait_parameters(11)){
        if(p.key=="makeup.foundation" || p.key.endsWith("X") || p.key.endsWith("Y") || p.key.endsWith("Spread"))continue;
        if(p.bilateral){parameters[p.key+".left"]=0;parameters[p.key+".right"]=0;}else parameters[p.key]=0;
    }
    parameters["makeup.lip"]=style==0?28:style==1?42:55;
    parameters["makeup.lipCoverage"]=style==2?65:25;
    parameters["makeup.lipFeather"]=15;
    parameters["makeup.lipGloss"]=style==1?25:0;
    for(const auto* side:{".left",".right"}){
        parameters[QString("makeup.brow")+side]=style==2?28:15;
        parameters[QString("makeup.shadow")+side]=style==0?15:style==1?30:42;
        parameters[QString("makeup.shadowAccent")+side]=style==0?5:style==1?12:25;
        parameters[QString("makeup.liner")+side]=style==0?20:style==1?30:45;
        parameters[QString("makeup.lash")+side]=style==0?20:style==1?28:40;
        parameters[QString("makeup.blush")+side]=style==0?18:style==1?30:22;
    }
    QJsonObject colors{{"makeup.lip",style==2?QJsonArray{.25,.065,.055}:style==1?QJsonArray{.27,.11,.13}:QJsonArray{.20,.11,.095}},
        {"makeup.blush",style==1?QJsonArray{.25,.12,.14}:QJsonArray{.24,.13,.12}},
        {"makeup.brow",QJsonArray{.035,.022,.016}},
        {"makeup.shadow",style==1?QJsonArray{.20,.115,.13}:QJsonArray{.19,.12,.08}},
        {"makeup.shadowAccent",QJsonArray{.10,.055,.04}},
        {"makeup.liner",QJsonArray{.015,.008,.005}}, {"makeup.lash",QJsonArray{.015,.008,.005}}};
    return {{"parameters",parameters},{"colors",colors},{"mouthTopologyVersion",2},{"lipMaterialVersion",2},{"lipFinishVersion",2},
        {"lipMaterial",style==2?"opaque":"sheer"},{"browMaterialVersion",2},{"browMaterial","natural"},
        {"eyeShadowVersion",2},{"eyeLinerVersion",2},{"eyeLashVersion",2},{"linerMaterial",style==2?"wing":"soft"},
        {"lashMaterial",style==2?"long":"light"},{"blushMaterialVersion",2},{"blushMaterial",style==1?"underEye":"diagonal"}};
}
QJsonObject portrait_portable_settings(const QJsonObject& settings,const QStringList& categories) {
    QJsonObject result;QJsonObject parameters,colors,enabled,disabled;
    // Whitelist semantic parameters only. Never transplant source coordinates, masks,
    // repairs, liquify meshes, detections or a source person's identity.
    auto sourceParameters=settings["parameters"].toObject();
    for(int panel=0;panel<15;++panel)for(const auto& descriptor:portrait_parameters(panel)){
        const bool sharedHair=descriptor.key=="head.hairVolume" && categories.contains("hair");
        if(!sharedHair && !categories.contains(descriptor.key.section('.',0,0)))continue;
        disabled[descriptor.key]=settings["disabledParameters"].toObject()[descriptor.key].toBool() || (sharedHair && !categories.contains("head") && !settings["enabled"].toObject()["head"].toBool(true));
        for(int side=0;side<(descriptor.bilateral?2:1);++side){const auto key=descriptor.key+(descriptor.bilateral?(side==0?".left":".right"):"");if(sourceParameters.contains(key))parameters[key]=sourceParameters[key];}
        const auto color=settings["colors"].toObject()[descriptor.key];if(color.isArray())colors[descriptor.key]=color;
        if(descriptor.bilateral)for(const auto* suffix:{".left",".right"}){const auto key=descriptor.key+suffix;const auto c=settings["colors"].toObject().value(key);if(c.isArray())colors[key]=c;}
    }
    if(categories.contains("head") && settings["headGeometryVersion"].toInt(1)<2 && sourceParameters.contains("head.proportion")){
        parameters["head.proportion"]=sourceParameters["head.proportion"];disabled["head.proportion"]=settings["disabledParameters"].toObject()["head.proportion"];
    }
    for(const auto& category:categories)enabled[category]=settings["enabled"].toObject()[category].toBool(true);
    result["parameters"]=parameters;result["colors"]=colors;result["enabled"]=enabled;result["disabledParameters"]=disabled;
    for(const auto* key:{"face.jawAngle","face.small","face.vShape","face.width","face.cheek","face.jaw","face.chin","face.chinWidth","face.smoothContour"})
        if(parameters.contains(key)){result["contourVersion"]=settings["contourVersion"].toInt(1);break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("feature.eye")){result["eyeGeometryVersion"]=settings["eyeGeometryVersion"].toInt(1);break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("feature.nose")){result["noseGeometryVersion"]=settings["noseGeometryVersion"].toInt(1);break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("feature.brow")){result["browGeometryVersion"]=settings["browGeometryVersion"].toInt(1);break;}
    if(parameters.contains("makeup.lip") || parameters.contains("makeup.lipGloss") || parameters.contains("makeup.lipFeather"))result["mouthTopologyVersion"]=settings["mouthTopologyVersion"].toInt(1);
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("feature.lip") || it.key().startsWith("feature.smile")){
        result["mouthGeometryVersion"]=settings["mouthGeometryVersion"].toInt(1);result["mouthTopologyVersion"]=settings["mouthTopologyVersion"].toInt(1);
    }
    for(const auto* key:{"head.midface","head.philtrum","head.lowerface"})if(parameters.contains(key)){result["proportionGeometryVersion"]=settings["proportionGeometryVersion"].toInt(1);break;}
    if(parameters.contains("head.roll") || parameters.contains("head.size") || parameters.contains("head.proportion"))result["headGeometryVersion"]=settings["headGeometryVersion"].toInt(1);
    if(parameters.contains("face.forehead") || parameters.contains("head.crown") || parameters.contains("head.hairline") || parameters.contains("head.hairVolume"))result["scalpGeometryVersion"]=settings["scalpGeometryVersion"].toInt(1);
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("body.arm") || it.key().startsWith("body.upperArm") || it.key().startsWith("body.legWidth") || it.key().startsWith("body.thigh") || it.key().startsWith("body.calf")){result["limbGeometryVersion"]=settings["limbGeometryVersion"].toInt(1);break;}
    if(parameters.contains("body.back"))result["backMode"]=(settings["backContour"].toArray().size()==3 || settings["backMode"].toString()=="manual")?"manual":"automatic";
    if(parameters.contains("body.legLength.left") || parameters.contains("body.legLength.right"))result["legLengthVersion"]=settings["legLengthVersion"].toInt(1);
    if(parameters.contains("body.shoulder.left") || parameters.contains("body.shoulder.right"))result["shoulderGeometryVersion"]=settings["shoulderGeometryVersion"].toInt(1);
    if(parameters.contains("body.trapezius.left") || parameters.contains("body.trapezius.right"))result["trapeziusGeometryVersion"]=settings["trapeziusGeometryVersion"].toInt(1);
    if(parameters.contains("body.neck"))result["neckGeometryVersion"]=settings["neckGeometryVersion"].toInt(1);
    if(parameters.contains("body.height"))result["heightVersion"]=settings["heightVersion"].toInt(1);
    if(parameters.contains("body.waist"))result["waistGeometryVersion"]=settings["waistGeometryVersion"].toInt(1);
    if(parameters.contains("teeth.yellow") || parameters.contains("teeth.light"))result["teethColorVersion"]=settings["teethColorVersion"].toInt(1);
    if(parameters.contains("repair.conceal"))result["concealVersion"]=settings["concealVersion"].toInt(1);
    if(parameters.contains("color.custom"))result["skinCustomVersion"]=settings["skinCustomVersion"].toInt(1);
    if(parameters.contains("skin.uniform"))result["skinUniformVersion"]=settings["skinUniformVersion"].toInt(1);
    if(parameters.contains("color.light"))result["skinLightVersion"]=settings["skinLightVersion"].toInt(1);
    if(parameters.contains("eye.red"))result["redEyeVersion"]=settings["redEyeVersion"].toInt(1);
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("makeup.iris") || it.key().startsWith("makeup.catch")){result["irisMaterialVersion"]=settings["irisMaterialVersion"].toInt(1);break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("makeup.lash")){result["eyeLashVersion"]=settings["eyeLashVersion"].toInt(1);result["lashMaterial"]=settings["lashMaterial"].toString("light");break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("makeup.liner")){result["eyeLinerVersion"]=settings["eyeLinerVersion"].toInt(1);result["linerMaterial"]=settings["linerMaterial"].toString("soft");break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("makeup.shadow")){result["eyeShadowVersion"]=settings["eyeShadowVersion"].toInt(1);break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("makeup.brow")){result["browMaterialVersion"]=settings["browMaterialVersion"].toInt(1);result["browMaterial"]=settings["browMaterial"].toString("natural");break;}
    for(auto it=parameters.begin();it!=parameters.end();++it)if(it.key().startsWith("makeup.blush")){result["blushMaterialVersion"]=settings["blushMaterialVersion"].toInt(1);result["blushMaterial"]=settings["blushMaterial"].toString("underEye");break;}
    if(parameters.contains("makeup.crease.left") || parameters.contains("makeup.crease.right"))result["creaseMaterial"]=settings["creaseMaterial"].toString("fan");
    if(parameters.contains("makeup.lip") || parameters.contains("makeup.lipCoverage") || parameters.contains("makeup.lipFeather")){result["lipMaterialVersion"]=settings["lipMaterialVersion"].toInt(1);result["lipMaterial"]=settings["lipMaterial"].toString("sheer");}
    if(parameters.contains("makeup.tattoo"))result["tattooMaterial"]=settings["tattooMaterial"].toString("leaf");
    if(parameters.contains("makeup.sticker")){result["stickerMaterial"]=settings["stickerMaterial"].toString("heart");result["stickerSide"]=settings["stickerSide"].toString("left");}
    if(parameters.contains("makeup.freckles"))result["freckleSeed"]=settings["freckleSeed"].toDouble(1729);
    if(parameters.contains("hair.smooth"))result["hairSmoothVersion"]=settings["hairSmoothVersion"].toInt(1);
    if(parameters.contains("hair.accessory")){result["hairAccessoryVersion"]=settings["hairAccessoryVersion"].toInt(1);result["hairAccessoryOcclusion"]=settings["hairAccessoryOcclusion"].toString("manual");}
    if(parameters.contains("hair.shine"))result["hairShineVersion"]=settings["hairShineVersion"].toInt(1);
    if(parameters.contains("contour.highlight") || parameters.contains("contour.shadow"))result["contourLightVersion"]=settings["contourLightVersion"].toInt(1);
    if(parameters.contains("contour.highlight") || parameters.contains("contour.shadow"))result["contourAutoLighting"]=settings["contourAutoLighting"].toBool();
    if(categories.contains("hair") && (settings.contains("hairEdgeVersion") || parameters.contains("hair.color")))result["hairEdgeVersion"]=settings["hairEdgeVersion"].toInt();
    if(categories.contains("hair") && (parameters.contains("hair.color") || settings.contains("hairColorVersion")))result["hairColorVersion"]=settings["hairColorVersion"].toInt(1);
    if(parameters.contains("face.volume"))result["cheekVolumeVersion"]=settings["cheekVolumeVersion"].toInt(1);
    if(parameters.contains("makeup.lipGloss"))result["lipFinishVersion"]=settings["lipFinishVersion"].toInt(1);
    if(parameters.contains("eye.white"))result["eyeWhiteVersion"]=settings["eyeWhiteVersion"].toInt(1);
    if(parameters.contains("eye.bags"))result["eyeBagsVersion"]=settings["eyeBagsVersion"].toInt(1);
    if(parameters.contains("eye.circles"))result["eyeCirclesVersion"]=settings["eyeCirclesVersion"].toInt(1);
    if(parameters.contains("skin.texture"))result["textureVersion"]=settings["textureVersion"].toInt(1);
    if(parameters.contains("skin.smooth"))result["smoothScopeVersion"]=settings["smoothScopeVersion"].toInt(1);
    if(parameters.contains("skin.smooth"))result["smoothVersion"]=settings["smoothVersion"].toInt(1);
    if(parameters.contains("skin.smooth") && settings.contains("smoothStyle"))result["smoothStyle"]=settings["smoothStyle"];
    if(parameters.contains("skin.oil"))result["oilVersion"]=settings["oilVersion"].toInt(1);
    if(parameters.contains("color.balance"))result["skinBalanceVersion"]=settings["skinBalanceVersion"].toInt(1);
    if(parameters.contains("color.redness"))result["rednessVersion"]=settings["rednessVersion"].toInt(1);
    if(parameters.contains("color.warmth") || parameters.contains("color.rosy"))result["colorChromaVersion"]=settings["colorChromaVersion"].toInt(1);
    if(categories.contains("color") && settings.contains("skinColor"))result["skinColor"]=settings["skinColor"];
    for(const auto& pair:std::vector<std::pair<QString,QString>>{{"makeup","catchMaterial"},{"hair","hairMaterial"},{"contour","bodyMaterial"}})if(categories.contains(pair.first) && settings.contains(pair.second))result[pair.second]=settings[pair.second];
    if(categories.contains("makeup"))for(const auto* name:{"irisMaterialVersion","browMaterialVersion","eyeShadowVersion","eyeLinerVersion","eyeLashVersion","browMaterial","catchMaterial","linerMaterial","lashMaterial","blushMaterialVersion","blushMaterial","creaseMaterial"})for(const auto* suffix:{".left",".right"}){const QString key=QString(name)+suffix;if(settings.contains(key))result[key]=settings[key];}
    return result;
}
QJsonObject portrait_merge_preset(QJsonObject target,const QJsonObject& preset,double strength) {
    if(!std::isfinite(strength) || strength<0 || strength>1)throw EditError("预设强度无效");
    const auto clean=portrait_portable_settings(preset,{"face","feature","head","body","skin","color","eye","teeth","makeup","hair","contour","light"});
    if(strength>0 && clean["backMode"].toString()=="manual" && clean["parameters"].toObject()["body.back"].toDouble()!=0 && clean["enabled"].toObject()["body"].toBool(true) && !clean["disabledParameters"].toObject()["body.back"].toBool() && target["backContour"].toArray().size()!=3)
        throw EditError("此预设使用手动上背轮廓，请先在目标照片定位上背轮廓，再应用预设");
    if(clean["parameters"].toObject().contains("face.tearFill.left") || clean["parameters"].toObject().contains("face.tearFill.right"))target.remove("tearVolume");if(clean["parameters"].toObject().contains("face.malarTone.left") || clean["parameters"].toObject().contains("face.malarTone.right"))target.remove("malarVolume");if(clean["parameters"].toObject().contains("face.cheekTone.left") || clean["parameters"].toObject().contains("face.cheekTone.right"))target.remove("cheekVolume");if(clean["parameters"].toObject().contains("face.orbitalTone.left") || clean["parameters"].toObject().contains("face.orbitalTone.right"))target.remove("orbitalVolume");if(clean["parameters"].toObject().contains("face.perioralTone.left") || clean["parameters"].toObject().contains("face.perioralTone.right"))target.remove("perioralVolume");
    if(clean["parameters"].toObject().contains("face.foreheadFill"))target.remove("foreheadVolume");
    for(const auto* key:{"stickerMaterial","stickerSide","tattooMaterial"})if(clean.contains(key))target[key]=clean[key];
    if(clean.contains("freckleSeed"))target["freckleSeed"]=clean["freckleSeed"];
    if(clean.contains("cheekVolumeVersion"))target["cheekVolumeVersion"]=clean["cheekVolumeVersion"];
    if(clean.contains("contourLightVersion"))target["contourLightVersion"]=clean["contourLightVersion"];
    if(clean.contains("contourAutoLighting")){target["contourAutoLighting"]=clean["contourAutoLighting"];target.remove("contourLighting");}
    auto parameters=target["parameters"].toObject();const auto values=clean["parameters"].toObject();for(auto i=values.begin();i!=values.end();++i){const double value=i.value().toDouble();if(!std::isfinite(value) || std::abs(value)>100)throw EditError("预设参数无效");parameters[i.key()]=value*strength;}if(clean["headGeometryVersion"].toInt(1)>=2 && values.contains("head.size"))parameters.remove("head.proportion");target["parameters"]=parameters;if(values.contains("skin.smooth") && !clean.contains("smoothStyle"))target.remove("smoothStyle");
    for(const auto* name:{"colors","enabled","disabledParameters"}){auto object=target[name].toObject();const auto from=clean[name].toObject();if(QString(name)=="colors")for(auto i=from.begin();i!=from.end();++i)if(!i.key().endsWith(".left") && !i.key().endsWith(".right")){object.remove(i.key()+".left");object.remove(i.key()+".right");}for(auto i=from.begin();i!=from.end();++i){bool touched=QString(name)=="colors";for(auto v=values.begin();v!=values.end() && !touched;++v)touched=QString(name)=="enabled"?v.key().section('.',0,0)==i.key():(v.key()==i.key() || v.key()==i.key()+".left" || v.key()==i.key()+".right");if(touched || preset[name].toObject().contains(i.key()))object[i.key()]=i.value();}target[name]=object;}
    for(const auto* name:{"hairShineVersion","hairColorVersion","hairSmoothVersion","hairEdgeVersion","hairAccessoryVersion","hairAccessoryOcclusion","skinColor","catchMaterial","hairMaterial","bodyMaterial","contourVersion","eyeGeometryVersion","noseGeometryVersion","mouthTopologyVersion","mouthGeometryVersion","browGeometryVersion","proportionGeometryVersion","headGeometryVersion","scalpGeometryVersion","limbGeometryVersion","legLengthVersion","heightVersion","neckGeometryVersion","shoulderGeometryVersion","trapeziusGeometryVersion","waistGeometryVersion","colorChromaVersion","rednessVersion","oilVersion","smoothVersion","smoothStyle","smoothScopeVersion","textureVersion","eyeCirclesVersion","eyeBagsVersion","teethColorVersion","eyeWhiteVersion","redEyeVersion","skinLightVersion","skinBalanceVersion","skinUniformVersion","skinCustomVersion","concealVersion","irisMaterialVersion","browMaterialVersion","browMaterial","eyeShadowVersion","eyeLinerVersion","linerMaterial","eyeLashVersion","lashMaterial","blushMaterialVersion","blushMaterial","creaseMaterial","lipMaterialVersion","lipMaterial","lipFinishVersion"})if(clean.contains(name)){target[name]=clean[name];target.remove(QString(name)+".left");target.remove(QString(name)+".right");}
    for(const auto* name:{"irisMaterialVersion","browMaterialVersion","eyeShadowVersion","eyeLinerVersion","eyeLashVersion","browMaterial","catchMaterial","linerMaterial","lashMaterial","blushMaterialVersion","blushMaterial","creaseMaterial"})for(const auto* suffix:{".left",".right"}){const QString key=QString(name)+suffix;if(clean.contains(key))target[key]=clean[key];}
    return target;
}
}
