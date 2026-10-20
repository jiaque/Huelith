#include "portrait_dialog.h"
#include "portrait_controls.h"
#include "raw_section_header.h"
#include "raw_preview_view.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/portrait.h"
#include "huelith/editing/project.h"
#include <QVBoxLayout>
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

namespace huelith::desktop {
void PortraitDialog::build_makeup_parameters(){
    details_->hide();if(auto* w=findChild<QToolButton*>("portraitScopeHeader"))w->hide();
    const int group=property("portraitMakeupGroup").toInt();const QStringList titles{tr("唇妆"),tr("眉妆"),tr("眼妆"),tr("腮红"),tr("修容"),tr("装饰"),tr("美甲"),tr("整妆"),tr("底妆")};
    if(selected_<0){parameter_layout_->addWidget(new QLabel(tr("请先选择人物"),parameter_host_));return;}const auto settings=people_[std::size_t(selected_)].settings;
    if(auto* sides=findChild<QWidget*>("portraitSides"))sides->setVisible(group==1||group==2||group==3);
    for(int g=0;g<titles.size();++g){const bool open=g==group && !property("portraitMakeupClosed").toBool();auto heading=raw_section_header(parameter_host_,titles[g],QString("portraitMakeupGroup%1").arg(g),QString("portraitMakeupGroupEye%1").arg(g),open);heading.enabled->hide();parameter_layout_->addLayout(heading.layout);
        connect(heading.expand,&QToolButton::clicked,this,[this,g,group](bool on){finish_brush_gesture();finish_gesture();placement_key_.clear();range_mode_=repair_mode_=0;view_->set_tool(0);setProperty("portraitMakeupGroup",g);setProperty("portraitMakeupClosed",g==group&&!on);setProperty("portraitMakeupBrush",QString{});build_parameters();});
        if(!open)continue;
        auto* body=new QWidget(parameter_host_);auto* bodyRows=new QVBoxLayout(body);bodyRows->setContentsMargins(8,0,8,12);bodyRows->setSpacing(6);parameter_layout_->addWidget(body);auto* outer=parameter_layout_;parameter_layout_=bodyRows;
        const QString pageProperty=QString("portraitMakeupPage%1").arg(g);int page=property(pageProperty.toUtf8()).toInt();
        QStringList pages=g==0?QStringList{tr("颜色"),tr("质地"),tr("晕染")}:g==1?QStringList{tr("眉形"),tr("颜色")}:g==2?QStringList{tr("眼影"),tr("眼线"),tr("睫毛"),tr("卧蚕"),tr("美瞳"),tr("眼神光")}:g==3?QStringList{tr("颜色"),tr("部位"),tr("质地")}:g==4?QStringList{tr("立体"),tr("高光"),tr("阴影")}:g==5?QStringList{tr("痣"),tr("雀斑"),tr("面饰"),tr("纹身")}:QStringList{};
        if(!pages.isEmpty()){page=std::clamp(page,0,int(pages.size())-1);auto* tabs=new QTabBar(body);tabs->setObjectName("portraitMakeupPages");for(const auto& name:pages)tabs->addTab(name);tabs->setExpanding(true);tabs->setCurrentIndex(page);bodyRows->addWidget(tabs);connect(tabs,&QTabBar::currentChanged,this,[this,pageProperty](int i){finish_brush_gesture();finish_gesture();setProperty(pageProperty.toUtf8(),i);setProperty("portraitMakeupBrush",QString{});placement_key_.clear();range_mode_=repair_mode_=0;view_->set_tool(0);build_parameters();});}
        auto button=[&](QString text,QString name,auto callback){auto* b=new QToolButton(body);b->setText(text);b->setObjectName(name);bodyRows->addWidget(b);connect(b,&QToolButton::clicked,this,callback);return b;};
        auto rows=[&](QVBoxLayout* layout,QStringList keys){for(const auto& key:keys)if(auto p=editing::portrait_parameter(key))add_parameter_row(layout,key,p->label);};
        auto fold=[&](QString title,QStringList keys){auto* content=new QWidget(body);auto* r=new QVBoxLayout(content);r->setContentsMargins(0,0,0,0);rows(r,keys);auto h=raw_section_header(body,title,"portraitMakeupDetails","portraitMakeupDetailsEye",false);h.enabled->hide();bodyRows->addLayout(h.layout);bodyRows->addWidget(content);content->hide();connect(h.expand,&QToolButton::toggled,this,[content,b=h.expand](bool on){content->setVisible(on);b->setArrowType(on?Qt::DownArrow:Qt::RightArrow);});};
        auto color=[&](const QString& key){auto* host=new QWidget(body);auto* r=new QHBoxLayout(host);r->setContentsMargins(0,8,0,8);auto apply=[this,key](QColor c){if(!c.isValid()||selected_<0)return;finish_gesture();gesture_=true;QImage sample(1,1,QImage::Format_RGBA8888);sample.fill(c);sample.setColorSpace(QColorSpace::SRgb);auto image=editing::to_working(sample);const auto* p=reinterpret_cast<const float*>(image.constBits());auto& s=people_[std::size_t(selected_)].settings;editing::portrait_set_style_color(s,key,QJsonArray{p[0],p[1],p[2]});auto recent=s["makeupRecentColors"].toArray();for(int i=recent.size()-1;i>=0;--i)if(recent[i].toString()==c.name())recent.removeAt(i);recent.prepend(c.name());while(recent.size()>8)recent.removeLast();s["makeupRecentColors"]=recent;const auto params=s["parameters"].toObject();const double amount=params[key+(s["side"].toInt()==0?".left":".right")].toDouble(params[key].toDouble());if(amount==0)set_parameter(key,50);else remember();finish_gesture();build_parameters();};
            const QStringList palette=key.contains("brow")||key.contains("liner")||key.contains("lash")?QStringList{"#302722","#49372b","#755647","#8b6e50","#665d53"}:key.contains("iris")?QStringList{"#755647","#5b7566","#587b91","#81738d","#7d7667"}:QStringList{"#b54861","#c97675","#a9443d","#85513e","#b58b77"};
            for(auto hex:palette){auto* b=new QPushButton(host);b->setObjectName("portraitMakeupColor_"+hex.mid(1));b->setToolTip(hex);b->setFixedSize(34,32);b->setStyleSheet("background:"+hex+";border:1px solid #68727b;border-radius:4px");r->addWidget(b);connect(b,&QPushButton::clicked,this,[apply,hex]{apply(QColor(hex));});}auto* more=new QToolButton(host);more->setText(tr("更多…"));r->addWidget(more);connect(more,&QToolButton::clicked,this,[this,apply]{apply(QColorDialog::getColor(Qt::darkRed,this,tr("选择颜色")));});auto* sample=new QToolButton(host);sample->setText(tr("取色"));r->addWidget(sample);connect(sample,&QToolButton::clicked,this,[this,key]{setProperty("portraitMaterialSampleKey",key);setProperty("portraitMaterialSampleEnd",false);range_mode_=repair_mode_=0;placement_key_.clear();view_->set_tool(-1);status_->setText(tr("单击画布取色"));});bodyRows->addWidget(host);
            if(!settings["makeupRecentColors"].toArray().isEmpty()){auto* recent=new QComboBox(body);recent->addItem(tr("最近使用的颜色"));for(auto v:settings["makeupRecentColors"].toArray())recent->addItem(v.toString(),v.toString());bodyRows->addWidget(recent);connect(recent,&QComboBox::activated,this,[recent,apply](int i){if(i>0)apply(QColor(recent->itemData(i).toString()));});}
        };
        auto cards=[&](QString field,QStringList labels,QStringList values,QString key){auto* host=new QWidget(body);auto* r=new QHBoxLayout(host);r->setContentsMargins(0,8,0,8);for(int i=0;i<labels.size();++i){auto* b=portrait_style_card(host,labels[i]);b->setObjectName("portraitMakeupCard_"+field+"_"+values[i]);b->setChecked(editing::portrait_side_value(settings,field,settings["side"].toInt()==0).toString(settings[field].toString(values.front()))==values[i]);r->addWidget(b);connect(b,&QToolButton::clicked,this,[this,field,value=values[i],key]{finish_gesture();gesture_=true;auto& s=people_[std::size_t(selected_)].settings;
                if(field=="lipMaterial"||field=="lipTexture")s[field]=value;else editing::portrait_set_side_value(s,field,value);
                s["lipMaterialVersion"]=2;s["mouthTopologyVersion"]=2;for(auto name:{"blushMaterialVersion","browMaterialVersion","eyeLashVersion","eyeLinerVersion","irisMaterialVersion"})editing::portrait_set_side_value(s,name,2);
                if(field=="lipTexture"){set_parameter("makeup.lipGloss",value=="matte"?-65:value=="glass"?90:45);set_parameter("makeup.lipSparkle",value=="sparkle"?55:0);}else if(field=="lipMaterial"){set_parameter("makeup.lipCoverage",value=="opaque"?75:value=="bitten"?20:30);set_parameter("makeup.lipFeather",value=="bitten"?25:10);set_parameter("makeup.lipBleed",value=="bleed"?50:0);if(value=="bleed")set_parameter("makeup.lipBleedWidth",25);}else if(field=="blushTexture"){set_parameter("makeup.blushGloss",value=="matte"?-50:50);}
                const auto p=s["parameters"].toObject();if(p[key+(s["side"].toInt()==0?".left":".right")].toDouble(p[key].toDouble())==0)set_parameter(key,50);else remember();finish_gesture();build_parameters();});}bodyRows->addWidget(host);};
        auto place=[&](QString key){button(tr("在画布上修正位置"),"portraitMakeupPosition",[this,key]{finish_brush_gesture();placement_key_=key;range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();});};
        QString maskKey;
        if(g==0){maskKey="makeup.lip";if(page==0){color(maskKey);add_parameter_row(bodyRows,maskKey,tr("程度"));fold(tr("细节"),{"makeup.lipCoverage"});}else if(page==1){cards("lipTexture",{tr("雾面"),tr("光泽"),tr("玻璃"),tr("细闪")},{"matte","gloss","glass","sparkle"},"makeup.lip");add_parameter_row(bodyRows,"makeup.lipGloss",tr("光泽／雾面"));if(settings["lipTexture"].toString()=="sparkle"){add_parameter_row(bodyRows,"makeup.lipSparkle",tr("细闪程度"));fold(tr("细闪细节"),{"makeup.lipSparkleSize","makeup.lipSparkleDensity"});}}else{cards("lipMaterial",{tr("自然"),tr("咬唇"),tr("厚涂"),tr("外溢")},{"sheer","bitten","opaque","bleed"},"makeup.lip");add_parameter_row(bodyRows,"makeup.lipFeather",tr("边缘柔化"));if(settings["lipMaterial"].toString()=="bleed")rows(bodyRows,{"makeup.lipBleed","makeup.lipBleedWidth"});}}
        if(g==1){maskKey=page==0?"makeup.browShape":"makeup.brow";if(page==0){cards("browMaterial",{tr("自然"),tr("平眉"),tr("挑眉")},{"natural","straight","arched"},maskKey);add_parameter_row(bodyRows,maskKey,tr("程度"));fold(tr("精细调整"),{"feature.browLength","feature.browThickness","feature.browArchHeight","feature.browArchPosition"});}else{color(maskKey);add_parameter_row(bodyRows,maskKey,tr("浓度"));}}
        if(g==2){const QStringList keys{"makeup.shadow","makeup.liner","makeup.lash","makeup.aegyosal","makeup.iris","makeup.catchlight"};maskKey=keys[page];if(page==0){color(maskKey);fold(tr("尾色与细节"),{"makeup.shadowAccent","makeup.crease"});}if(page==1){cards("linerMaterial",{tr("自然"),tr("圆眼"),tr("上扬")},{"soft","round","wing"},maskKey);color(maskKey);fold(tr("细节"),{"makeup.linerLower"});}if(page==2){cards("lashMaterial",{tr("自然"),tr("纤长"),tr("浓密")},{"light","long","volume"},maskKey);color(maskKey);}if(page==3)cards("aegyosalMaterial",{tr("自然卧蚕")},{"natural"},maskKey);if(page==4){color(maskKey);fold(tr("细节"),{"makeup.irisTexture"});}if(page==5){cards("catchMaterial",{tr("柔光"),tr("窗形"),tr("环形")},{"circle","window","ring"},maskKey);place("makeup.catch");fold(tr("细节"),{"makeup.catchSize"});}add_parameter_row(bodyRows,maskKey,tr("程度"));}
        if(g==3){maskKey="makeup.blush";if(page==0)color(maskKey);if(page==1){cards("blushMaterial",{tr("眼下"),tr("横向"),tr("斜向")},{"underEye","horizontal","diagonal"},maskKey);place(maskKey);}if(page==2){cards("blushTexture",{tr("哑光"),tr("柔光")},{"matte","soft"},maskKey);rows(bodyRows,{"makeup.blushGloss"});}add_parameter_row(bodyRows,maskKey,tr("程度"));fold(tr("细节"),{"makeup.blushSpread","makeup.blushSparkle"});}
        if(g==4){maskKey=page==2?"contour.shadow":"contour.highlight";if(page==0){setProperty("portraitSection13",0);build_style(13);rows(bodyRows,{"contour.highlight","contour.shadow"});}else add_parameter_row(bodyRows,maskKey,tr("程度"));button(tr("用修容笔局部调整"),"portraitMakeupToPaint",[this]{setProperty("portraitPaintType",0);if(auto* tasks=findChild<QListWidget*>("portraitTasks"))tasks->setCurrentRow(12);});}
        if(g==5){if(page==0||page==1){setProperty("portraitSection11",page==0?7:8);build_style(11);maskKey=page==0?"makeup.mole":"makeup.freckles";add_parameter_row(bodyRows,maskKey,tr("程度"));fold(tr("细节"),page==0?QStringList{"makeup.moleSize","makeup.moleFeather"}:QStringList{"makeup.frecklesSize","makeup.frecklesDensity"});}else build_decals(page==2?"makeup":"tattoo");}
        if(g==6){setProperty("portraitSection11",10);build_style(11);rows(bodyRows,{"makeup.nails","makeup.nailsGloss"});}
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
