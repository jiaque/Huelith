#include "portrait_dialog.h"
#include "portrait_controls.h"
#include "raw_section_header.h"
#include "raw_preview_view.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/portrait.h"
#include "huelith/editing/project.h"
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QDir>
#include "huelith/editing/ai_worker.h"
#include <QRegularExpression>
#include <QTabBar>
#include <QToolButton>
#include <QPainter>
#include <QButtonGroup>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QColorDialog>
#include <QTabWidget>
#include <QSignalBlocker>
#include <QUuid>
#include <QMouseEvent>
#include <QLineF>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
void PortraitDialog::build_makeup_accordion(){
    auto* tabs=parameter_host_->findChild<QTabBar*>("portraitStyleTabs");
    if(!tabs)return;
    const int current=tabs->currentIndex(),start=parameter_layout_->indexOf(tabs)+1;
    auto* body=new QWidget(parameter_host_);body->setObjectName("portraitMakeupContent");
    auto* content=new QVBoxLayout(body);content->setContentsMargins(8,4,8,10);content->setSpacing(6);
    auto* corrections=new QWidget(body);auto* correctionRows=new QVBoxLayout(corrections);correctionRows->setContentsMargins(0,0,0,0);
    auto* details=new QWidget(body);auto* detailRows=new QVBoxLayout(details);detailRows->setContentsMargins(0,0,0,0);
    bool primary=false;
    while(parameter_layout_->count()>start){
        auto* item=parameter_layout_->takeAt(start);
        if(auto* widget=item->widget()){
            const auto name=widget->objectName();
            if(name.startsWith("portraitRefresh") || name.endsWith("Anchors"))
                correctionRows->addWidget(widget);
            else if(!widget->findChildren<QToolButton*>(QRegularExpression("^portraitGroup_")).isEmpty()){
                if(!primary){
                    primary=true;content->addWidget(widget);
                    // The main category already provides the header and effect switch.
                    if(auto* row=widget->layout()->itemAt(0)->layout())
                        for(int i=0;i<row->count();++i)if(auto* control=row->itemAt(i)->widget())control->hide();
                    if(auto* rows=widget->layout()->itemAt(1)->widget())rows->show();
                }else detailRows->addWidget(widget);
            }else content->addWidget(widget);
        }else if(item->layout()){content->addItem(item);continue;}
        delete item;
    }
    if(detailRows->count()){
        auto header=raw_section_header(body,tr("细节"),"portraitMakeupDetails","portraitMakeupDetailsEye",false);
        header.enabled->hide();content->addLayout(header.layout);content->addWidget(details);details->hide();
        connect(header.expand,&QToolButton::toggled,this,[details,button=header.expand](bool open){details->setVisible(open);button->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    }else delete details;
    if(correctionRows->count()){
        auto header=raw_section_header(body,tr("定位修正"),"portraitMakeupCorrection","portraitMakeupCorrectionEye",false);
        header.enabled->hide();content->addLayout(header.layout);content->addWidget(corrections);corrections->hide();
        connect(header.expand,&QToolButton::toggled,this,[corrections,button=header.expand](bool open){corrections->setVisible(open);button->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    }else delete corrections;
    const auto classify=[](const QString& key){
        return key.startsWith("makeup.tattoo")?11:key.startsWith("makeup.nails")?10:key.startsWith("makeup.sticker")?9:
            key.startsWith("makeup.freckles")?8:key.startsWith("makeup.mole")?7:key=="makeup.foundation"?6:
            key.contains("lip")?0:key.contains("brow")?1:key.contains("blush")?3:key.contains("catch")?4:2;
    };
    const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};
    const auto parameters=settings["parameters"].toObject(),disabled=settings["disabledParameters"].toObject();
    // Retain existing section ids so saved materials, scripts and callbacks remain valid.
    for(const int index:{5,6,0,1,2,3,4,7,8,9,10,11}){
        QStringList keys;bool adjusted=false,active=false;
        for(const auto& descriptor:editing::portrait_parameters(11)){
            if(index!=5 && classify(descriptor.key)!=index)continue;
            keys<<descriptor.key;
            active|=!disabled[descriptor.key].toBool();
            adjusted|=parameters[descriptor.key].toDouble()!=0 || parameters[descriptor.key+".left"].toDouble()!=0 || parameters[descriptor.key+".right"].toDouble()!=0;
        }
        const bool open=index==current && !property("portraitMakeupCollapsed").toBool();
        auto* heading=new QWidget(parameter_host_);auto* headingLayout=new QVBoxLayout(heading);headingLayout->setContentsMargins(0,0,0,0);
        auto header=raw_section_header(heading,tabs->tabText(index)+(adjusted?tr(" · 已调整"):QString{}),
            QString("portraitMakeupSection%1").arg(index),QString("portraitMakeupEnabled%1").arg(index),open,active);
        header.expand->setMinimumHeight(40);header.enabled->setEnabled(selected_>=0);
        headingLayout->addLayout(header.layout);parameter_layout_->addWidget(heading);
        connect(header.expand,&QToolButton::clicked,this,[this,tabs,index,current](bool checked){
            finish_brush_gesture();finish_gesture();
            if(index!=current)tabs->setCurrentIndex(index);
            else{setProperty("portraitMakeupCollapsed",!checked);build_parameters();}
        });
        connect(header.enabled,&QToolButton::toggled,this,[this,keys](bool on){
            if(selected_<0)return;finish_brush_gesture();finish_gesture();
            auto& settings=people_[std::size_t(selected_)].settings;auto disabled=settings["disabledParameters"].toObject();
            for(const auto& key:keys){if(on)disabled.remove(key);else disabled[key]=true;}
            settings["disabledParameters"]=disabled;remember();build_parameters();
        });
        if(index==current){parameter_layout_->addWidget(body);body->setVisible(open);}
    }
}
void PortraitDialog::refresh_ranges(){
    const auto previous=range_key_->currentData();const QSignalBlocker block(range_key_);range_key_->clear();const int panel=navigation_->currentRow();const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};
    if(panel>=5)for(const auto& p:editing::portrait_parameters(panel)){if(p.key.endsWith("Size") || p.key.endsWith("X") || p.key.endsWith("Y") || p.key.endsWith("Angle") || p.key.endsWith("Curve") || (p.key.endsWith("Gloss") && p.key!="makeup.lipGloss" && p.key!="makeup.blushGloss") || p.key.endsWith("Roughness") || p.key.endsWith("Coverage") || p.key.endsWith("Density") || p.key.endsWith("Feather") || p.key.endsWith("Spread") || p.key.endsWith("Width") || p.key.endsWith("Height"))continue;range_key_->addItem(p.label,p.key+(p.bilateral?(settings["side"].toInt()==0?".left":".right"):""));}
    if(panel==12)range_key_->addItem(tr("发丝边缘（所有美发效果）"),"hair.edge");
    if(panel==8){range_key_->addItem(tr("擦除纹理修复"),"wrinkle.erase.texture");range_key_->addItem(tr("擦除明暗淡化"),"wrinkle.erase.lighting");}
    if(panel==7)range_key_->addItem(tr("保留痣／雀斑"),"repair.preserve");
    if(panel==12)for(const auto& value:settings["hairZones"].toArray()){const auto zone=value.toObject();range_key_->addItem(zone["name"].toString(),zone["key"].toString());}
    if(panel==7 || panel==8 || panel==12){int i=0;for(const auto& v:settings["repairs"].toArray()){const auto s=v.toObject();++i;if((s["kind"].toString()=="wrinkle")!=(panel==8) || (s["kind"].toString()=="hairline")!=(panel==12))continue;if(!s["id"].toString().isEmpty())range_key_->addItem(tr("修复笔画 %1").arg(i),"repair.item."+s["id"].toString());}}
    // Scope correction follows the current page; unrelated effects must not
    // become editable merely because they share the same legacy panel.
    const int task=property("portraitWorkspaceTask").toInt();
    if(panel==5){
        for(int i=range_key_->count()-1;i>=0;--i){
            const bool oil=range_key_->itemData(i).toString()=="skin.oil";
            if((task==7)!=oil)range_key_->removeItem(i);
        }
    }
    const bool geometry=panel>=0 && panel<=4;
    inspector_tabs_->setTabVisible(1,!geometry && range_key_->count()>0);
    inspector_tabs_->setTabVisible(2,geometry);
    inspector_tabs_->setCurrentIndex(geometry?2:1);
    if(auto* header=findChild<QToolButton*>("portraitScopeHeader"))header->setEnabled(selected_>=0 && (geometry || range_key_->count()>0));
    const int found=range_key_->findData(previous);if(found>=0)range_key_->setCurrentIndex(found);
    range_key_->setVisible(range_key_->count()>1);
}
void PortraitDialog::build_style(int panel) {
    const bool eye_entry=panel==9;
    int section=property(QString("portraitSection%1").arg(panel).toUtf8().constData()).toInt();
    if(eye_entry){if(section==0)return;section=section==1?2:4;panel=11;}
    const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};
    if(panel==12 && section==1){auto* note=new QLabel(tr("柔顺沿可靠发向处理，碎发使用补发页的取样修复；减弱油亮只压低过强反光；蓬松度单独调整外轮廓，与头部页的头包脸共用数值，不重复叠加。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);}
    if(panel==12 && section==0){auto* brush=new QPushButton(tr("修正发丝范围"),parameter_host_);brush->setObjectName("portraitHairEdgeBrush");brush->setEnabled(selected_>=0);parameter_layout_->addWidget(brush);connect(brush,&QPushButton::clicked,this,[this]{refresh_ranges();range_key_->setCurrentIndex(range_key_->findData("hair.edge"));range_mode_=1;repair_mode_=0;locate_->setChecked(false);range_visible_->setChecked(true);inspector_tabs_->setCurrentIndex(1);view_->set_tool(-1);status_->setText(tr("直接涂抹补选发丝；范围面板可切换擦除，覆盖预览不会写入照片"));});
        auto* reset=new QPushButton(tr("恢复自动发丝范围"),parameter_host_);reset->setObjectName("portraitHairEdgeReset");reset->setEnabled(selected_>=0);parameter_layout_->addWidget(reset);connect(reset,&QPushButton::clicked,this,[this]{if(selected_<0)return;finish_brush_gesture();auto& s=people_[std::size_t(selected_)].settings;auto masks=s["masks"].toObject();masks.remove("hair.edge");s["masks"]=masks;remember();update_range_overlay();build_parameters();});}
    if(panel==12 && section==0){auto* edge=new QCheckBox(tr("细化发丝边缘"),parameter_host_);edge->setObjectName("portraitHairRefineEdges");edge->setChecked(settings["hairEdgeVersion"].toInt()>=1);edge->setEnabled(selected_>=0 && !repair_analysis_busy_);parameter_layout_->addWidget(edge);connect(edge,&QCheckBox::toggled,this,[this](bool enabled){if(selected_<0)return;auto& s=people_[std::size_t(selected_)].settings;s["hairEdgeVersion"]=enabled?1:0;if(enabled)s["hairColorVersion"]=2;remember();if(enabled && s["hairLighting"].toObject()["edgeVersion"].toInt()<2){analyze_repairs();if(repair_analysis_busy_)analysis_merge_recipe_=recipe();}});}
    if(panel==12 && section==0){auto* note=new QLabel(tr("保留原发束明暗和反光；深黑发只能改变可见色调，不模拟漂白成浅金或白发。分区颜色仅作用于头发范围。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);}
    if(panel==11 && section==6){
        auto* note=new QLabel(tr("均衡面部肤色差异，保留原有明暗、毛孔和纹理。眼口、头发和镜框不参与；可在范围修正中擦除不希望处理的部位。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);
        auto* apply=new QPushButton(tr("自然底妆"),parameter_host_);apply->setObjectName("portraitFoundationNatural");apply->setEnabled(selected_>=0);parameter_layout_->addWidget(apply);connect(apply,&QPushButton::clicked,this,[this]{set_parameter("makeup.foundation",35);});return;
    }
    if(panel==11 && section==5){
        auto* note=new QLabel(tr("选择整套妆容后，可切换唇妆、眉妆、眼妆和腮红分别微调。只应用到当前人物，保留已调整的位置与擦除区。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);
        auto* clear=new QPushButton(tr("无妆容"),parameter_host_);clear->setObjectName("portraitMakeupLookNone");clear->setEnabled(selected_>=0 && !repair_analysis_busy_);parameter_layout_->addWidget(clear);
        connect(clear,&QPushButton::clicked,this,[this]{if(selected_<0)return;finish_gesture();finish_preset(true);auto& settings=people_[std::size_t(selected_)].settings;auto parameters=settings["parameters"].toObject();for(const auto& p:editing::portrait_parameters(11)){if(p.key.endsWith("X") || p.key.endsWith("Y") || p.key.endsWith("Spread") || p.key.endsWith("Size") || p.key.endsWith("Width") || p.key.endsWith("Density") || p.key.endsWith("Feather") || p.key.endsWith("Coverage"))continue;if(p.bilateral){parameters[p.key+".left"]=0;parameters[p.key+".right"]=0;}else parameters[p.key]=0;}settings["parameters"]=parameters;remember();status_->setText(tr("已关闭当前人物妆容，位置、颜色和其他修饰保留；可撤销恢复"));});
        const QStringList names{tr("自然裸妆"),tr("柔和玫瑰"),tr("暖棕轮廓")};
        for(int i=0;i<names.size();++i){auto* card=new QToolButton(parameter_host_);card->setObjectName(QString("portraitMakeupLook%1").arg(i));card->setText(names[i]);card->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);card->setIconSize({110,90});card->setEnabled(selected_>=0 && !repair_analysis_busy_);
            if(selected_>=0 && !linear_preview_.isNull()){
                const auto source=linear_preview_.scaled(384,384,Qt::KeepAspectRatio,Qt::SmoothTransformation);auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);person["settings"]=editing::portrait_merge_preset(settings,editing::portrait_makeup_combination(i),1);
                const auto rendered=editing::display_image(editing::portrait_style_region(source,source.size(),QJsonObject{{"people",QJsonArray{person}}},source.rect(),1,nullptr,{}));const auto face=person["face"].toArray();const auto region=QRectF(face[0].toDouble()*source.width(),face[1].toDouble()*source.height(),face[2].toDouble()*source.width(),face[3].toDouble()*source.height()).toAlignedRect().intersected(source.rect());
                if(!region.isEmpty())card->setIcon(QPixmap::fromImage(rendered.copy(region).scaled(110,90,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
            }
            parameter_layout_->addWidget(card);connect(card,&QToolButton::clicked,this,[this,i]{finish_gesture();preview_preset(editing::portrait_makeup_combination(i),1);finish_preset(true);status_->setText(tr("整套妆容已应用；各部位可独立调整，撤销一次恢复应用前状态"));});
        }
        return;
    }
    if(panel==11 && (section==2 || section==4)){const auto iris=settings["irisMaterials"].toObject();const bool left=iris["geometry.left"].toObject()["valid"].toBool(),right=iris["geometry.right"].toObject()["valid"].toBool();auto* note=new QLabel(iris.isEmpty()?tr("正在定位虹膜；定位完成后显示材质"):left && right?tr("已定位两侧虹膜，原反光和瞳孔保持"):tr("部分虹膜定位不可靠，该侧不自动贴材质；可修正关键点后重新检测"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);}
    auto button=[&](QString text,QString name,auto fn){auto* b=new QPushButton(text,parameter_host_);b->setObjectName(name);b->setEnabled(selected_>=0);parameter_layout_->addWidget(b);connect(b,&QPushButton::clicked,this,fn);};
    if(panel==11 && (section==7 || section==8)){
        const QString decorationKey=section==7?"makeup.mole":"makeup.freckles";
        auto* note=new QLabel(section==7?tr("新增装饰痣，不修改原有痣或修复保护区；拖动位置，大小和边缘可独立调整。"):tr("新增雀斑妆容，不修改原有雀斑；密度增加保留已有位置，重新分布后可一步撤销。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);
        if(section==7)button(tr("在画布上放置／拖动"),"portraitPlaceMole",[this]{if(selected_<0)return;if(people_[std::size_t(selected_)].settings["parameters"].toObject()["makeup.mole"].toDouble()==0)set_parameter("makeup.mole",70);placement_key_="makeup.mole";repair_mode_=0;range_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();status_->setText(tr("拖动调整装饰痣；空格临时抓手，浓度归零可移除"));});
        if(section==8){
            button(tr("应用自然雀斑"),"portraitFrecklesApply",[this]{if(selected_<0)return;finish_gesture();gesture_=true;auto& s=people_[std::size_t(selected_)].settings;auto enabled=s["enabled"].toObject();enabled["makeup"]=true;s["enabled"]=enabled;auto disabled=s["disabledParameters"].toObject();disabled.remove("makeup.freckles");s["disabledParameters"]=disabled;set_parameter("makeup.freckles",65);set_parameter("makeup.frecklesSize",30);set_parameter("makeup.frecklesDensity",35);finish_gesture();build_parameters();});
            button(tr("换一组分布"),"portraitFrecklesShuffle",[this]{if(selected_<0)return;auto& s=people_[std::size_t(selected_)].settings;s["freckleSeed"]=double((unsigned(std::clamp(s["freckleSeed"].toDouble(1729),0.,2147483647.))+104729u)&0x7fffffffu);remember();});
        }
        auto* palette=new QWidget(parameter_host_);auto* row=new QHBoxLayout(palette);row->setContentsMargins(0,0,0,0);
        for(const auto& hex:QStringList{"#483126","#694332","#342d29"}){auto* chip=new QPushButton(palette);chip->setObjectName((section==7?"portraitMoleColor_":"portraitFreckleColor_")+hex.mid(1));chip->setFixedSize(30,30);chip->setToolTip(hex);chip->setStyleSheet("background:"+hex+";border:1px solid #888;border-radius:4px");row->addWidget(chip);connect(chip,&QPushButton::clicked,this,[this,hex,decorationKey]{if(selected_<0)return;QImage sample(1,1,QImage::Format_RGBA8888);sample.setColorSpace(QColorSpace::SRgb);sample.fill(QColor(hex));const auto linear=editing::to_working(sample);const auto* c=reinterpret_cast<const float*>(linear.constBits());auto& s=people_[std::size_t(selected_)].settings;auto colors=s["colors"].toObject();colors[decorationKey]=QJsonArray{c[0],c[1],c[2]};s["colors"]=colors;remember();});}row->addStretch();parameter_layout_->addWidget(palette);
    }
    if(panel==11 && section==11){
        const bool ready=selected_>=0 && !people_[std::size_t(selected_)].unmatched && !people_[std::size_t(selected_)].match_ambiguous && (!people_[std::size_t(selected_)].body_ambiguous || settings["partialSkin"].toBool());
        auto* note=new QLabel(ready?tr("选择款式后点按裸露皮肤放置；拖动整体移动，拖动四角贴合。衣物和背景由身体皮肤范围排除；曲面贴合用于手臂等弧面。"):tr("请先确认人物与身体关联，再放置纹身贴。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);if(!ready)return;
        if(settings["automaticMasks"].toObject()["bodySkin"].toString().isEmpty()){parameter_layout_->addWidget(new QLabel(tr("缺少身体皮肤范围，请重新识别人像；不会在未知范围直接覆盖纹身。"),parameter_host_));return;}
        auto* cards=new QWidget(parameter_host_);auto* row=new QHBoxLayout(cards);row->setContentsMargins(0,0,0,0);
        for(const auto& item:std::vector<std::pair<QString,QString>>{{"leaf",tr("枝叶")},{"wave",tr("波浪")},{"compass",tr("罗盘")}}){auto* card=new QToolButton(cards);card->setObjectName("portraitTattoo_"+item.first);card->setText(item.second);card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);QPixmap icon(64,64);icon.fill(QColor("#d6bba1"));QPainter painter(&icon);painter.setRenderHint(QPainter::Antialiasing);painter.translate(32,32);painter.scale(28,28);painter.setPen(Qt::NoPen);painter.setBrush(QColor("#292f36"));painter.drawPath(editing::portrait_tattoo_path(item.first));painter.end();card->setIcon(QIcon(icon));card->setIconSize({64,64});row->addWidget(card);connect(card,&QToolButton::clicked,this,[this,key=item.first]{if(selected_<0)return;finish_brush_gesture();finish_gesture();gesture_=true;auto& s=people_[std::size_t(selected_)].settings;s["tattooMaterial"]=key;auto enabled=s["enabled"].toObject();enabled["makeup"]=true;s["enabled"]=enabled;auto disabled=s["disabledParameters"].toObject();disabled.remove("makeup.tattoo");s["disabledParameters"]=disabled;set_parameter("makeup.tattoo",80);finish_gesture();placement_key_="makeup.tattoo";range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);build_parameters();update_overlay();});}row->addStretch();parameter_layout_->addWidget(cards);
        button(tr("移动／四角贴合"),"portraitTattooPosition",[this]{finish_brush_gesture();placement_key_="makeup.tattoo";range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();});
        button(tr("擦除遮挡部分"),"portraitTattooErase",[this]{finish_brush_gesture();placement_key_.clear();refresh_ranges();range_key_->setCurrentIndex(range_key_->findData("makeup.tattoo"));range_mode_=2;range_visible_->setChecked(true);view_->set_tool(-1);inspector_tabs_->setCurrentIndex(1);update_overlay();});
        auto* palette=new QWidget(parameter_host_);auto* colors=new QHBoxLayout(palette);colors->setContentsMargins(0,0,0,0);for(const auto& hex:QStringList{"#27303b","#285044","#784132"}){auto* chip=new QPushButton(palette);chip->setObjectName("portraitTattooColor_"+hex.mid(1));chip->setToolTip(hex);chip->setFixedSize(32,32);chip->setStyleSheet("background:"+hex+";border:1px solid #888;border-radius:4px");colors->addWidget(chip);connect(chip,&QPushButton::clicked,this,[this,hex]{if(selected_<0)return;QImage image(1,1,QImage::Format_RGBA8888);image.setColorSpace(QColorSpace::SRgb);image.fill(QColor(hex));const auto linear=editing::to_working(image);const auto* p=reinterpret_cast<const float*>(linear.constBits());auto& s=people_[std::size_t(selected_)].settings;auto colors=s["colors"].toObject();colors["makeup.tattoo"]=QJsonArray{p[0],p[1],p[2]};s["colors"]=colors;remember();});}colors->addStretch();parameter_layout_->addWidget(palette);
    }
    if(panel==11 && section==10){
        if(selected_>=0 && (people_[std::size_t(selected_)].unmatched || people_[std::size_t(selected_)].match_ambiguous)){parameter_layout_->addWidget(new QLabel(tr("请先确认当前照片的人物关联，再编辑甲面"),parameter_host_));return;}
        auto* note=new QLabel(tr("自动识别可见甲面后逐个上色；漏检可直接圈选补充，边缘不准可重绘或擦除。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);
        button(tr("识别甲面"),"portraitNailDetect",[this]{analyze_nails();});
        button(tr("圈选一个甲面"),"portraitNailOutline",[this]{finish_brush_gesture();setProperty("portraitNailReplace",false);placement_key_="nail:outline";range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();status_->setText(tr("沿甲面边缘连续拖动一圈，松开完成；空格临时抓手"));});
        auto* list=new QComboBox(parameter_host_);list->setObjectName("portraitNailSelection");const auto nails=settings["nails"].toArray();for(int i=0;i<nails.size();++i)list->addItem(tr("指甲 %1").arg(i+1),i);list->setCurrentIndex(std::clamp(property("portraitNailIndex").toInt(),0,std::max(0,int(nails.size())-1)));parameter_layout_->addWidget(list);connect(list,&QComboBox::activated,this,[this](int i){setProperty("portraitNailIndex",i);});
        button(tr("重绘当前甲面"),"portraitNailRedraw",[this]{if(selected_<0 || people_[std::size_t(selected_)].settings.value("nails").toArray().isEmpty())return;finish_brush_gesture();setProperty("portraitNailReplace",true);placement_key_="nail:outline";range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();status_->setText(tr("重新圈选当前甲面，原颜色保留；松开完成，取消保留原范围"));});
        button(tr("删除当前甲面"),"portraitNailRemove",[this]{if(selected_<0)return;auto& s=people_[std::size_t(selected_)].settings;auto nails=s["nails"].toArray();const int i=property("portraitNailIndex").toInt();if(i<0 || i>=nails.size())return;nails.removeAt(i);s["nails"]=nails;setProperty("portraitNailIndex",std::max(0,i-1));remember();build_parameters();update_overlay();});
        auto* palette=new QWidget(parameter_host_);auto* row=new QHBoxLayout(palette);row->setContentsMargins(0,0,0,0);
        for(const auto& hex:QStringList{"#a92239","#e5a3aa","#c59b7d","#416179","#eee4d5"}){auto* chip=new QPushButton(palette);chip->setObjectName("portraitNailColor_"+hex.mid(1));chip->setFixedSize(32,32);chip->setToolTip(hex);chip->setStyleSheet("background:"+hex+";border:1px solid #888;border-radius:5px");row->addWidget(chip);chip->setEnabled(!nails.isEmpty());connect(chip,&QPushButton::clicked,this,[this,hex]{if(selected_<0)return;auto& s=people_[std::size_t(selected_)].settings;auto nails=s["nails"].toArray();const int i=property("portraitNailIndex").toInt();if(i<0 || i>=nails.size())return;QImage image(1,1,QImage::Format_RGBA8888);image.setColorSpace(QColorSpace::SRgb);image.fill(QColor(hex));const auto linear=editing::to_working(image);const auto* p=reinterpret_cast<const float*>(linear.constBits());auto nail=nails[i].toObject();nail["color"]=QJsonArray{p[0],p[1],p[2]};nails[i]=nail;s["nails"]=nails;remember();});}row->addStretch();parameter_layout_->addWidget(palette);
        button(tr("擦除越界部分"),"portraitNailErase",[this]{finish_brush_gesture();placement_key_.clear();refresh_ranges();range_key_->setCurrentIndex(range_key_->findData("makeup.nails"));range_mode_=2;range_visible_->setChecked(true);view_->set_tool(-1);inspector_tabs_->setCurrentIndex(1);update_overlay();});
    }
    if(panel==11 && section==9){
        auto* cards=new QWidget(parameter_host_);auto* row=new QHBoxLayout(cards);row->setContentsMargins(0,0,0,0);
        for(const auto& entry:std::vector<std::pair<QString,QString>>{{"heart",tr("爱心")},{"star",tr("星星")},{"flower",tr("花朵")}}){auto* card=new QToolButton(cards);card->setObjectName("portraitSticker_"+entry.first);card->setText(entry.second);card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);QPixmap icon(56,48);icon.fill(Qt::transparent);QPainter painter(&icon);painter.setRenderHint(QPainter::Antialiasing);painter.translate(28,24);painter.scale(19,19);painter.setPen(Qt::NoPen);painter.setBrush(QColor("#e96487"));painter.drawPath(editing::portrait_sticker_path(entry.first));painter.end();card->setIcon(QIcon(icon));card->setIconSize({56,48});row->addWidget(card);connect(card,&QToolButton::clicked,this,[this,key=entry.first]{if(selected_<0)return;finish_gesture();gesture_=true;auto& s=people_[std::size_t(selected_)].settings;s["stickerMaterial"]=key;auto enabled=s["enabled"].toObject();enabled["makeup"]=true;s["enabled"]=enabled;auto disabled=s["disabledParameters"].toObject();disabled.remove("makeup.sticker");s["disabledParameters"]=disabled;set_parameter("makeup.sticker",85);if(!s["parameters"].toObject().contains("makeup.stickerSize"))set_parameter("makeup.stickerSize",35);finish_gesture();build_parameters();});}parameter_layout_->addWidget(cards);
        auto* side=new QComboBox(parameter_host_);side->setObjectName("portraitStickerSide");side->addItem(tr("人物左侧"),"left");side->addItem(tr("人物右侧"),"right");side->setCurrentIndex(settings["stickerSide"].toString("left")=="right"?1:0);parameter_layout_->addWidget(side);connect(side,&QComboBox::activated,this,[this,side](int){if(selected_<0)return;people_[std::size_t(selected_)].settings["stickerSide"]=side->currentData().toString();remember();update_overlay();});
        button(tr("在画布上放置／拖动"),"portraitPlaceSticker",[this]{placement_key_="makeup.sticker";repair_mode_=0;range_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();});
        button(tr("擦除遮挡部分"),"portraitEraseSticker",[this]{placement_key_.clear();refresh_ranges();range_key_->setCurrentIndex(range_key_->findData("makeup.sticker"));range_mode_=2;range_visible_->setChecked(true);view_->set_tool(-1);inspector_tabs_->setCurrentIndex(1);update_overlay();});
        auto* palette=new QWidget(parameter_host_);auto* colors=new QHBoxLayout(palette);colors->setContentsMargins(0,0,0,0);
        for(const auto& hex:QStringList{"#e96487","#e2b34b","#86b8d4","#a47bbd"}){auto* chip=new QPushButton(palette);chip->setObjectName("portraitStickerColor_"+hex.mid(1));chip->setFixedSize(30,30);chip->setStyleSheet("background:"+hex+";border:1px solid #888;border-radius:4px");colors->addWidget(chip);connect(chip,&QPushButton::clicked,this,[this,hex]{if(selected_<0)return;QImage sample(1,1,QImage::Format_RGBA8888);sample.setColorSpace(QColorSpace::SRgb);sample.fill(QColor(hex));const auto linear=editing::to_working(sample);const auto* c=reinterpret_cast<const float*>(linear.constBits());auto& s=people_[std::size_t(selected_)].settings;auto values=s["colors"].toObject();values["makeup.sticker"]=QJsonArray{c[0],c[1],c[2]};s["colors"]=values;remember();});}colors->addStretch();parameter_layout_->addWidget(palette);
    }
    if(panel==13 && section==0){
        button(tr("自动立体"),"portraitContourAuto",[this]{if(selected_<0)return;finish_brush_gesture();auto& person=people_[std::size_t(selected_)];const auto lighting=editing::portrait_contour_lighting(linear_preview_,editing::portrait_person_json(person));if(lighting.isEmpty()){status_->setText(tr("皮肤范围或光照不足，未应用自动立体；请先检查人物定位"));return;}auto& s=person.settings;s["contourLightVersion"]=2;s["contourAutoLighting"]=true;s["contourLighting"]=lighting;auto enabled=s["enabled"].toObject();enabled["contour"]=true;s["enabled"]=enabled;auto disabled=s["disabledParameters"].toObject();disabled.remove("contour.highlight");disabled.remove("contour.shadow");s["disabledParameters"]=disabled;auto parameters=s["parameters"].toObject();parameters["contour.highlight"]=35;parameters["contour.shadow"]=25;s["parameters"]=parameters;remember();build_parameters();status_->setText(tr("已按原光线组合高光与阴影，可分别调节；保留丰盈轮廓，不重复叠加丰盈明暗"));});
        if(settings["contourAutoLighting"].toBool()){
            auto* note=new QLabel(tr("高光、阴影遵循原光线，可分别调节。丰盈轮廓保留，其原有明暗由此组合替代。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);
            button(tr("解除光线约束"),"portraitContourAutoRelease",[this]{if(selected_<0)return;people_[std::size_t(selected_)].settings["contourAutoLighting"]=false;remember();build_parameters();});
        }
    }
    if(panel==11 && (section==2 || section==4))button(tr("重新定位虹膜"),"portraitRefreshIris",[this]{analyze_repairs();});
    auto material=[&](QString label,QString key,QStringList labels,QStringList values){
        parameter_layout_->addWidget(new QLabel(label,parameter_host_));auto* c=new QComboBox(parameter_host_);c->setObjectName("portraitMaterial_"+key);for(int i=0;i<labels.size();++i)c->addItem(labels[i],values[i]);c->setCurrentIndex(std::max(0,c->findData((key=="hairMaterial" || key=="bodyMaterial" || key=="lipMaterial"?settings.value(key):editing::portrait_side_value(settings,key,settings.value("side").toInt()==0)).toString(values.front()))));c->hide();
        auto* cards=new QWidget(parameter_host_);auto* row=new QHBoxLayout(cards);row->setContentsMargins(0,0,0,0);auto* group=new QButtonGroup(cards);
        for(int i=0;i<labels.size();++i){auto* b=portrait_style_card(cards,labels[i]);b->setObjectName("portraitMaterialCard_"+key+"_"+values[i]);b->setChecked(i==c->currentIndex());group->addButton(b);row->addWidget(b);
            QPixmap pixmap(64,44);pixmap.fill(QColor(37,42,46));QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(205,213,222),2));p.setBrush(QColor(185,196,208));
            if(key=="blushMaterial"){p.setPen(Qt::NoPen);p.setBrush(QColor(207,120,137,160));if(values[i]=="horizontal")p.drawRoundedRect(QRectF(10,18,44,13),6,6);else if(values[i]=="diagonal"){p.translate(32,22);p.rotate(-25);p.drawEllipse(QRectF(-24,-7,48,14));}else{QPainterPath shape;shape.moveTo(10,15);shape.quadTo(32,25,54,15);shape.quadTo(42,36,20,30);shape.closeSubpath();p.drawPath(shape);}}else if(key=="creaseMaterial"){p.setBrush(Qt::NoBrush);QPainterPath lid;lid.moveTo(9,30);lid.cubicTo(22,15,40,15,55,30);p.drawPath(lid);QPainterPath crease;crease.moveTo(10,values[i]=="fan"?28:22);crease.cubicTo(23,7,40,7,54,22);p.drawPath(crease);}else if(key=="lashMaterial"){p.setBrush(Qt::NoBrush);for(int n=0;n<(values[i]=="volume"?11:7);++n){const double t=(n+.5)/(values[i]=="volume"?11:7);const QPointF root(10+t*44,29-7*std::sin(t*3.14159265));const double length=values[i]=="long"?18:12;QPainterPath hair;hair.moveTo(root);hair.cubicTo(root+QPointF(0,-length*.5),root+QPointF((t-.5)*12,-length),root+QPointF((t-.5)*17,-length));p.drawPath(hair);}}else if(key=="linerMaterial"){p.setBrush(Qt::NoBrush);QPainterPath lid;lid.moveTo(9,29);lid.cubicTo(22,12,39,12,51,28);p.setPen(QPen(QColor(205,213,222),values[i]=="round"?4:2));p.drawPath(lid);if(values[i]=="wing")p.drawLine(QPointF(45,23),QPointF(59,14));}else if(values[i]=="natural" || values[i]=="straight" || values[i]=="arched"){p.setBrush(Qt::NoBrush);const double arch=values[i]=="straight"?18:values[i]=="arched"?5:11;QPainterPath brow;brow.moveTo(9,27);brow.cubicTo(22,arch,40,arch,56,26);p.drawPath(brow);for(int n=0;n<10;++n){const double t=(n+.5)/10;const auto pt=brow.pointAtPercent(t);p.drawLine(pt+QPointF(-2,3),pt+QPointF(2,-2));}}else if(values[i]=="circle")p.drawEllipse(QRectF(22,12,20,20));else if(values[i]=="ring"){p.setBrush(Qt::NoBrush);p.drawEllipse(QRectF(22,12,20,20));}else if(values[i]=="window"){p.drawRect(20,10,24,24);p.setPen(QColor(37,42,46));p.drawLine(32,10,32,34);p.drawLine(20,22,44,22);}else if(values[i]=="clip")p.drawRoundedRect(QRectF(9,19,46,6),3,3);else if(values[i]=="flower"){for(int n=0;n<5;++n){p.save();p.translate(32,22);p.rotate(n*72);p.drawEllipse(QRectF(-5,-18,10,15));p.restore();}}else if(values[i]=="bow"){p.drawPolygon(QPolygonF{{32,22},{12,10},{12,34}});p.drawPolygon(QPolygonF{{32,22},{52,10},{52,34}});}else{p.setBrush(Qt::NoBrush);p.drawArc(QRectF(10,5,44,30),values[i]=="waist"?60*16:190*16,120*16);}p.end();
            if(key=="lipMaterial" && selected_>=0 && !linear_preview_.isNull()){
                auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);auto local=person["settings"].toObject();local["parameters"]=QJsonObject{{"makeup.lip",80},{"makeup.lipCoverage",values[i]=="opaque"?75:values[i]=="bitten"?20:30},{"makeup.lipFeather",values[i]=="bitten"?25:10}};local["lipMaterialVersion"]=2;local["mouthTopologyVersion"]=2;local["lipMaterial"]=values[i];local["strength"]=100;local["enabled"]=QJsonObject{};local["disabledParameters"]=QJsonObject{};person["settings"]=local;
                QPolygonF mouth;for(int n=48;n<66;++n){const auto q=person["landmarks"].toArray()[n].toArray();mouth<<QPointF(q[0].toDouble()*linear_preview_.width(),q[1].toDouble()*linear_preview_.height());}auto box=mouth.boundingRect();const double padding=box.width()*.2;const auto region=box.adjusted(-padding,-padding,padding,padding).toAlignedRect().intersected(linear_preview_.rect());
                if(!region.isEmpty()){const auto preview=editing::portrait_style_region(linear_preview_.copy(region),linear_preview_.size(),QJsonObject{{"people",QJsonArray{person}}},region,1,nullptr,{});const auto thumbnail=editing::display_image(preview).scaled(64,44,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);pixmap=QPixmap::fromImage(thumbnail.copy((thumbnail.width()-64)/2,(thumbnail.height()-44)/2,64,44));}
            }
            if(key=="hairMaterial" && selected_>=0){
                auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);person["face"]=QJsonArray{.1,.1,.8,.8};QJsonObject previewSettings{{"parameters",QJsonObject{{"hair.accessory",100},{"hair.accessoryX",-35},{"hair.accessoryY",65},{"hair.accessorySize",85},{"hair.accessoryRoughness",settings["parameters"].toObject()["hair.accessoryRoughness"].toDouble()}}},{"hairAccessoryVersion",2},{"hairMaterial",values[i]},{"colors",settings["colors"]}};person["settings"]=previewSettings;
                QImage plate(128,88,QImage::Format_RGBA32FPx4_Premultiplied);plate.setColorSpace(editing::working_space());for(int y=0;y<plate.height();++y){auto* pixels=reinterpret_cast<float*>(plate.scanLine(y));for(int x=0;x<plate.width();++x){pixels[x*4]=.018F;pixels[x*4+1]=.023F;pixels[x*4+2]=.028F;pixels[x*4+3]=1;}}
                const auto rendered=editing::portrait_style_region(plate,plate.size(),QJsonObject{{"people",QJsonArray{person}}},plate.rect(),1,nullptr,{});pixmap=QPixmap::fromImage(editing::display_image(rendered).scaled(64,44,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
            }
            b->setIcon(QIcon(pixmap));b->setIconSize({64,44});
            connect(b,&QToolButton::clicked,this,[this,c,key,i]{if(selected_<0)return;c->setCurrentIndex(i);if(key=="hairMaterial" || key=="bodyMaterial" || key=="lipMaterial")people_[std::size_t(selected_)].settings[key]=c->currentData().toString();else editing::portrait_set_side_value(people_[std::size_t(selected_)].settings,key,c->currentData().toString());if(key=="lipMaterial"){auto& settings=people_[std::size_t(selected_)].settings;settings["lipMaterialVersion"]=2;settings["mouthTopologyVersion"]=2;auto parameters=settings["parameters"].toObject();parameters["makeup.lipCoverage"]=c->currentData().toString()=="opaque"?75:c->currentData().toString()=="bitten"?20:30;parameters["makeup.lipFeather"]=c->currentData().toString()=="bitten"?25:10;settings["parameters"]=parameters;}if(key=="hairMaterial"){auto& settings=people_[std::size_t(selected_)].settings;settings["hairAccessoryVersion"]=2;if(!settings.contains("hairAccessoryOcclusion"))settings["hairAccessoryOcclusion"]="skin";}if(key=="blushMaterial")editing::portrait_set_side_value(people_[std::size_t(selected_)].settings,"blushMaterialVersion",2);if(key=="lashMaterial")editing::portrait_set_side_value(people_[std::size_t(selected_)].settings,"eyeLashVersion",2);if(key=="linerMaterial")editing::portrait_set_side_value(people_[std::size_t(selected_)].settings,"eyeLinerVersion",2);if(key=="browMaterial")editing::portrait_set_side_value(people_[std::size_t(selected_)].settings,"browMaterialVersion",2);if(key=="catchMaterial")editing::portrait_set_side_value(people_[std::size_t(selected_)].settings,"irisMaterialVersion",2);const QString parameter=key=="blushMaterial"?"makeup.blush":key=="lipMaterial"?"makeup.lip":key=="creaseMaterial"?"makeup.crease":key=="lashMaterial"?"makeup.lash":key=="linerMaterial"?"makeup.liner":key=="browMaterial"?"makeup.browShape":key=="catchMaterial"?"makeup.catchlight":key=="hairMaterial"?"hair.accessory":"contour.lines";const auto params=people_[std::size_t(selected_)].settings["parameters"].toObject();const bool single=key!="hairMaterial" && key!="bodyMaterial" && key!="lipMaterial" && !people_[std::size_t(selected_)].settings.value("linked").toBool(true);const QString suffix=people_[std::size_t(selected_)].settings.value("side").toInt()==0?".left":".right";if(single?params.value(parameter+suffix).toDouble()==0:(params.value(parameter).toDouble()==0 && params.value(parameter+".left").toDouble()==0 && params.value(parameter+".right").toDouble()==0))set_parameter(parameter,key=="blushMaterial"?35:70);else remember();if(key=="lipMaterial" || key=="hairMaterial")build_parameters();});
        }parameter_layout_->addWidget(cards);
        connect(c,&QComboBox::activated,this,[this,c,key](int){if(selected_<0)return;if(key=="hairMaterial" || key=="bodyMaterial" || key=="lipMaterial")people_[std::size_t(selected_)].settings[key]=c->currentData().toString();else editing::portrait_set_side_value(people_[std::size_t(selected_)].settings,key,c->currentData().toString());remember();});
    };
    if((panel==11 && (section==4 || section==3)) || (panel==12 && section==3) || (panel==13 && section==2)){
        const QString key=panel==11?(section==3?"makeup.blush":"makeup.catch"):panel==12?"hair.accessory":"contour.lines";
        button(key=="hair.accessory"?tr("在画布上调整发饰"):tr("在画布上拖动位置"),"portraitPlaceStyle",[this,key]{placement_key_=key;repair_mode_=0;range_mode_=0;locate_->setChecked(false);view_->set_tool(-1);update_overlay();status_->setText(key=="hair.accessory"?tr("拖动框内移动；右下方块缩放，顶部圆点旋转；空格临时抓手"):tr("拖动画布调整此素材位置；空格临时抓手"));});
    }
    if(panel==11 && section==1){button(tr("调整眉部定位"),"portraitBrowAnchors",[this]{placement_key_.clear();repair_mode_=0;range_mode_=0;view_->set_tool(-1);locate_->setChecked(true);update_overlay();status_->setText(tr("拖动眉毛关键点修正眉头、眉峰或眉尾；确认位置后可重新提取眉毛颜色范围"));});button(tr("重新提取眉毛范围"),"portraitRefreshBrow",[this]{analyze_repairs();});}
    if(panel==11 && section==2 && !eye_entry){
        auto* combinations=new QWidget(parameter_host_);auto* row=new QHBoxLayout(combinations);row->setContentsMargins(0,0,0,0);
        for(int i=0;i<2;++i){auto* b=new QPushButton(i==0?tr("自然眼妆"):tr("深邃眼妆"),combinations);b->setObjectName(i==0?"portraitEyeCombinationNatural":"portraitEyeCombinationDefined");b->setEnabled(selected_>=0);b->setToolTip(tr("将眼影、眼线和睫毛强度作为一步应用到当前侧；保留已选颜色、款式及其他妆容，可分别继续调整"));row->addWidget(b);
            connect(b,&QPushButton::clicked,this,[this,i]{if(selected_<0)return;finish_gesture();gesture_=true;set_parameter("makeup.shadow",i==0?25:50);set_parameter("makeup.shadowAccent",i==0?10:40);set_parameter("makeup.liner",i==0?30:60);set_parameter("makeup.linerLower",i==0?0:20);set_parameter("makeup.lash",i==0?30:55);finish_gesture();build_parameters();status_->setText(tr("眼妆组合已应用，可分别微调；撤销一次恢复组合前状态"));});
        }parameter_layout_->addWidget(combinations);
    }
    if(panel==11 && section==2 && !eye_entry){material(tr("常规双眼皮"),"creaseMaterial",{tr("平扇"),tr("平行")},{"fan","parallel"});button(tr("修正眼睑定位"),"portraitEyelidAnchors",[this]{placement_key_.clear();repair_mode_=0;range_mode_=0;view_->set_tool(-1);locate_->setChecked(true);update_overlay();status_->setText(tr("拖动上眼睑及眉部关键点修正贴合；此功能只添加折痕明暗，不改变眼睛开合"));});auto* note=new QLabel(tr("折痕按眼睑与眉下空间贴合；眼睛过小或空间不足时不绘制。可先修正眼睑定位。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);}
    if(panel==11 && section==2 && !eye_entry)material(tr("睫毛款式"),"lashMaterial",{tr("自然"),tr("纤长"),tr("浓密")},{"light","long","volume"});
    if(panel==11 && section==2 && !eye_entry)material(tr("眼线款式"),"linerMaterial",{tr("自然细线"),tr("圆眼"),tr("上扬眼尾")},{"soft","round","wing"});
    if(panel==11 && section==0)button(tr("重新分析唇部光照"),"portraitRefreshLipLighting",[this]{analyze_repairs();});
    if(panel==11 && section==0)material(tr("唇妆款式"),"lipMaterial",{tr("轻透"),tr("厚涂"),tr("咬唇")},{"sheer","opaque","bitten"});
    if(panel==11 && section==3)material(tr("腮红分布"),"blushMaterial",{tr("眼下"),tr("横向"),tr("斜向")},{"underEye","horizontal","diagonal"});
    if(panel==11 && section==1)material(tr("眉形"),"browMaterial",{tr("自然眉"),tr("平眉"),tr("挑眉")},{"natural","straight","arched"});
    if(panel==11 && section==4)material(tr("眼神光素材"),"catchMaterial",{tr("圆形柔光"),tr("窗形"),tr("环形")},{"circle","window","ring"});
    if(panel==12 && section==3){auto* order=new QComboBox(parameter_host_);order->setObjectName("portraitAccessoryOcclusion");order->addItem(tr("手工处理遮挡"),"manual");order->addItem(tr("皮肤与镜框在前"),"skin");order->addItem(tr("头发、皮肤与镜框在前"),"behindHair");order->setCurrentIndex(std::max(0,order->findData(settings["hairAccessoryOcclusion"].toString("manual"))));order->setEnabled(selected_>=0);parameter_layout_->addWidget(order);connect(order,&QComboBox::activated,this,[this,order](int){if(selected_<0)return;people_[std::size_t(selected_)].settings["hairAccessoryOcclusion"]=order->currentData().toString();remember();update_range_overlay();});auto* note=new QLabel(tr("遮挡按当前人物的识别范围处理；识别不准时可切换手工模式，再擦除遮挡部分。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);}
    if(panel==12 && section==3){auto* follow=new QCheckBox(tr("擦除范围随发饰移动"),parameter_host_);follow->setObjectName("portraitAccessoryMaskFollows");follow->setChecked(settings["hairAccessoryMaskFollows"].toBool(!settings["masks"].toObject().contains("hair.accessory")));follow->setEnabled(selected_>=0);parameter_layout_->addWidget(follow);connect(follow,&QCheckBox::toggled,this,[this](bool enabled){if(selected_<0)return;finish_brush_gesture();auto& person=people_[std::size_t(selected_)];auto& settings=person.settings;auto masks=settings["masks"].toObject();if(masks.contains("hair.accessory"))masks["hair.accessory"]=editing::portrait_mask_png(editing::portrait_accessory_coverage(editing::portrait_person_json(person),preview_.size().scaled(768,768,Qt::KeepAspectRatio)));settings["masks"]=masks;QJsonObject anchor;for(const auto* name:{"hair.accessoryX","hair.accessoryY","hair.accessorySize","hair.accessoryAngle"})anchor[name]=settings["parameters"].toObject()[name].toDouble();settings["hairAccessoryMaskAnchor"]=anchor;settings["hairAccessoryMaskFollows"]=enabled;remember();update_range_overlay();});}
    if(panel==12 && section==3)material(tr("发饰素材"),"hairMaterial",{tr("发夹"),tr("花朵"),tr("蝴蝶结")},{"clip","flower","bow"});
    if(panel==13 && section==2)material(tr("身体线条素材"),"bodyMaterial",{tr("腰腹阴影"),tr("锁骨阴影")},{"waist","collarbone"});
    if(panel==13 && section==2){auto* note=new QLabel(tr("身体线条仅作用于已识别的裸露皮肤；衣物、遮挡或缺少躯干定位时不会硬贴。可在范围页检查素材位置和减选遮挡。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);}
    if((panel==11 && section!=4 && section!=7 && section!=8 && section!=9) || (panel==12 && (section==0 || section==3)) || (panel==13 && section==1)){auto* colors=new QComboBox(parameter_host_);colors->setObjectName("portraitStyleColorTarget");
        if(panel==11){for(const auto& p:editing::portrait_parameters(panel))if((section==0 && p.key=="makeup.lip") || (section==1 && p.key=="makeup.brow") || (section==3 && p.key=="makeup.blush") || (section==2 && (p.key=="makeup.iris" || (!eye_entry && (p.key.startsWith("makeup.shadow") || p.key=="makeup.liner" || p.key=="makeup.lash")))))colors->addItem(p.label,p.key);}
        else if(panel==12){if(section==0){colors->addItem(tr("整体染发"),"hair.color");colors->addItem(tr("发根颜色"),"hair.roots");}else colors->addItem(tr("发饰颜色"),"hair.accessory");for(const auto& v:settings["hairZones"].toArray()){const auto z=v.toObject();colors->addItem(z["name"].toString(),z["key"].toString());}}
        else colors->addItem(tr("手绘修容"),"contour.paint");colors->hide();
        auto* color_tabs=new QTabBar(parameter_host_);color_tabs->setObjectName("portraitColorTabs");color_tabs->setExpanding(false);for(int i=0;i<colors->count();++i)color_tabs->addTab(colors->itemText(i));parameter_layout_->addWidget(color_tabs);color_tabs->setVisible(color_tabs->count()>1);connect(color_tabs,&QTabBar::currentChanged,colors,&QComboBox::setCurrentIndex);
        auto* palette_widget=new QWidget(parameter_host_);auto* palette_row=new QHBoxLayout(palette_widget);palette_row->setContentsMargins(0,0,0,0);auto* palette_group=new QButtonGroup(palette_widget);
        const QStringList palette=panel==12?QStringList{"#302520","#604031","#98684b","#c29765","#73403e","#5a526f"}:panel==13?QStringList{"#e4c7a0","#c29571","#997052","#765443","#62483c","#4e3931"}:QStringList{"#be4c5b","#cf7474","#a74740","#854039","#b17865","#755647"};
        for(const auto& color:palette){auto* swatch=portrait_color_swatch(palette_widget,QColor(color));swatch->setObjectName("portraitSwatch_"+color.mid(1));palette_group->addButton(swatch);palette_row->addWidget(swatch);swatch->setProperty("portraitColor",color);connect(swatch,&QPushButton::clicked,this,[this,colors,swatch]{if(selected_<0)return;const auto color=swatch->property("portraitColor").toString();QImage chip(1,1,QImage::Format_RGBA8888);chip.setColorSpace(QColorSpace::SRgb);chip.setPixelColor(0,0,QColor(color));auto working=editing::to_working(chip);const auto* rgb=reinterpret_cast<const float*>(working.constBits());auto& settings=people_[std::size_t(selected_)].settings;const auto key=colors->currentData().toString();editing::portrait_set_style_color(settings,key,QJsonArray{rgb[0],rgb[1],rgb[2]});const auto params=settings["parameters"].toObject();const bool single=key.startsWith("makeup.") && key!="makeup.lip" && !settings.value("linked").toBool(true);const QString suffix=settings.value("side").toInt()==0?".left":".right";if(single?params.value(key+suffix).toDouble()==0:(params.value(key).toDouble()==0 && params.value(key+".left").toDouble()==0 && params.value(key+".right").toDouble()==0))set_parameter(key,50);else remember();if(key=="makeup.lip" || key=="hair.accessory")build_parameters();});}palette_row->addStretch();parameter_layout_->addWidget(palette_widget);
        const auto refreshPalette=[this,colors,palette_widget,palette]{const auto entries=colors->currentData().toString()=="makeup.iris"?QStringList{"#755647","#5a4636","#7d7667","#5b7566","#587b91","#81738d"}:colors->currentData().toString()=="makeup.brow"?QStringList{"#302722","#49372b","#755647","#8b6e50","#665d53","#a18a6d"}:(colors->currentData().toString()=="makeup.liner" || colors->currentData().toString()=="makeup.lash")?QStringList{"#292323","#49372b","#59443a","#5b4252","#393c48","#67564e"}:colors->currentData().toString().startsWith("makeup.shadow")?QStringList{"#b58b77","#986c55","#725044","#8b6877","#71697c","#4c4546"}:palette;const auto chips=palette_widget->findChildren<QPushButton*>();for(int i=0;i<chips.size() && i<entries.size();++i){auto* swatch=chips[i];const auto color=entries[i];swatch->setProperty("portraitColor",color);swatch->setObjectName("portraitSwatch_"+color.mid(1));swatch->setToolTip(color);swatch->setStyleSheet(QString("QPushButton{background:%1;border:2px solid #40464c;border-radius:5px;}QPushButton:checked{border:3px solid #70b6ff;}").arg(color));bool selected=false;if(selected_>=0){const auto& settings=people_[std::size_t(selected_)].settings;const auto current=editing::portrait_side_value(settings.value("colors").toObject(),colors->currentData().toString(),settings.value("side").toInt()==0).toArray();if(current.size()==3){QImage chip(1,1,QImage::Format_RGBA8888);chip.setColorSpace(QColorSpace::SRgb);chip.setPixelColor(0,0,QColor(color));const auto linear=editing::to_working(chip);const auto* c=reinterpret_cast<const float*>(linear.constBits());selected=std::abs(c[0]-current[0].toDouble())+std::abs(c[1]-current[1].toDouble())+std::abs(c[2]-current[2].toDouble())<.001;}}swatch->setChecked(selected);}};
        refreshPalette();connect(colors,&QComboBox::currentIndexChanged,this,[refreshPalette](int){refreshPalette();});

        button(tr("选择当前项目颜色…"),"portraitStyleColor",[this,colors]{if(selected_<0)return;const auto chosen=QColorDialog::getColor(QColor(175,65,85),this,tr("颜色"));if(!chosen.isValid())return;QImage image(1,1,QImage::Format_RGBA8888);image.setColorSpace(QColorSpace::SRgb);image.setPixelColor(0,0,chosen);const auto linear=editing::to_working(image);const auto* p=reinterpret_cast<const float*>(linear.constBits());auto& settings=people_[std::size_t(selected_)].settings;const bool lip=colors->currentData().toString()=="makeup.lip";editing::portrait_set_style_color(settings,colors->currentData().toString(),QJsonArray{p[0],p[1],p[2]});remember();if(lip)build_parameters();});
    }
    if(panel==12 && section==2)build_repairs(12);
    if(panel==12 && section==0){
        button(tr("新增染发分区"),"portraitAddHairZone",[this]{if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;settings["hairColorVersion"]=2;auto zones=settings["hairZones"].toArray();const QString key="hair.zone_"+QUuid::createUuid().toString(QUuid::WithoutBraces);zones.append(QJsonObject{{"key",key},{"name",tr("分区 %1").arg(zones.size()+1)},{"mode","solid"}});settings["hairZones"]=zones;auto parameters=settings["parameters"].toObject();parameters[key]=50;settings["parameters"]=parameters;auto masks=settings["masks"].toObject();QImage empty(preview_.size().scaled(768,768,Qt::KeepAspectRatio),QImage::Format_Grayscale8);empty.fill(0);masks[key]=editing::portrait_mask_png(empty);settings["masks"]=masks;remember();build_parameters();refresh_ranges();range_key_->setCurrentIndex(range_key_->findData(key));inspector_tabs_->setCurrentIndex(1);range_mode_=1;range_visible_->setChecked(true);view_->set_tool(-1);});
        auto* zones=new QComboBox(parameter_host_);zones->setObjectName("portraitHairZones");for(const auto& v:settings["hairZones"].toArray()){const auto z=v.toObject();zones->addItem(z["name"].toString(),z["key"].toString());}parameter_layout_->addWidget(zones);
        auto* mode=new QComboBox(parameter_host_);mode->setObjectName("portraitHairZoneMode");mode->addItem(tr("单色"),"solid");mode->addItem(tr("渐变染"),"gradient");mode->addItem(tr("挑染"),"highlights");parameter_layout_->addWidget(mode);
        auto* strength=new QSlider(Qt::Horizontal,parameter_host_);strength->setRange(0,100);bind_slider(strength,100);strength->setValue(50);parameter_layout_->addWidget(strength);
        if(zones->count()>0){const auto first=settings["hairZones"].toArray()[0].toObject();mode->setCurrentIndex(std::max(0,mode->findData(first["mode"].toString())));strength->setValue(settings["parameters"].toObject()[first["key"].toString()].toInt(50));}
        connect(zones,&QComboBox::currentIndexChanged,this,[this,zones,mode,strength](int i){if(selected_<0 || i<0)return;placement_key_.clear();update_overlay();const auto settings=people_[std::size_t(selected_)].settings;const auto z=settings["hairZones"].toArray()[i].toObject();const QSignalBlocker block(mode),block2(strength);mode->setCurrentIndex(std::max(0,mode->findData(z["mode"].toString())));strength->setValue(settings["parameters"].toObject()[zones->currentData().toString()].toInt(50));});
        connect(mode,&QComboBox::activated,this,[this,zones,mode](int){if(selected_<0 || zones->currentIndex()<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();auto z=list[zones->currentIndex()].toObject();placement_key_.clear();update_overlay();z["mode"]=mode->currentData().toString();if(z["mode"].toString()=="highlights")z["strandVersion"]=2;list[zones->currentIndex()]=z;settings["hairZones"]=list;remember();});
        connect(strength,&QSlider::valueChanged,this,[this,zones](int value){if(selected_<0 || zones->currentIndex()<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto params=settings["parameters"].toObject();params[zones->currentData().toString()]=value;settings["parameters"]=params;remember();});
        auto* strands=new QWidget(parameter_host_);auto* strandLayout=new QVBoxLayout(strands);strandLayout->setContentsMargins(0,0,0,0);
        for(const auto& entry:std::vector<std::pair<QString,QString>>{{"strandWidth",tr("挑染发束宽度")},{"strandDensity",tr("挑染发束密度")}}){strandLayout->addWidget(new QLabel(entry.second,strands));auto* control=new QSlider(Qt::Horizontal,strands);control->setObjectName("portraitHair_"+entry.first);control->setRange(0,100);bind_slider(control,entry.first=="strandWidth"?35:40);strandLayout->addWidget(control);
            const auto refresh=[this,zones,control,key=entry.first]{const QSignalBlocker block(control);if(selected_<0 || zones->currentIndex()<0)return;control->setValue(people_[std::size_t(selected_)].settings["hairZones"].toArray()[zones->currentIndex()].toObject()[key].toInt(key=="strandWidth"?35:40));};refresh();connect(zones,&QComboBox::currentIndexChanged,this,[refresh](int){refresh();});
            connect(control,&QSlider::valueChanged,this,[this,zones,key=entry.first](int value){if(selected_<0 || zones->currentIndex()<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();auto zone=list[zones->currentIndex()].toObject();zone[key]=value;zone["strandVersion"]=2;list[zones->currentIndex()]=zone;settings["hairZones"]=list;remember();});
        }
        auto* strandNote=new QLabel(tr("按原图发丝方向挑染；可涂抹分区选择发束。方向不清晰的区域不自动生成，可改用手绘单色。"),strands);strandNote->setWordWrap(true);strandLayout->addWidget(strandNote);parameter_layout_->addWidget(strands);const auto showStrands=[strands,mode]{strands->setVisible(mode->currentData().toString()=="highlights");};showStrands();connect(mode,&QComboBox::currentIndexChanged,this,[showStrands](int){showStrands();});
        button(tr("在画布上调整渐变轴"),"portraitHairGradientAxis",[this,zones,mode]{if(selected_<0 || zones->currentIndex()<0)return;finish_brush_gesture();auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();auto z=list[zones->currentIndex()].toObject();z["mode"]="gradient";if(z["gradientAxis"].toArray().size()!=4){const auto f=people_[std::size_t(selected_)].face;z["gradientAxis"]=QJsonArray{f.center().x(),std::max(0.,f.y()-f.height()*.5),f.center().x(),std::min(1.,f.bottom())};}if(z["secondColor"].toArray().size()!=3)z["secondColor"]=QJsonArray{.15,.04,.025};list[zones->currentIndex()]=z;settings["hairZones"]=list;settings["hairColorVersion"]=2;remember();mode->setCurrentIndex(mode->findData("gradient"));placement_key_="gradient:"+zones->currentData().toString();repair_mode_=0;range_mode_=0;range_visible_->setChecked(false);locate_->setChecked(false);view_->set_tool(-1);update_overlay();status_->setText(tr("拖动圆点调整起色、方点调整终色；空白处拖出新渐变轴，空格临时抓手"));});
        button(tr("设置渐变终点颜色…"),"portraitHairSecondColor",[this,zones]{if(selected_<0 || zones->currentIndex()<0)return;const auto color=QColorDialog::getColor(Qt::darkRed,this,tr("渐变终点"));if(!color.isValid())return;QImage image(1,1,QImage::Format_RGBA8888);image.setColorSpace(QColorSpace::SRgb);image.setPixelColor(0,0,color);const auto linear=editing::to_working(image);const auto* p=reinterpret_cast<const float*>(linear.constBits());auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();auto z=list[zones->currentIndex()].toObject();z["secondColor"]=QJsonArray{p[0],p[1],p[2]};list[zones->currentIndex()]=z;settings["hairZones"]=list;remember();});
        button(tr("删除所选分区"),"portraitRemoveHairZone",[this,zones]{if(selected_<0 || zones->currentIndex()<0)return;auto& settings=people_[std::size_t(selected_)].settings;placement_key_.clear();update_overlay();const auto key=zones->currentData().toString();auto list=settings["hairZones"].toArray();list.removeAt(zones->currentIndex());settings["hairZones"]=list;for(const auto* field:{"parameters","masks","colors"}){auto object=settings[field].toObject();object.remove(key);settings[field]=object;}remember();build_parameters();refresh_ranges();});
        button(tr("发际线取样修补"),"portraitHairRepair",[this]{repair_mode_=4;repair_selected_=-1;range_mode_=0;locate_->setChecked(false);view_->set_tool(-1);status_->setText(tr("Alt 单击已有头发作为来源，再涂抹发际线；不生成新发丝"));});
    }
    if(panel==13 && section==1){button(tr("手绘修容范围"),"portraitContourPaint",[this]{range_key_->setCurrentIndex(range_key_->findData("contour.paint"));range_mode_=1;range_visible_->setChecked(true);view_->set_tool(-1);inspector_tabs_->setCurrentIndex(1);set_parameter("contour.paint",40);});button(tr("锁定已画范围"),"portraitContourLock",[this]{if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;settings["contourLocked"]=!settings["contourLocked"].toBool();range_mode_=0;view_->set_tool(0);remember();build_parameters();});if(auto* lock=findChild<QPushButton*>("portraitContourLock")){lock->setCheckable(true);lock->setChecked(settings["contourLocked"].toBool());lock->setText(settings["contourLocked"].toBool()?tr("已锁定范围 · 点击解锁"):tr("锁定已画范围"));}}
    if(panel==14)button(tr("计算补光建议（可撤销预览）"),"portraitLightSuggest",[this]{if(selected_<0)return;const auto record=editing::portrait_person_json(people_[std::size_t(selected_)]);const auto mask=editing::portrait_effect_mask(record,"light.face",linear_preview_.size());double sum{},weight{};for(int y=0;y<mask.height();y+=3)for(int x=0;x<mask.width();x+=3){const double w=mask.constScanLine(y)[x]/255.;const auto* p=reinterpret_cast<const float*>(linear_preview_.constScanLine(y))+x*4;if(p[3]<.01F)continue;sum+=(.2627*p[0]+.678*p[1]+.0593*p[2])/p[3]*w;weight+=w;}const double light=weight>0?sum/weight:.18;set_parameter("light.face",std::clamp(std::log2(.22/std::max(.005,light))*100.,0.,75.));set_parameter("light.highlights",80);build_parameters();status_->setText(tr("补光建议已预览，可逐项调整或撤销；不会重建真实光源"));});
}
}

namespace huelith::desktop {
bool PortraitDialog::nail_event(QEvent* event){
    if(placement_key_!="nail:outline" || view_->temporary_pan())return false;
    if(people_[std::size_t(selected_)].unmatched || people_[std::size_t(selected_)].match_ambiguous)return true;
    const auto type=event->type();if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);const auto p=view_->mapToScene(mouse->position().toPoint());
    if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){if(!preview_.rect().contains(p.toPoint()))return true;finish_brush_gesture();view_->setFocus();nail_outline_={p};nail_drawing_=true;painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->viewport()->grabMouse();}
    if(nail_drawing_){const QPointF q(std::clamp(p.x(),0.,double(preview_.width())),std::clamp(p.y(),0.,double(preview_.height())));if(nail_outline_.size()<2048 && QLineF(nail_outline_.last(),q).length()>.35)nail_outline_<<q;update_overlay();if(type==QEvent::MouseButtonRelease && mouse->button()==Qt::LeftButton)finish_nail_outline(true);}
    return true;
}
void PortraitDialog::finish_nail_outline(bool commit){
    if(!nail_drawing_)return;nail_drawing_=false;view_->viewport()->releaseMouse();
    if(commit && selected_>=0 && nail_outline_.size()>=3){
        QJsonArray outline;for(const auto& p:nail_outline_){const auto q=source_position(p);outline.append(QJsonArray{q.x(),q.y()});}QJsonObject nail{{"outline",outline},{"color",QJsonArray{.32,.018,.035}},{"enabled",true},{"origin","manual"}};
        const auto path=editing::portrait_nail_path(nail,linear_preview_.size());const auto box=path.boundingRect().toAlignedRect().intersected(linear_preview_.rect());std::vector<double> light;
        for(int y=box.top();y<=box.bottom();y+=2){const auto* row=reinterpret_cast<const float*>(linear_preview_.constScanLine(y));for(int x=box.left();x<=box.right();x+=2)if(path.contains(QPointF(x+.5,y+.5)) && row[x*4+3]>.001)light.push_back((.2627*row[x*4]+.678*row[x*4+1]+.0593*row[x*4+2])/row[x*4+3]);}
        auto& s=people_[std::size_t(selected_)].settings;auto nails=s["nails"].toArray();if(light.size()>=3 && nails.size()<40){std::nth_element(light.begin(),light.begin()+light.size()/2,light.end());nail["referenceLuminance"]=light[light.size()/2];const int replacement=property("portraitNailIndex").toInt();const bool replace=property("portraitNailReplace").toBool() && replacement>=0 && replacement<nails.size();if(replace){nail["color"]=nails[replacement].toObject()["color"];nails[replacement]=nail;}else nails.append(nail);s["nails"]=nails;auto params=s["parameters"].toObject();if(params["makeup.nails"].toDouble()==0)params["makeup.nails"]=85;if(!params.contains("makeup.nailsGloss"))params["makeup.nailsGloss"]=70;s["parameters"]=params;auto enabled=s["enabled"].toObject();enabled["makeup"]=true;s["enabled"]=enabled;auto disabled=s["disabledParameters"].toObject();disabled.remove("makeup.nails");s["disabledParameters"]=disabled;setProperty("portraitNailIndex",replace?replacement:nails.size()-1);setProperty("portraitNailReplace",false);remember();build_parameters();status_->setText(tr("甲面已添加，可继续圈选下一个或单独选色"));}else status_->setText(tr("甲面过小或已达到 40 个甲面，请放大后重新圈选"));
    }nail_outline_.clear();update_overlay();
}
}

namespace huelith::desktop {
bool PortraitDialog::tattoo_event(QEvent* event){
    if(placement_key_!="makeup.tattoo" || view_->temporary_pan())return false;
    auto& person=people_[std::size_t(selected_)];if(person.unmatched || person.match_ambiguous || (person.body_ambiguous && !person.settings["partialSkin"].toBool()))return true;
    const auto type=event->type();if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);const auto point=view_->mapToScene(mouse->position().toPoint());auto quad=person.settings.value("tattooQuad").toArray();
    auto qpoint=[&](int i){const auto a=quad[i].toArray();return QPointF(a[0].toDouble()*preview_.width(),a[1].toDouble()*preview_.height());};
    if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){if(!preview_.rect().contains(point.toPoint()))return true;finish_brush_gesture();finish_gesture();view_->setFocus();gesture_=placement_drawing_=true;placement_last_=point;tattoo_handle_=-1;
        if(quad.size()==4){double distance=12/std::max(.01,view_->transform().m11());for(int i=0;i<4;++i){const double d=QLineF(point,qpoint(i)).length();if(d<distance){distance=d;tattoo_handle_=i;}}}
        else{const double radius=std::min(preview_.width(),preview_.height())*.10;const double cx=std::clamp(point.x(),radius,double(preview_.width())-radius),cy=std::clamp(point.y(),radius,double(preview_.height())-radius);for(const auto& d:QPolygonF{{-1,-1},{1,-1},{1,1},{-1,1}})quad.append(QJsonArray{(cx+d.x()*radius)/preview_.width(),(cy+d.y()*radius)/preview_.height()});person.settings["tattooQuad"]=quad;remember();}
        view_->viewport()->grabMouse();update_overlay();return true;
    }
    if(!placement_drawing_ || quad.size()!=4)return true;
    if(type==QEvent::MouseMove || type==QEvent::MouseButtonRelease){
        if(tattoo_handle_>=0)quad[tattoo_handle_]=QJsonArray{std::clamp(point.x()/preview_.width(),0.,1.),std::clamp(point.y()/preview_.height(),0.,1.)};
        else{QPointF delta((point.x()-placement_last_.x())/preview_.width(),(point.y()-placement_last_.y())/preview_.height());double minx=1,miny=1,maxx=0,maxy=0;for(const auto& v:quad){const auto a=v.toArray();minx=std::min(minx,a[0].toDouble());maxx=std::max(maxx,a[0].toDouble());miny=std::min(miny,a[1].toDouble());maxy=std::max(maxy,a[1].toDouble());}delta.setX(std::clamp(delta.x(),-minx,1-maxx));delta.setY(std::clamp(delta.y(),-miny,1-maxy));for(int i=0;i<4;++i){const auto a=quad[i].toArray();quad[i]=QJsonArray{a[0].toDouble()+delta.x(),a[1].toDouble()+delta.y()};}}
        placement_last_=point;auto proposed=editing::portrait_person_json(person);auto settings=proposed["settings"].toObject();settings["tattooQuad"]=quad;proposed["settings"]=settings;
        if(editing::portrait_tattoo_transform(proposed,preview_.size()).isInvertible()){person.settings["tattooQuad"]=quad;remember();update_overlay();}
        if(type==QEvent::MouseButtonRelease && mouse->button()==Qt::LeftButton){placement_drawing_=false;view_->viewport()->releaseMouse();finish_gesture();}return true;
    }return true;
}
}
namespace huelith::desktop {
void PortraitDialog::analyze_nails(){
    if(selected_<0 || repair_analysis_busy_ || busy_)return;finish_brush_gesture();finish_gesture();
    auto directory=property("modelDirectory").toString();if(directory.isEmpty())directory=QCoreApplication::applicationDirPath()+"/models";
    const auto model=QDir(directory).filePath("nails-ccby.onnx");const auto image=linear_preview_;
    auto& person=people_[std::size_t(selected_)];repair_analysis_person_=person.id;repair_analysis_masks_=person.settings["masks"].toObject();repair_analysis_landmarks_=person.landmarks;
    setProperty("portraitNailAnalysisRecipe",QJsonDocument(recipe()).toJson(QJsonDocument::Compact));
    cancelled_=false;repair_analysis_busy_=true;repair_analysis_refresh_blemishes_=false;analysis_merge_recipe_={};
    repair_analysis_=std::async(std::launch::async,[this,image,model]{return editing::run_ai_job(QJsonObject{{"operation","portrait-nails"},{"model",model},{"directml",false}},image,&cancelled_).metadata;});
    detect_->setEnabled(false);accept_->setEnabled(false);cancel_compute_->setText(tr("取消识别"));cancel_compute_->setEnabled(true);cancel_compute_->show();status_->setText(tr("正在识别可见甲面…"));
}
void PortraitDialog::apply_nail_analysis(const QJsonObject& analysis){
    if(property("portraitNailAnalysisRecipe").toByteArray()!=QJsonDocument(recipe()).toJson(QJsonDocument::Compact)){status_->setText(tr("识别期间设置已变化，保留当前甲面；可再次识别"));return;}
    auto& person=people_[std::size_t(selected_)];auto& settings=person.settings;const auto old=settings["nails"].toArray();QJsonArray nails;for(const auto& value:old)if(value.toObject()["origin"].toString()!="automatic")nails.append(value);
    int added=0;for(const auto& value:analysis["nails"].toArray()){
        auto nail=value.toObject();const auto bounds=editing::portrait_nail_path(nail,{1000,1000}).boundingRect();const auto center=bounds.center()/1000.;
        // A full-photo detector cannot confidently associate overlapping people.
        // Keep manual selection available instead of silently assigning their nails.
        if(people_.size()>1){int owner=-1,count=0;for(int i=0;i<int(people_.size());++i){const auto body=editing::portrait_person_json(people_[std::size_t(i)])["body"].toArray();if(body.size()==4 && QRectF(body[0].toDouble(),body[1].toDouble(),body[2].toDouble(),body[3].toDouble()).contains(center)){owner=i;++count;}}if(count!=1 || owner!=selected_)continue;}
        bool manual=false;for(const auto& saved:nails)if(editing::portrait_nail_path(saved.toObject(),{1000,1000}).contains(bounds.center())){manual=true;break;}if(manual)continue;
        for(const auto& saved:old){const auto item=saved.toObject();if(editing::portrait_nail_path(item,{1000,1000}).contains(bounds.center())){nail["color"]=item["color"];break;}}
        nails.append(nail);++added;if(nails.size()>=40)break;
    }
    if(added==0){settings["nailDetectionComplete"]=true;build_parameters();status_->setText(tr("未识别到可可靠归属的甲面，已有甲面保留；可手动圈选"));return;}
    settings["nails"]=nails;settings["nailDetectionComplete"]=true;setProperty("portraitNailIndex",0);
    auto parameters=settings["parameters"].toObject();if(!parameters.contains("makeup.nails"))parameters["makeup.nails"]=85;if(!parameters.contains("makeup.nailsGloss"))parameters["makeup.nailsGloss"]=70;settings["parameters"]=parameters;
    remember();build_parameters();update_overlay();status_->setText(added?tr("已识别 %1 个甲面，可逐个选色或修正边缘").arg(added):tr("没有找到可可靠归属的甲面，可圈选补充；已有甲面保留"));
}
}
