#include "portrait_dialog.h"
#include "raw_preview_view.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/ai_worker.h"
#include "huelith/editing/healing.h"
#include "huelith/editing/project.h"
#include <QVBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QMouseEvent>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QRegion>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QSignalBlocker>
#include <QUuid>

namespace huelith::desktop {
QPointF PortraitDialog::source_position(QPointF scene) const {
    QPointF p(scene.x()/preview_.width(),scene.y()/preview_.height());const auto mesh=(nail_drawing_ || repair_drawing_ || repair_source_dragging_ || placement_key_=="head:outline" || placement_key_=="body:manualSlim" || placement_key_=="body:manualScale" || placement_key_=="body:manualDistort")?painting_mesh_:editing::portrait_deformation(source_.size(),recipe());
    if(!mesh.isNull()){const auto* row=reinterpret_cast<const float*>(mesh.constScanLine(std::clamp(int(p.y()*(mesh.height()-1)),0,mesh.height()-1)))+std::clamp(int(p.x()*(mesh.width()-1)),0,mesh.width()-1)*4;p+=QPointF(row[0],row[1]);}return {std::clamp(p.x(),0.,1.),std::clamp(p.y(),0.,1.)};
}
QPointF PortraitDialog::repair_scene_position(QPointF source) const {
    const auto& mesh=painting_mesh_;if(mesh.isNull())return {source.x()*preview_.width(),source.y()*preview_.height()};
    const auto displacement=[&](QPointF point){const double x=std::clamp(point.x(),0.,1.)*(mesh.width()-1),y=std::clamp(point.y(),0.,1.)*(mesh.height()-1);const int ix=int(x),iy=int(y);QPointF value;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto* p=reinterpret_cast<const float*>(mesh.constScanLine(std::min(iy+dy,mesh.height()-1)))+std::min(ix+dx,mesh.width()-1)*4;value+=QPointF(p[0],p[1])*(dx?x-ix:1-x+ix)*(dy?y-iy:1-y+iy);}return value;};
    QPointF q=source;for(int i=0;i<40;++i){const auto residual=q+displacement(q)-source;if(QPointF::dotProduct(residual,residual)<1e-12)break;q-=residual*.5;}
    return {q.x()*preview_.width(),q.y()*preview_.height()};
}
void PortraitDialog::update_repair_overlay(){
    if(selected_<0 || repair_selected_<0)return;const auto repairs=people_[std::size_t(selected_)].settings["repairs"].toArray();if(repair_selected_>=repairs.size())return;
    const auto stroke=repairs[repair_selected_].toObject();const auto points=stroke["points"].toArray();if(points.isEmpty())return;const auto first=points[0].toArray();const QPointF target(first[0].toDouble(),first[1].toDouble()),source=target+QPointF(stroke["offsetX"].toDouble(),stroke["offsetY"].toDouble());
    const auto a=repair_scene_position(target),b=repair_scene_position(source);const double radius=std::max(5./view_->transform().m11(),stroke["radius"].toDouble()*std::min(preview_.width(),preview_.height()));QPainterPath path;path.addEllipse(a,radius,radius);
    if(stroke["wrinkleMethod"].toString()!="lighting-v2"){path.moveTo(a);path.lineTo(b);path.addRect(QRectF(b-QPointF(radius,radius),QSizeF(radius*2,radius*2)));}
    overlay_->setPath(path);
}
void PortraitDialog::analyze_repairs() {
    analyze_repairs_with_model(false);
}
void PortraitDialog::analyze_repairs_with_model(bool detect_blemishes) {
    if(selected_<0 || repair_analysis_busy_ || busy_)return;
    QString model;
    if(detect_blemishes){
        auto directory=property("modelDirectory").toString();
        if(directory.isEmpty())directory=QCoreApplication::applicationDirPath()+"/models";
        model=QDir(directory).filePath("blemish_detection.onnx");
        if(!QFileInfo::exists(model)){setProperty("portraitBlemishError",tr("缺少瑕疵检测模型，请使用完整安装包"));build_parameters();status_->setText(tr("瑕疵检测模型缺失，请使用包含 models 文件夹的完整安装包；仍可手动修复"));return;}
    }
    if(detect_blemishes)setProperty("portraitBlemishError",QString{});
    analysis_merge_recipe_={};cancelled_=false;repair_analysis_busy_=true;repair_analysis_person_=people_[std::size_t(selected_)].id;repair_analysis_masks_=people_[std::size_t(selected_)].settings.value("masks").toObject();repair_analysis_landmarks_=people_[std::size_t(selected_)].landmarks;
    const auto image=linear_preview_;const auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);
    repair_analysis_refresh_blemishes_=detect_blemishes;
    repair_analysis_=std::async(std::launch::async,[this,image,person,model]{
        return editing::run_ai_job(QJsonObject{{"operation","portrait-repair"},{"directml",false},{"person",person},{"model",model}},image,&cancelled_).metadata;
    });
    if(auto* b=findChild<QPushButton*>("portraitPresetApply"))b->setEnabled(false);
    if(auto* edge=findChild<QCheckBox*>("portraitHairRefineEdges"))edge->setEnabled(false);
    detect_->setEnabled(false);accept_->setEnabled(false);cancel_compute_->setText(tr("取消分析"));cancel_compute_->setEnabled(true);cancel_compute_->show();status_->setText(tr("正在分析修复范围…"));if(detect_blemishes)build_parameters();
}

