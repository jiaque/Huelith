#include "portrait_dialog.h"
#include "portrait_controls.h"
#include "raw_section_header.h"
#include "raw_preview_view.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/portrait.h"
#include "huelith/editing/project.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QTabBar>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QCache>
#include <QListWidget>
#include <QJsonDocument>
#include <QCryptographicHash>

namespace huelith::desktop {
namespace {
QJsonArray makeup_linear_color(const QColor& color){
    QImage sample(1,1,QImage::Format_RGBA8888);sample.fill(color);sample.setColorSpace(QColorSpace::SRgb);
    const auto linear=editing::to_working(sample);const auto* p=reinterpret_cast<const float*>(linear.constBits());return {p[0],p[1],p[2]};
}
void makeup_palette(QJsonObject& settings,const QString& field,const QString& value){
    if(field=="shadowPalette"){
        const QStringList primary{"#986c55","#b88493","#92785e","#71697c"},accent{"#725044","#88566e","#62523e","#4c4546"};
        const int i=std::clamp(value.toInt(),0,3);editing::portrait_set_style_color(settings,"makeup.shadow",makeup_linear_color(QColor(primary[i])));editing::portrait_set_style_color(settings,"makeup.shadowAccent",makeup_linear_color(QColor(accent[i])));editing::portrait_set_side_value(settings,"eyeShadowVersion",2);
    }else if(field=="irisPalette"){
        const QStringList colors{"#755647","#343138","#936b4d","#587b91","#5b7566","#7b7c82","#ad8252","#555767"};editing::portrait_set_style_color(settings,"makeup.iris",makeup_linear_color(QColor(colors[std::clamp(value.toInt(),0,7)])));
    }
}
QPixmap makeup_preview(const QImage& source,QJsonObject person,const QString& field,
                       const QString& value,const QString& key){
    if(source.isNull())return {};
    auto s=person["settings"].toObject();
    s[field]=value;s[field+".left"]=value;s[field+".right"]=value;
    s["linked"]=true;makeup_palette(s,field,value);s["strength"]=100;s["disabledParameters"]=QJsonObject{};
    s["enabled"]=QJsonObject{};
    QJsonObject p{{key,75},{key+".left",75},{key+".right",75}};
    if(field=="lipTexture"){
        p["makeup.lipGloss"]=value=="matte"?-65:value=="glass"?90:45;
        p["makeup.lipSparkle"]=value=="sparkle"?55:0;
    }
    if(field=="lipMaterial"){
        p["makeup.lipCoverage"]=value=="opaque"?75:value=="bitten"?20:30;
        p["makeup.lipFeather"]=value=="bitten"?25:10;
        p["makeup.lipBleed"]=value=="bleed"?50:0;p["makeup.lipBleedWidth"]=25;
    }
    if(field=="shadowPalette"){p["makeup.shadowAccent.left"]=35;p["makeup.shadowAccent.right"]=35;}
    if(field=="blushTexture")p["makeup.blushGloss"]=value=="matte"?-50:50;
    s["parameters"]=p;s["lipMaterialVersion"]=2;s["lipFinishVersion"]=2;s["mouthTopologyVersion"]=2;
    for(auto name:{"blushMaterialVersion","browMaterialVersion","eyeLashVersion","eyeLinerVersion","irisMaterialVersion"})s[name]=2;
    person["settings"]=s;
    static QCache<QByteArray,QPixmap> cache(96);
    auto hash=QCryptographicHash::hash(QByteArray::number(source.cacheKey())+QJsonDocument(person).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
    if(auto* cached=cache.object(hash))return *cached;
    const auto image=source.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    auto rendered=editing::display_image(editing::portrait_style_region(image,image.size(),QJsonObject{{"people",QJsonArray{person}}},image.rect(),1,nullptr,{}));
    const auto points=person["landmarks"].toArray();
    const auto face=person["face"].toArray();
    QRectF bounds;
    int first=key.contains("lip")?48:key.contains("brow")?17:36;
    int last=key.contains("lip")?60:key.contains("brow")?22:42;
    if(key.contains("blush")){bounds=QRectF(face[0].toDouble(),face[1].toDouble()+face[3].toDouble()*.3,face[2].toDouble(),face[3].toDouble()*.6);}
    else for(int i=first;i<last && i<points.size();++i){const auto a=points[i].toArray();bounds=bounds.united(QRectF(a[0].toDouble(),a[1].toDouble(),.001,.001));}
    if(bounds.isEmpty())return {};
    const double margin=std::max(bounds.width(),bounds.height())*.3;
    bounds.adjust(-margin,-margin,margin,margin);
    auto crop=QRectF(bounds.x()*image.width(),bounds.y()*image.height(),bounds.width()*image.width(),bounds.height()*image.height()).toAlignedRect().intersected(image.rect());
    auto thumbnail=QPixmap::fromImage(rendered.copy(crop).scaled(80,56,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    cache.insert(hash,new QPixmap(thumbnail));return thumbnail;
}
}
void PortraitDialog::build_makeup_parameters(){
    details_->hide();if(auto* w=findChild<QToolButton*>("portraitScopeHeader"))w->hide();
    const bool eyeEntry=property("portraitWorkspaceTask").toInt()==14;
    const int group=eyeEntry?2:property("portraitMakeupGroup").toInt();const QStringList titles{tr("唇妆"),tr("眉妆"),tr("眼妆"),tr("腮红"),tr("修容"),tr("装饰"),tr("美甲"),tr("整妆"),tr("底妆")};
    if(selected_<0){parameter_layout_->addWidget(new QLabel(tr("请先选择人物"),parameter_host_));return;}const auto settings=people_[std::size_t(selected_)].settings;
    if(auto* sides=findChild<QWidget*>("portraitSides"))sides->setVisible(group==1||group==2||group==3);
    for(int g:QList<int>{7,8,1,2,0,3,4,5,6}){if(eyeEntry && g!=2)continue;const bool open=eyeEntry || (g==group && !property("portraitMakeupClosed").toBool());auto heading=raw_section_header(parameter_host_,titles[g],QString("portraitMakeupGroup%1").arg(g),QString("portraitMakeupGroupEye%1").arg(g),open);heading.enabled->hide();parameter_layout_->addLayout(heading.layout);if(eyeEntry)heading.expand->hide();
        connect(heading.expand,&QToolButton::clicked,this,[this,g,group](bool on){finish_brush_gesture();finish_gesture();placement_key_.clear();setProperty("portraitMaterialSampleKey",QString{});setProperty("portraitMakeupLocate",QString{});locate_->setChecked(false);range_mode_=repair_mode_=0;view_->set_tool(0);setProperty("portraitMakeupGroup",g);setProperty("portraitMakeupClosed",g==group&&!on);setProperty("portraitMakeupBrush",QString{});build_parameters();});
        if(!open)continue;
        auto* body=new QWidget(parameter_host_);auto* bodyRows=new QVBoxLayout(body);bodyRows->setContentsMargins(8,0,8,12);bodyRows->setSpacing(6);parameter_layout_->addWidget(body);auto* outer=parameter_layout_;parameter_layout_=bodyRows;
        const QString pageProperty=QString("portraitMakeupPage%1").arg(g);int page=property(pageProperty.toUtf8()).toInt();
        QStringList pages=g==0?QStringList{tr("颜色"),tr("质地"),tr("晕染")}:g==1?QStringList{tr("眉形"),tr("颜色")}:g==2?QStringList{tr("眼影"),tr("眼线"),tr("睫毛"),tr("卧蚕"),tr("美瞳"),tr("眼神光")}:g==3?QStringList{tr("颜色"),tr("部位"),tr("质地")}:g==4?QStringList{tr("立体"),tr("高光"),tr("阴影")}:g==5?QStringList{tr("痣"),tr("雀斑"),tr("面饰"),tr("纹身")}:QStringList{};
        if(!pages.isEmpty()){page=std::clamp(page,0,int(pages.size())-1);auto* tabs=new QTabBar(body);tabs->setObjectName("portraitMakeupPages");for(const auto& name:pages)tabs->addTab(name);tabs->setExpanding(true);tabs->setCurrentIndex(page);bodyRows->addWidget(tabs);connect(tabs,&QTabBar::currentChanged,this,[this,pageProperty](int i){finish_brush_gesture();finish_gesture();setProperty(pageProperty.toUtf8(),i);setProperty("portraitMakeupBrush",QString{});placement_key_.clear();setProperty("portraitMaterialSampleKey",QString{});setProperty("portraitMakeupLocate",QString{});locate_->setChecked(false);range_mode_=repair_mode_=0;view_->set_tool(0);build_parameters();});}
        if(g==2 && page>=4 && settings.contains("irisMaterials")){
            const auto materials=settings["irisMaterials"].toObject();const bool linked=settings["linked"].toBool(true),left=settings["side"].toInt()==0;
            const bool available=((linked||left)&&materials["geometry.left"].toObject()["valid"].toBool()) || ((linked||!left)&&materials["geometry.right"].toObject()["valid"].toBool());
            if(!available){auto* note=new QLabel(tr("当前侧未识别到可用虹膜；闭眼或遮挡时不添加美瞳和眼神光。其他眼妆仍可使用。"),body);note->setObjectName("portraitEyeUnavailable");note->setWordWrap(true);bodyRows->addWidget(note);parameter_layout_=outer;continue;}
        }
        auto button=[&](QString text,QString name,auto callback){auto* b=new QToolButton(body);b->setText(text);b->setObjectName(name);bodyRows->addWidget(b);connect(b,&QToolButton::clicked,this,callback);return b;};
        auto rows=[&](QVBoxLayout* layout,QStringList keys){for(const auto& key:keys)if(auto p=editing::portrait_parameter(key))add_parameter_row(layout,key,p->label);};
        auto fold=[&](QString title,QStringList keys){auto* content=new QWidget(body);auto* r=new QVBoxLayout(content);r->setContentsMargins(0,0,0,0);rows(r,keys);auto h=raw_section_header(body,title,"portraitMakeupDetails","portraitMakeupDetailsEye",false);h.enabled->hide();bodyRows->addLayout(h.layout);bodyRows->addWidget(content);content->hide();connect(h.expand,&QToolButton::toggled,this,[content,b=h.expand](bool on){content->setVisible(on);b->setArrowType(on?Qt::DownArrow:Qt::RightArrow);});};
        auto color=[&](const QString& key){auto* host=new QWidget(body);auto* r=new QHBoxLayout(host);r->setContentsMargins(0,8,0,8);auto apply=[this,key](QColor c){if(!c.isValid()||selected_<0)return;finish_gesture();gesture_=true;QImage sample(1,1,QImage::Format_RGBA8888);sample.fill(c);sample.setColorSpace(QColorSpace::SRgb);auto image=editing::to_working(sample);const auto* p=reinterpret_cast<const float*>(image.constBits());auto& s=people_[std::size_t(selected_)].settings;editing::portrait_set_style_color(s,key,QJsonArray{p[0],p[1],p[2]});editing::portrait_set_side_value(s,"selectedColor_"+key,c.name());auto recent=s["makeupRecentColors"].toArray();for(int i=recent.size()-1;i>=0;--i)if(recent[i].toString()==c.name())recent.removeAt(i);recent.prepend(c.name());while(recent.size()>8)recent.removeLast();s["makeupRecentColors"]=recent;const auto params=s["parameters"].toObject();const double amount=params[key+(s["side"].toInt()==0?".left":".right")].toDouble(params[key].toDouble());if(amount==0)set_parameter(key,50);else remember();finish_gesture();build_parameters();};
            const QStringList palette=key.startsWith("makeup.shadow")?QStringList{"#b58b77","#986c55","#725044","#8b6877","#71697c"}:key.contains("brow")||key.contains("liner")||key.contains("lash")?QStringList{"#302722","#49372b","#755647","#8b6e50","#665d53"}:key.contains("iris")?QStringList{"#755647","#5b7566","#587b91","#81738d","#7d7667"}:QStringList{"#b54861","#c97675","#a9443d","#85513e","#b58b77"};
            for(auto hex:palette){auto* b=new QPushButton(host);b->setObjectName("portraitMakeupColor_"+key+"_"+hex.mid(1));b->setToolTip(hex);b->setFixedSize(34,32);const bool selected=editing::portrait_side_value(settings,"selectedColor_"+key,settings["side"].toInt()==0).toString()==hex;b->setCheckable(true);b->setChecked(selected);b->setText(selected?QString::fromUtf8("✓"):QString{});b->setStyleSheet("background:"+hex+(selected?";border:3px solid #63b4ff;color:white;font-weight:bold;border-radius:4px":";border:1px solid #68727b;border-radius:4px"));r->addWidget(b);connect(b,&QPushButton::clicked,this,[apply,hex]{apply(QColor(hex));});}auto* more=new QToolButton(host);more->setText(tr("更多…"));r->addWidget(more);connect(more,&QToolButton::clicked,this,[this,apply]{apply(QColorDialog::getColor(Qt::darkRed,this,tr("选择颜色")));});auto* sample=new QToolButton(host);sample->setText(tr("取色"));r->addWidget(sample);connect(sample,&QToolButton::clicked,this,[this,key]{setProperty("portraitMaterialSampleKey",key);setProperty("portraitMaterialSampleEnd",false);range_mode_=repair_mode_=0;placement_key_.clear();view_->set_tool(-1);status_->setText(tr("单击画布取色"));});bodyRows->addWidget(host);
            if(!settings["makeupRecentColors"].toArray().isEmpty()){auto* recent=new QComboBox(body);recent->addItem(tr("最近使用的颜色"));for(auto v:settings["makeupRecentColors"].toArray())recent->addItem(v.toString(),v.toString());bodyRows->addWidget(recent);connect(recent,&QComboBox::activated,this,[recent,apply](int i){if(i>0)apply(QColor(recent->itemData(i).toString()));});}
        };
        auto cards=[&](QString field,QStringList labels,QStringList values,QString key){auto* host=new QWidget(body);auto* r=new QGridLayout(host);r->setContentsMargins(0,8,0,8);for(int i=0;i<labels.size();++i){auto* b=portrait_style_card(host,labels[i]);b->setObjectName("portraitMakeupCard_"+field+"_"+values[i]);b->setChecked(editing::portrait_side_value(settings,field,settings["side"].toInt()==0).toString(settings[field].toString(values.front()))==values[i]);b->setIcon(QIcon(makeup_preview(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]),field,values[i],key)));b->setIconSize({80,56});r->addWidget(b,i/4,i%4);connect(b,&QToolButton::clicked,this,[this,field,value=values[i],defaultValue=values.front(),key]{finish_gesture();gesture_=true;auto& s=people_[std::size_t(selected_)].settings;
                const auto descriptor=editing::portrait_parameter(key);
                const bool bilateral=descriptor && descriptor->bilateral;
                auto bank=s["materialStrengths"].toObject();auto parameters=s["parameters"].toObject();
                for(int side=0;side<(bilateral?2:1);++side){if(bilateral && !s["linked"].toBool(true) && side!=s["side"].toInt())continue;const QString suffix=bilateral?(side==0?".left":".right"):"";const QString old=editing::portrait_side_value(s,field,side==0).toString(defaultValue);const QString slot=field+suffix;
                    auto values=bank[slot].toObject();values[old]=parameters[key+suffix].toDouble(parameters[key].toDouble());parameters[key+suffix]=values.contains(value)?values[value]:QJsonValue(50);bank[slot]=values;}
                s["materialStrengths"]=bank;s["parameters"]=parameters;
                makeup_palette(s,field,value);if(field=="shadowPalette")set_parameter("makeup.shadowAccent",25);if(field=="lipMaterial"||field=="lipTexture")s[field]=value;else editing::portrait_set_side_value(s,field,value);
                s["lipMaterialVersion"]=2;s["lipFinishVersion"]=2;s["mouthTopologyVersion"]=2;for(auto name:{"blushMaterialVersion","browMaterialVersion","eyeLashVersion","eyeLinerVersion","irisMaterialVersion"})editing::portrait_set_side_value(s,name,2);
                if(field=="lipTexture"){set_parameter("makeup.lipGloss",value=="matte"?-65:value=="glass"?90:45);set_parameter("makeup.lipSparkle",value=="sparkle"?55:0);}else if(field=="lipMaterial"){set_parameter("makeup.lipCoverage",value=="opaque"?75:value=="bitten"?20:30);set_parameter("makeup.lipFeather",value=="bitten"?25:10);set_parameter("makeup.lipBleed",value=="bleed"?50:0);if(value=="bleed")set_parameter("makeup.lipBleedWidth",25);}else if(field=="blushTexture"){set_parameter("makeup.blushGloss",value=="matte"?-50:50);}
                remember();finish_gesture();build_parameters();});}bodyRows->addWidget(host);};
        auto place=[&](QString key){button(tr("在画布上修正位置"),"portraitMakeupPosition",[this,key]{finish_brush_gesture();placement_key_=key;range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();});};
        auto anchors=[&](QString kind){button(tr("修正位置"),"portraitMakeupAnchors",[this,kind]{finish_brush_gesture();finish_gesture();placement_key_.clear();range_mode_=repair_mode_=0;setProperty("portraitMaterialSampleKey",QString{});setProperty("portraitMakeupLocate",kind);locate_->setChecked(true);view_->set_tool(-1);update_overlay();status_->setText(tr("拖动当前部位的定位点；空格临时抓手"));});};
        QString maskKey;
        if(g==0){maskKey="makeup.lip";if(page==0){color(maskKey);add_parameter_row(bodyRows,maskKey,tr("程度"));fold(tr("细节"),{"makeup.lipCoverage"});}else if(page==1){cards("lipTexture",{tr("雾面"),tr("光泽"),tr("玻璃"),tr("细闪")},{"matte","gloss","glass","sparkle"},"makeup.lip");add_parameter_row(bodyRows,"makeup.lipGloss",tr("光泽／雾面"));if(settings["lipTexture"].toString()=="sparkle"){add_parameter_row(bodyRows,"makeup.lipSparkle",tr("细闪程度"));fold(tr("细闪细节"),{"makeup.lipSparkleSize","makeup.lipSparkleDensity"});}}else{cards("lipMaterial",{tr("自然"),tr("咬唇"),tr("厚涂"),tr("外溢")},{"sheer","bitten","opaque","bleed"},"makeup.lip");add_parameter_row(bodyRows,"makeup.lipFeather",tr("边缘柔化"));if(settings["lipMaterial"].toString()=="bleed")rows(bodyRows,{"makeup.lipBleed","makeup.lipBleedWidth"});}}
        if(g==1){maskKey=page==0?"makeup.browShape":"makeup.brow";if(page==0){cards("browMaterial",{tr("自然"),tr("平眉"),tr("挑眉")},{"natural","straight","arched"},maskKey);add_parameter_row(bodyRows,maskKey,tr("程度"));anchors("brow");fold(tr("精细调整"),{"feature.browLength","feature.browThickness","feature.browArchHeight","feature.browArchPosition"});}else{color(maskKey);add_parameter_row(bodyRows,maskKey,tr("浓度"));}}
        if(g==2){const QStringList keys{"makeup.shadow","makeup.liner","makeup.lash","makeup.aegyosal","makeup.iris","makeup.catchlight"};maskKey=keys[page];if(page==0){cards("shadowPalette",{tr("自然棕"),tr("奶茶粉"),tr("大地色"),tr("烟熏灰")},{"0","1","2","3"},maskKey);anchors("eye");
            auto* details=new QWidget(body);auto* detailRows=new QVBoxLayout(details);detailRows->setContentsMargins(0,0,0,0);auto h=raw_section_header(body,tr("颜色与细节"),"portraitShadowDetails","portraitShadowDetailsEye",false);h.enabled->hide();bodyRows->addLayout(h.layout);bodyRows->addWidget(details);details->hide();connect(h.expand,&QToolButton::toggled,details,&QWidget::setVisible);
            auto* mainRows=bodyRows;bodyRows=detailRows;bodyRows->addWidget(new QLabel(tr("主色"),details));color(maskKey);bodyRows->addWidget(new QLabel(tr("尾色"),details));color("makeup.shadowAccent");rows(detailRows,{"makeup.shadowAccent","makeup.shadowFeather","makeup.crease"});bodyRows=mainRows;}if(page==1){anchors("eye");cards("linerMaterial",{tr("自然"),tr("圆眼"),tr("上扬")},{"soft","round","wing"},maskKey);color(maskKey);fold(tr("细节"),{"makeup.linerWidth","makeup.linerTail","makeup.linerLower"});}if(page==2){anchors("eye");cards("lashMaterial",{tr("自然"),tr("纤长"),tr("浓密")},{"light","long","volume"},maskKey);color(maskKey);fold(tr("细节"),{"makeup.lashLength","makeup.lashCurl"});}if(page==3){cards("aegyosalMaterial",{tr("自然卧蚕")},{"natural"},maskKey);fold(tr("细节"),{"makeup.aegyosalY","makeup.aegyosalWidth"});}if(page==4){cards("irisPalette",{tr("自然棕"),tr("深邃黑"),tr("蜜茶棕"),tr("灰蓝色"),tr("橄榄绿"),tr("雾灰"),tr("琥珀棕"),tr("星空灰")},{"0","1","2","3","4","5","6","7"},maskKey);color(maskKey);place("makeup.iris");fold(tr("细节"),{"makeup.irisTexture","makeup.irisSize","makeup.irisFeather"});}if(page==5){cards("catchMaterial",{tr("柔光"),tr("窗形"),tr("环形")},{"circle","window","ring"},maskKey);place("makeup.catch");fold(tr("细节"),{"makeup.catchSize"});}add_parameter_row(bodyRows,maskKey,tr("程度"));}
        if(g==3){maskKey="makeup.blush";if(page==0)color(maskKey);if(page==1){cards("blushMaterial",{tr("眼下"),tr("横向"),tr("斜向")},{"underEye","horizontal","diagonal"},maskKey);place(maskKey);}if(page==2){cards("blushTexture",{tr("哑光"),tr("柔光")},{"matte","soft"},maskKey);rows(bodyRows,{"makeup.blushGloss"});}add_parameter_row(bodyRows,maskKey,tr("程度"));fold(tr("细节"),{"makeup.blushSpread","makeup.blushSparkle"});}
        if(g==4){maskKey=page==2?"contour.shadow":"contour.highlight";if(page==0){setProperty("portraitSection13",0);build_style(13);rows(bodyRows,{"contour.highlight","contour.shadow"});}else add_parameter_row(bodyRows,maskKey,tr("程度"));button(tr("用修容笔局部调整"),"portraitMakeupToPaint",[this]{setProperty("portraitPaintType",0);if(auto* tasks=findChild<QListWidget*>("portraitTasks"))for(int i=0;i<tasks->count();++i)if(tasks->item(i)->data(Qt::UserRole).toInt()==12){tasks->setCurrentRow(i);break;}});}
        if(g==5){if(page==0||page==1){setProperty("portraitSection11",page==0?7:8);build_style(11);maskKey=page==0?"makeup.mole":"makeup.freckles";add_parameter_row(bodyRows,maskKey,tr("程度"));fold(tr("细节"),page==0?QStringList{"makeup.moleSize","makeup.moleFeather"}:QStringList{"makeup.frecklesSize","makeup.frecklesDensity"});}else build_decals(page==2?"makeup":"tattoo");}
        if(g==6){setProperty("portraitSection11",10);build_style(11);if(!settings["nails"].toArray().isEmpty())rows(bodyRows,{"makeup.nails","makeup.nailsGloss"});}
        if(g==7){setProperty("portraitSection11",5);build_style(11);}if(g==8){setProperty("portraitSection11",6);build_style(11);rows(bodyRows,{"makeup.foundation"});maskKey="makeup.foundation";}
        if(!maskKey.isEmpty()){
            const QString brush=property("portraitMakeupBrush").toString();button(brush==maskKey?tr("结束范围修正"):tr("修正范围"),"portraitMakeupCorrect",[this,maskKey,brush]{finish_brush_gesture();setProperty("portraitMakeupBrush",brush==maskKey?QString{}:maskKey);if(brush==maskKey){range_mode_=0;view_->set_tool(0);}else activate_hair_brush(maskKey,1);build_parameters();});
            if(brush==maskKey){auto* host=new QWidget(body);auto* r=new QHBoxLayout(host);for(int m:{1,2}){auto* b=new QPushButton(m==1?tr("补选"):tr("擦除"),host);r->addWidget(b);connect(b,&QPushButton::clicked,this,[this,maskKey,m]{activate_hair_brush(maskKey,m);});}bodyRows->addWidget(host);auto size=portrait_integer_row(body,tr("大小"),"portraitMakeupSize",1,500,brush_size_->value());bodyRows->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);}
        }
        parameter_layout_=outer;
    }
    parameter_layout_->addStretch();
}
}