void PortraitDialog::build_repairs(int panel) {
    const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};
    auto add_button=[&](QString text,QString name,auto fn){auto* button=new QPushButton(text,parameter_host_);button->setObjectName(name);button->setEnabled(selected_>=0);parameter_layout_->addWidget(button);connect(button,&QPushButton::clicked,this,[this,fn]{try{fn();}catch(const std::exception& error){status_->setText(QString::fromUtf8(error.what()));}});return button;};
    if(panel==7)add_button(tr("检测瑕疵建议"),"portraitAnalyzeRepairs",[this]{analyze_repairs_with_model(true);});
    if(panel==10){auto* label=new QLabel(settings["teethStatus"].toString(tr("正在自动准备牙齿范围；闭嘴时不会生成牙齿")),parameter_host_);label->setWordWrap(true);parameter_layout_->addWidget(label);return;}
    if(panel==9){auto* label=new QLabel(settings["eyeStatus"].toString(tr("正在准备眼周独立范围"))+QStringLiteral("；")+settings["redEyeStatus"].toString()+tr("。眼镜反光和遮挡可在范围页减选。"),parameter_host_);label->setWordWrap(true);parameter_layout_->addWidget(label);return;}
    auto* help=new QLabel(panel==12?tr("先按住 Alt 单击完整头发取样，松开 Alt 后在缺发处按住拖动。发质滑块无需涂抹。"):panel==8?tr("直接沿皱纹连续涂抹，拖动时预览淡化效果。空格临时抓手。"):tr("直接涂抹需要修复的位置。空格临时抓手，Alt 可指定取样来源。"),parameter_host_);help->setWordWrap(true);parameter_layout_->addWidget(help);
    add_button(panel==8?tr("绘制去皱笔画"):panel==12?tr("发际线修补笔"):tr("取样修复笔"),"portraitRepairBrush",[this,panel]{repair_mode_=panel==8?2:panel==12?4:1;repair_selected_=-1;range_mode_=0;locate_->setChecked(false);view_->set_tool(-1);status_->setText(panel==8?tr("沿皱纹涂抹以淡化明暗；Alt 指定来源可切换为取样修复"):panel==12?tr("先 Alt 单击头发取样，松开 Alt 后拖动补发"):tr("涂抹目标；Alt 单击指定来源，未指定时使用邻近匹配来源"));});
    if(panel==7){add_button(tr("妆容取样修复"),"portraitMakeupRepair",[this]{repair_mode_=3;repair_has_source_=false;repair_selected_=-1;brush_size_->setValue(std::max(2,int(people_[std::size_t(selected_)].face.width()*source_.width()*.025)));range_mode_=0;locate_->setChecked(false);view_->set_tool(-1);status_->setText(tr("直接涂抹卡粉处，自动从同一妆容部位取样；Alt 可指定来源"));});
        add_button(tr("遮瑕笔"),"portraitConcealBrush",[this]{repair_mode_=0;range_key_->setCurrentIndex(range_key_->findData("repair.conceal"));range_mode_=1;range_visible_->setChecked(true);locate_->setChecked(false);view_->set_tool(-1);set_parameter("repair.conceal",60);status_->setText(tr("直接涂抹，首次自动建议邻近肤色；Alt 可重新取色"));});}
    if(panel==7)for(bool erase:{false,true})add_button(erase?tr("擦除保留标记"):tr("保留痣／雀斑"),erase?"portraitPreserveErase":"portraitPreserveBrush",[this,erase]{
        repair_mode_=0;repair_selected_=-1;range_key_->setCurrentIndex(range_key_->findData("repair.preserve"));range_mode_=erase?2:1;range_visible_->setChecked(true);locate_->setChecked(false);view_->set_tool(-1);status_->setText(erase?tr("涂抹取消手绘保留区域；建议列表中的保留可再次点击取消"):tr("涂抹要保留的痣或雀斑；局部修复与遮瑕将避开此区域"));
    });
    if(panel==8)for(const bool restore:{false,true})add_button(restore?tr("恢复当前去皱笔画"):tr("擦除当前去皱笔画"),restore?"portraitWrinkleRestore":"portraitWrinkleErase",[this,restore]{
        if(selected_<0)return;const auto strokes=people_[std::size_t(selected_)].settings["repairs"].toArray();int target=repair_selected_;
        if(target<0 || target>=strokes.size() || strokes[target].toObject()["kind"].toString()!="wrinkle"){target=-1;for(int i=strokes.size()-1;i>=0;--i)if(strokes[i].toObject()["kind"].toString()=="wrinkle"){target=i;break;}}
        if(target<0){status_->setText(tr("先绘制一笔去皱，再局部擦除或恢复"));return;}
        refresh_ranges();const auto key="repair.item."+strokes[target].toObject()["id"].toString();const int item=range_key_->findData(key);if(item<0)return;
        range_key_->setCurrentIndex(item);repair_mode_=0;repair_selected_=target;range_mode_=restore?1:2;locate_->setChecked(false);view_->set_tool(-1);range_visible_->setChecked(false);status_->setText(restore?tr("涂抹恢复这一笔的去皱效果"):tr("涂抹局部还原，只影响这一笔去皱"));
    });
    if(panel==7){auto* swatch=new QLabel(parameter_host_);swatch->setObjectName("portraitConcealSwatch");swatch->setFixedHeight(24);swatch->setToolTip(tr("遮瑕参考肤色；Alt 单击画面可修改"));const auto color=settings["concealColor"].toArray();
        if(color.size()==3){QImage image(1,1,QImage::Format_RGBA32FPx4_Premultiplied);image.setColorSpace(editing::working_space());auto* pixel=reinterpret_cast<float*>(image.bits());for(int c=0;c<3;++c)pixel[c]=float(color[c].toDouble());pixel[3]=1;swatch->setPixmap(QPixmap::fromImage(editing::display_image(image).scaled(48,20)));}else swatch->setText(tr("首次涂抹时建议邻近肤色"));parameter_layout_->addWidget(swatch);
    }
    auto* stop=add_button(tr("结束绘制／抓手"),"portraitRepairStop",[this]{repair_mode_=0;range_mode_=0;repair_has_source_=false;view_->set_tool(0);});stop->hide();
    auto* size=new QSlider(Qt::Horizontal,parameter_host_);size->setObjectName("portraitRepairSize");size->setRange(1,500);size->setValue(brush_size_->value());parameter_layout_->addWidget(new QLabel(tr("大小（原图像素）"),parameter_host_));parameter_layout_->addWidget(size);connect(size,&QSlider::valueChanged,brush_size_,&QSlider::setValue);connect(brush_size_,&QSlider::valueChanged,size,&QSlider::setValue);
    auto* feather=new QSlider(Qt::Horizontal,parameter_host_);feather->setObjectName("portraitRepairFeather");feather->setRange(0,100);feather->setValue(brush_feather_->value());bind_slider(feather,50);parameter_layout_->addWidget(new QLabel(tr("羽化"),parameter_host_));parameter_layout_->addWidget(feather);connect(feather,&QSlider::valueChanged,brush_feather_,&QSlider::setValue);connect(brush_feather_,&QSlider::valueChanged,feather,&QSlider::setValue);
    const int advanced_begin=parameter_layout_->count();
    auto* list=new QListWidget(parameter_host_);list->setObjectName("portraitRepairList");list->setMaximumHeight(180);list->setIconSize(QSize(40,40));const auto strokes=settings["repairs"].toArray();for(int i=0;i<strokes.size();++i){const auto s=strokes[i].toObject();if((s["kind"].toString()=="wrinkle")!=(panel==8) || (s["kind"].toString()=="hairline")!=(panel==12))continue;auto* item=new QListWidgetItem(tr("%1 %2").arg(s["kind"].toString()=="wrinkle"?tr("去皱"):s["kind"].toString()=="makeup"?tr("妆容修复"):tr("修复")).arg(i+1),list);item->setData(Qt::UserRole,i);const auto points=s["points"].toArray();const auto first=points.isEmpty()?QJsonArray{}:points.first().toArray();const QPoint center(int(first[0].toDouble()*preview_.width()),int(first[1].toDouble()*preview_.height()));const int extent=std::max(16,int(s["radius"].toDouble()*std::min(preview_.width(),preview_.height())*3));const auto crop=QRect(center-QPoint(extent,extent),QSize(extent*2,extent*2)).intersected(preview_.rect());if(first.size()>=2 && !crop.isEmpty())item->setIcon(QPixmap::fromImage(preview_.copy(crop).scaled(40,40,Qt::KeepAspectRatio,Qt::SmoothTransformation)));item->setToolTip(s["wrinkleMethod"].toString()=="lighting-v2"?tr("明暗去皱；选择后可局部擦除或恢复"):tr("取样修复；选择后拖动画面中的来源方框"));item->setCheckState(s["enabled"].toBool(true)?Qt::Checked:Qt::Unchecked);if(i==repair_selected_)list->setCurrentItem(item);}parameter_layout_->addWidget(list);
    auto* opacity=new QSlider(Qt::Horizontal,parameter_host_);opacity->setObjectName("portraitRepairOpacity");opacity->setRange(0,100);bind_slider(opacity,70);opacity->setValue(int(repair_opacity_*100));parameter_layout_->addWidget(new QLabel(tr("当前笔画／新笔画强度"),parameter_host_));parameter_layout_->addWidget(opacity);
    connect(list,&QListWidget::currentItemChanged,this,[this,opacity,feather](QListWidgetItem* item){repair_selected_=item?item->data(Qt::UserRole).toInt():-1;if(repair_selected_>=0 && selected_>=0){const QSignalBlocker block(opacity),featherBlock(feather);const auto selectedStroke=people_[std::size_t(selected_)].settings.value("repairs").toArray()[repair_selected_].toObject();feather->setValue(int(selectedStroke["feather"].toDouble(.5)*100));repair_opacity_=people_[std::size_t(selected_)].settings.value("repairs").toArray()[repair_selected_].toObject()["opacity"].toDouble(1);opacity->setValue(int(repair_opacity_*100));repair_mode_=selectedStroke["kind"].toString()=="wrinkle"?(selectedStroke["wrinkleMethod"].toString()=="lighting-v2"?2:6):selectedStroke["kind"].toString()=="makeup"?3:selectedStroke["kind"].toString()=="hairline"?4:selectedStroke["clone"].toBool()?5:1;repair_has_source_=false;range_mode_=0;view_->set_tool(-1);painting_mesh_=editing::portrait_deformation(source_.size(),recipe());update_repair_overlay();status_->setText(tr("拖动方框更改来源；圆圈是修复目标。Alt 单击也可改源"));}});
    connect(list,&QListWidget::itemChanged,this,[this](QListWidgetItem* item){if(selected_<0)return;auto& s=people_[std::size_t(selected_)].settings;auto a=s["repairs"].toArray();const int i=item->data(Qt::UserRole).toInt();auto stroke=a[i].toObject();stroke["enabled"]=item->checkState()==Qt::Checked;a[i]=stroke;s["repairs"]=a;remember();});
    connect(feather,&QSlider::valueChanged,this,[this](int value){if(repair_selected_<0 || selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto strokes=settings["repairs"].toArray();if(repair_selected_>=strokes.size())return;auto stroke=strokes[repair_selected_].toObject();stroke["feather"]=value*.01;strokes[repair_selected_]=stroke;settings["repairs"]=strokes;remember();});
    connect(opacity,&QSlider::valueChanged,this,[this](int v){repair_opacity_=v*.01;if(repair_selected_<0 || selected_<0)return;auto& s=people_[std::size_t(selected_)].settings;auto a=s["repairs"].toArray();if(repair_selected_>=a.size())return;auto stroke=a[repair_selected_].toObject();stroke["opacity"]=repair_opacity_;a[repair_selected_]=stroke;s["repairs"]=a;remember();});
    add_button(tr("删除当前笔画"),"portraitRepairDelete",[this]{if(selected_<0 || repair_selected_<0)return;auto& s=people_[std::size_t(selected_)].settings;auto a=s["repairs"].toArray();if(repair_selected_<a.size())a.removeAt(repair_selected_);s["repairs"]=a;repair_selected_=-1;overlay_->setPath(QPainterPath{});remember();build_parameters();});
    if(panel==7){auto* suggestions=new QListWidget(parameter_host_);suggestions->setObjectName("portraitBlemishes");suggestions->setMaximumHeight(100);const auto a=settings["blemishes"].toArray();for(int i=0;i<a.size();++i){const auto entry=a[i].toObject();const auto category=entry["category"].toString();const auto label=category=="red-texture"?tr("红色纹理点"):category=="red-mark"?tr("泛红色印"):category=="pigment"?tr("色素斑点"):tr("瑕疵候选");const auto state=entry["state"].toString();const auto confidence=entry["confidence"].toDouble();auto* item=new QListWidgetItem(tr("%1 · %2 · %3").arg(label).arg(state=="preserved"?tr("已保留"):state=="repaired"?tr("已修复"):tr("待确认")).arg(entry.contains("confidence")?(confidence>=.65?tr("证据较强"):tr("需仔细检查")):QString{}),suggestions);item->setToolTip(tr("仅按颜色和纹理提出建议，不能判断是否应去除；痣、雀斑及个人特征请保留。"));item->setData(Qt::UserRole,i);}parameter_layout_->addWidget(suggestions);
        connect(suggestions,&QListWidget::currentRowChanged,this,[this](int i){
            if(i<0 || selected_<0)return;
            const auto s=people_[std::size_t(selected_)].settings.value("blemishes").toArray()[i].toObject();
            painting_mesh_=editing::portrait_deformation(source_.size(),recipe());
            const auto center=repair_scene_position({s["x"].toDouble(),s["y"].toDouble()});view_->centerOn(center);
            const auto coverage=QImage::fromData(QByteArray::fromBase64(s["coverage"].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
            QPainterPath path;
            if(!coverage.isNull()){
                QRegion region;
                for(int y=0;y<coverage.height();++y){const auto* row=coverage.constScanLine(y);int start=-1;
                    for(int x=0;x<=coverage.width();++x){const bool inside=x<coverage.width() && row[x]>=128;
                        if(inside && start<0)start=x;
                        if(!inside && start>=0){region+=QRect(start,y,x-start,1);start=-1;}}
                }
                QPainterPath sourcePath;sourcePath.addRegion(region);
                for(const auto& polygon:sourcePath.simplified().toSubpathPolygons()){
                    QPolygonF mapped;
                    for(const auto& point:polygon)mapped.append(repair_scene_position({point.x()/coverage.width(),point.y()/coverage.height()}));
                    path.addPolygon(mapped);path.closeSubpath();
                }
            }
            if(path.isEmpty())path.addEllipse(center,8,8);
            overlay_->setPath(path);
        });
        for(bool preserve:{true,false})add_button(preserve?tr("保留此处／取消保留"):tr("确认修复此处"),preserve?"portraitPreserveBlemish":"portraitApplyBlemish",[this,suggestions,preserve]{const int i=suggestions->currentRow();if(i<0 || selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto a=settings["blemishes"].toArray();auto s=a[i].toObject();if(s["state"].toString()=="repaired")return;if(!preserve && s["state"].toString()=="preserved"){status_->setText(tr("此处已保留；先取消保留再修复"));return;}const auto previousState=s["state"];s["state"]=preserve?(previousState.toString()=="preserved"?"pending":"preserved"):"repaired";a[i]=s;settings["blemishes"]=a;if(!preserve){editing::HealingStroke stroke;stroke.points={{s["x"].toDouble(),s["y"].toDouble()}};stroke.radius=s["radius"].toDouble();try{stroke.offset=editing::automatic_healing_offset(linear_preview_,stroke,0,editing::portrait_effect_mask(editing::portrait_person_json(people_[std::size_t(selected_)]),"skin",linear_preview_.size().scaled(768,768,Qt::KeepAspectRatio)));}catch(const std::exception& error){s["state"]=previousState;a[i]=s;settings["blemishes"]=a;status_->setText(QString::fromUtf8(error.what()));return;}auto object=editing::healing_json(stroke);object["kind"]="heal";object["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);if(s.contains("coverage"))object["validatedCandidateCoverage"]=true;auto repairs=settings["repairs"].toArray();repairs.append(object);settings["repairs"]=repairs;if(s.contains("coverage")){auto masks=settings["masks"].toObject();masks["repair.item."+object["id"].toString()]=s["coverage"];settings["masks"]=masks;}}remember();build_parameters();});
    }
    auto* advanced=new QWidget(parameter_host_);auto* advanced_layout=new QVBoxLayout(advanced);advanced_layout->setContentsMargins(0,0,0,0);
    for(int i=advanced_begin;i<parameter_layout_->count();){auto* item=parameter_layout_->itemAt(i);auto* w=item->widget();if(w && (w==opacity || w->objectName()=="portraitBlemishes" || w->objectName()=="portraitPreserveBlemish" || w->objectName()=="portraitApplyBlemish" || (qobject_cast<QLabel*>(w) && w->objectName().isEmpty()))){++i;continue;}item=parameter_layout_->takeAt(i);
        // addItem alone does not reparent QWidget children. They otherwise stay
        // visible in the next page even after this collapsed group is deleted.
        if(w){advanced_layout->addWidget(w);delete item;}else advanced_layout->addItem(item);}
    auto* header=new QToolButton(parameter_host_);header->setObjectName("portraitRepairManagement");header->setText(tr("笔画与取样管理"));header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);header->setArrowType(Qt::RightArrow);header->setCheckable(true);parameter_layout_->addWidget(header);parameter_layout_->addWidget(advanced);advanced->hide();connect(header,&QToolButton::toggled,this,[advanced,header](bool open){advanced->setVisible(open);header->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});

}
bool PortraitDialog::repair_event(QEvent* event) try {
    const int panel=navigation_->currentRow();if(panel!=7 && panel!=8 && panel!=12)return false;const auto type=event->type();if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);if(view_->temporary_pan())return false;const auto scene=view_->mapToScene(mouse->position().toPoint());if(!preview_.rect().contains(scene.toPoint()) && !repair_drawing_ && !repair_source_dragging_)return false;
    if(property("portraitWorkspaceTask").toInt()==5 && property("portraitBlemishPage").toInt()==1){
        if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){const auto marks=people_[std::size_t(selected_)].settings.value("blemishes").toArray();painting_mesh_=editing::portrait_deformation(source_.size(),recipe());for(int i=0;i<marks.size();++i){const auto m=marks[i].toObject();const auto at=repair_scene_position({m["x"].toDouble(),m["y"].toDouble()});if(QLineF(at,scene).length()<std::max(12./view_->transform().m11(),m["radius"].toDouble()*std::min(preview_.width(),preview_.height()))){if(auto* list=findChild<QListWidget*>("portraitBlemishes"))list->setCurrentRow(i);break;}}}return true;
    }
    if(people_[std::size_t(selected_)].unmatched || people_[std::size_t(selected_)].match_ambiguous){
        if(type==QEvent::MouseButtonPress)status_->setText(tr("当前人物尚未关联，请先重新识别或确认人物关联，再使用修复画笔"));
        return true;
    }
    if(panel==8 && property("portraitWrinkleMode").toInt()==1 && mouse->modifiers().testFlag(Qt::AltModifier))return true;
    if(repair_selected_>=0 && range_mode_==0 && !mouse->modifiers().testFlag(Qt::AltModifier)){
        auto& settings=people_[std::size_t(selected_)].settings;auto repairs=settings["repairs"].toArray();if(repair_selected_<repairs.size()){
            auto stroke=repairs[repair_selected_].toObject();const auto first=stroke["points"].toArray().first().toArray();const QPointF target(first[0].toDouble(),first[1].toDouble());
            const auto source=repair_scene_position(target+QPointF(stroke["offsetX"].toDouble(),stroke["offsetY"].toDouble()));const double radius=std::max(8./view_->transform().m11(),stroke["radius"].toDouble()*std::min(preview_.width(),preview_.height()));
            if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton && stroke["wrinkleMethod"].toString()!="lighting-v2" && QRectF(source-QPointF(radius,radius),QSizeF(radius*2,radius*2)).contains(scene)){view_->setFocus();repair_source_grab_offset_=source_position(scene)-target-QPointF(stroke["offsetX"].toDouble(),stroke["offsetY"].toDouble());repair_source_dragging_=true;view_->viewport()->grabMouse();return true;}
            if(repair_source_dragging_){const auto offset=source_position(scene)-repair_source_grab_offset_-target;const QPointF delta=offset-QPointF(stroke["offsetX"].toDouble(),stroke["offsetY"].toDouble());if(stroke.contains("sourceOffsets")){auto offsets=stroke["sourceOffsets"].toArray();for(int i=0;i<offsets.size();++i){const auto a=offsets[i].toArray();offsets[i]=QJsonArray{a[0].toDouble()+delta.x(),a[1].toDouble()+delta.y()};}stroke["sourceOffsets"]=offsets;}stroke["offsetX"]=offset.x();stroke["offsetY"]=offset.y();if(stroke.contains("makeupPart")){auto validated=editing::portrait_makeup_repair(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]),stroke,true);validated["id"]=stroke["id"];validated["kind"]=stroke["kind"];stroke=validated;}repairs[repair_selected_]=stroke;settings["repairs"]=repairs;update_repair_overlay();schedule_preview();if(type==QEvent::MouseButtonRelease){repair_source_dragging_=false;view_->viewport()->releaseMouse();remember();}return true;}
        }
    }
    if((repair_mode_>0 || repair_selected_>=0 || range_mode_>0) && type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton && (mouse->modifiers().testFlag(Qt::AltModifier) || property("portraitConcealSampling").toBool())){
        setProperty("portraitConcealSampling",false);const auto p=source_position(scene);if(panel==7 && range_mode_>0 && range_key_->currentData().toString()=="repair.conceal"){const int x=std::min(source_.width()-1,int(p.x()*source_.width())),y=std::min(source_.height()-1,int(p.y()*source_.height()));const auto* pixel=reinterpret_cast<const float*>(source_.constScanLine(y))+x*4;if(pixel[3]>.00001F){people_[std::size_t(selected_)].settings["concealColor"]=QJsonArray{pixel[0]/pixel[3],pixel[1]/pixel[3],pixel[2]/pixel[3]};remember();build_parameters();status_->setText(tr("已取遮瑕颜色；涂选需要协调的范围"));}return true;}
        repair_source_=p;repair_has_source_=true;setProperty("portraitCloneOffsetValid",false);if(repair_selected_>=0){auto& s=people_[std::size_t(selected_)].settings;auto a=s["repairs"].toArray();if(repair_selected_<a.size()){auto object=a[repair_selected_].toObject();auto healing=object;healing.remove("kind");healing.remove("id");healing.remove("wrinkleMethod");healing.remove("makeupPart");healing.remove("validatedCandidateCoverage");healing.remove("hairSourceAngle");healing.remove("hairSourceScale");auto stroke=editing::healing_from_json(healing);const auto offset=p-stroke.points.front();for(auto& value:stroke.source_offsets)value+=offset-stroke.offset;stroke.offset=offset;auto updated=object.contains("makeupPart")?editing::portrait_makeup_repair(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]),editing::healing_json(stroke),true):editing::healing_json(stroke);updated["kind"]=object["kind"];updated["id"]=object["id"];for(auto key:{"hairSourceAngle","hairSourceScale"})if(object.contains(key))updated[key]=object[key];if(object.contains("validatedCandidateCoverage"))updated["validatedCandidateCoverage"]=object["validatedCandidateCoverage"];a[repair_selected_]=updated;s["repairs"]=a;remember();update_repair_overlay();}}update_hair_source_toolbar();status_->setText(tr("取样来源已设置"));return true;
    }
    if(panel==7 && range_mode_==1 && range_key_->currentData().toString()=="repair.conceal" && type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){
        auto& settings=people_[std::size_t(selected_)].settings;
        if(settings.value("concealColor").toArray().size()!=3){const auto color=editing::portrait_conceal_reference(source_,editing::portrait_person_json(people_[std::size_t(selected_)]),source_position(scene),brush_size_->value()*.5);
            if(color.size()!=3){status_->setText(tr("附近没有足够的可靠肤色，请 Alt 单击指定颜色"));return true;}settings["concealColor"]=color;if(auto* swatch=parameter_host_->findChild<QLabel*>("portraitConcealSwatch")){QImage image(1,1,QImage::Format_RGBA32FPx4_Premultiplied);image.setColorSpace(editing::working_space());auto* pixel=reinterpret_cast<float*>(image.bits());for(int c=0;c<3;++c)pixel[c]=float(color[c].toDouble());pixel[3]=1;swatch->setPixmap(QPixmap::fromImage(editing::display_image(image).scaled(48,20)));}status_->setText(tr("已建议邻近肤色；继续涂抹，Alt 可改色"));}
    }
    if(repair_mode_==0)return false;
    if(type==QEvent::MouseMove){QPainterPath path;path.addEllipse(scene,brush_size_->value()*.5*preview_.width()/source_.width(),brush_size_->value()*.5*preview_.width()/source_.width());if(repair_has_source_){const auto normalized=repair_mode_==5 && property("portraitCloneOffsetValid").toBool()?source_position(scene)+repair_offset_:repair_source_;const auto origin=repair_scene_position(normalized);const double r=5/view_->transform().m11();path.moveTo(origin-QPointF(r,0));path.lineTo(origin+QPointF(r,0));path.moveTo(origin-QPointF(0,r));path.lineTo(origin+QPointF(0,r));}overlay_->setPath(path);}
    if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){if((repair_mode_==4 || repair_mode_==5) && !repair_has_source_){status_->setText(tr("请先 Alt 单击指定取样来源"));return true;}view_->setFocus();view_->viewport()->grabMouse();repair_drawing_=true;repair_live_=QJsonObject{};repair_points_.clear();repair_selected_=-1;painting_mesh_=editing::portrait_deformation(source_.size(),recipe());}
    if(!repair_drawing_)return false;
    const auto p=source_position(scene);if(repair_points_.empty() || QLineF(repair_points_.back(),p).length()>.0002)repair_points_.push_back(p);
    editing::HealingStroke stroke;stroke.clone=repair_mode_==4 || repair_mode_==5;stroke.points=repair_points_;stroke.radius=brush_size_->value()*.5/std::min(source_.width(),source_.height());stroke.feather=brush_feather_->value()*.01;stroke.opacity=repair_opacity_;
    if((repair_mode_==1 || repair_mode_==6) && !repair_has_source_){
        const auto skin=editing::portrait_effect_mask(editing::portrait_person_json(people_[std::size_t(selected_)]),"skin",linear_preview_.size().scaled(768,768,Qt::KeepAspectRatio));
        try{stroke=editing::automatic_healing_path(linear_preview_,stroke,skin,true);}
        catch(const editing::EditError&){if(repair_mode_!=6)throw;
            // A narrow eye-corner path may have no smoothly drifting source.
            // Try one coherent source footprint for the complete stroke instead.
            stroke.offset=editing::automatic_healing_offset(linear_preview_,stroke,0,skin,nullptr,true,true);
        }repair_offset_=stroke.offset;
    }
    else if(repair_live_.isEmpty() && !(repair_mode_==5 && property("portraitCloneOffsetValid").toBool()))repair_offset_=repair_has_source_?repair_source_-stroke.points.front():(repair_mode_==2 || repair_mode_==3)?QPointF{}:editing::automatic_healing_offset(linear_preview_,stroke,0,editing::portrait_effect_mask(editing::portrait_person_json(people_[std::size_t(selected_)]),"skin",linear_preview_.size().scaled(768,768,Qt::KeepAspectRatio)));
    if(repair_mode_==5){
        const auto origin=stroke.points.back()+repair_offset_;
        if(origin.x()<0 || origin.y()<0 || origin.x()>=1 || origin.y()>=1)throw editing::EditError("取样来源超出图像，请 Alt 重新取样");
        const auto* pixel=reinterpret_cast<const float*>(source_.constScanLine(int(origin.y()*source_.height())))+int(origin.x()*source_.width())*4;
        if(pixel[3]<.001F)throw editing::EditError("取样来源透明，请 Alt 选择有内容的位置");
        setProperty("portraitCloneOffsetValid",true);
    }
    stroke.offset=repair_offset_;auto input=editing::healing_json(stroke);if(repair_mode_==3 && property("portraitWorkspaceTask").toInt()==5)input["requestedPart"]=QStringList{"lip","brow","shadow"}[std::clamp(property("portraitMakeupRepairPart").toInt(),0,2)];auto object=repair_mode_==3?editing::portrait_makeup_repair(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]),input,repair_has_source_):input;if(repair_mode_==3 && repair_live_.isEmpty() && !repair_has_source_)brush_size_->setValue(std::max(1,int(std::round(object["radius"].toDouble()*std::min(source_.width(),source_.height())*2))));object["id"]=repair_live_.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):repair_live_["id"].toString();object["kind"]=(repair_mode_==2 || repair_mode_==6)?"wrinkle":repair_mode_==3?"makeup":repair_mode_==4?"hairline":"heal";if(repair_mode_==2)object["wrinkleMethod"]="lighting-v2";if(repair_mode_==4){object["hairSourceAngle"]=property("portraitHairSourceAngle").toDouble();object["hairSourceScale"]=property("portraitHairSourceScale").isValid()?property("portraitHairSourceScale").toDouble()*.01:1.;}repair_live_=object;schedule_preview();
    if(type==QEvent::MouseButtonRelease){repair_drawing_=false;view_->viewport()->releaseMouse();auto& settings=people_[std::size_t(selected_)].settings;auto repairs=settings["repairs"].toArray();repairs.append(object);settings["repairs"]=repairs;repair_selected_=-1;repair_live_=QJsonObject{};remember();build_parameters();refresh_ranges();status_->setText(repair_mode_==2?tr("已应用明暗淡化，可继续沿暗纹涂抹"):tr("已应用笔画；可继续涂抹，Alt 单击为下一笔重新取样"));}
    return true;
}catch(const std::exception& error){if(repair_source_dragging_){repair_source_dragging_=false;remember();}repair_drawing_=false;repair_live_=QJsonObject{};view_->viewport()->releaseMouse();schedule_preview();status_->setText(QString::fromUtf8(error.what()));return true;}
}
