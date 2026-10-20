#include "portrait_dialog.h"
#include "work_window.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <QVBoxLayout>
#include <QToolButton>
#include <QPainter>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QSettings>
#include <QJsonDocument>
#include <QInputDialog>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>

namespace huelith::desktop {
void PortraitDialog::preview_body_combination(double strength,const QStringList& categories){
    if(selected_<0)return;
    const auto& baseline=preset_active_?preset_before_:people_;
    const auto& person=baseline[std::size_t(selected_)];
    if(person.unmatched || person.match_ambiguous)return;
    QJsonObject preset;
    const QJsonObject proposed{{"body.waist",15},{"body.legWidth.left",10},{"body.legWidth.right",10},{"head.size",-8}};
    const auto existing=person.settings["parameters"].toObject();QJsonObject accepted;QStringList skipped,preserved;
    for(auto i=proposed.begin();i!=proposed.end();++i){
        if(!categories.contains(i.key().section('.',0,0)))continue;
        if(existing.contains(i.key()) && existing[i.key()].toDouble()!=0){preserved.append(i.key());continue;}
        if(i.key()=="head.size" && existing["head.proportion"].toDouble()!=0){preserved.append(i.key());continue;}
        auto trial=accepted;trial[i.key()]=i.value().toDouble()*std::clamp(strength,0.,1.);auto trialPreset=preset;trialPreset["parameters"]=trial;
        if(i.key()=="body.waist")trialPreset["waistGeometryVersion"]=2;
        if(i.key()=="head.size")trialPreset["headGeometryVersion"]=2;
        if(i.key().startsWith("body.legWidth")){
            bool legacyEdited=false;
            if(person.settings["limbGeometryVersion"].toInt(1)<2)for(auto old=existing.begin();old!=existing.end();++old)
                if(old.value().toDouble()!=0 && (old.key().startsWith("body.arm") || old.key().startsWith("body.upperArm") || old.key().startsWith("body.legWidth") || old.key().startsWith("body.thigh") || old.key().startsWith("body.calf")))legacyEdited=true;
            if(legacyEdited){preserved.append(i.key());continue;}
            trialPreset["limbGeometryVersion"]=2;
        }
        auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);
        auto settings=editing::portrait_merge_preset(person.settings,trialPreset,1);
        if(trialPreset["headGeometryVersion"].toInt()>=2){auto masks=settings["automaticMasks"].toObject();if(!masks.contains("head"))masks["head"]=editing::portrait_mask_png(editing::portrait_head_region(candidate));settings["automaticMasks"]=masks;}
        candidate["settings"]=settings;
        try{editing::portrait_deformation(linear_preview_.size(),QJsonObject{{"version",1},{"people",QJsonArray{candidate}}});accepted=trial;preset=trialPreset;}
        catch(const std::exception&){skipped.append(i.key());}
    }
    if(accepted.isEmpty()){if(preset_active_)finish_preset(false);status_->setText(tr("没有可新增的美体调整：已有手调保留，不可见部位已跳过。"));return;}
    preset["parameters"]=accepted;preview_preset(preset,1);
    const QMap<QString,QString> labels{{"body.waist",tr("腰部")},{"body.legWidth.left",tr("人物左腿宽度")},{"body.legWidth.right",tr("人物右腿宽度")},{"head.size",tr("头身比例")}};
    QStringList detail;
    for(auto i=accepted.begin();i!=accepted.end();++i)detail.append(tr("预览：%1 %2").arg(labels.value(i.key(),i.key())).arg(i.value().toDouble()));
    for(const auto& key:preserved)detail.append(tr("保留已有手调：%1").arg(labels.value(key,key)));
    for(const auto& key:skipped)detail.append(tr("当前范围不可用，已跳过：%1").arg(labels.value(key,key)));
    status_->setToolTip(detail.join('\n'));
    auto* summary=preset_bar_->findChild<QLabel*>("portraitBodyCombinationSummary");
    if(!summary){summary=new QLabel(preset_bar_);summary->setObjectName("portraitBodyCombinationSummary");preset_bar_->layout()->addWidget(summary);}
    summary->setText(tr("组合明细（悬停查看）"));summary->setToolTip(detail.join('\n'));summary->show();
    status_->setText(tr("美体组合预览：%1 项；保留 %2 项手调，跳过 %3 项不可用部位。可取消预览或应用后逐项调整。")
        .arg(accepted.size()).arg(preserved.size()).arg(skipped.size()));
}
void PortraitDialog::preview_preset(const QJsonObject& preset,double strength) {
    if(selected_<0 || people_[std::size_t(selected_)].unmatched || people_[std::size_t(selected_)].match_ambiguous)return;
    if(auto* summary=preset_bar_->findChild<QLabel*>("portraitBodyCombinationSummary"))summary->hide();
    try{if(!preset_active_)preset_before_=people_;else people_=preset_before_;preset_active_=true;
        people_[std::size_t(selected_)].settings=editing::portrait_merge_preset(people_[std::size_t(selected_)].settings,preset,strength);
        auto& person=people_[std::size_t(selected_)];auto masks=person.settings["automaticMasks"].toObject();
        if(person.settings["limbGeometryVersion"].toInt(1)>=2)for(int side=0;side<2;++side){
            if(person.settings["parameters"].toObject()[side==0?"body.arm.left":"body.arm.right"].toDouble()==0)continue;
            auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);
            const auto reason=editing::portrait_arm_unavailable(candidate,linear_preview_.size(),side);
            if(!reason.isEmpty())throw editing::EditError(reason);
        }
        if(person.settings["heightVersion"].toInt(1)>=2 && person.settings["parameters"].toObject()["body.height"].toDouble()!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_height_unavailable(candidate,linear_preview_.size(),person.settings["parameters"].toObject()["body.height"].toDouble()*.01);if(!reason.isEmpty())throw editing::EditError(reason);}
        if(person.settings["neckGeometryVersion"].toInt(1)>=2 && person.settings["parameters"].toObject()["body.neck"].toDouble()!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_neck_unavailable(candidate,linear_preview_.size());if(!reason.isEmpty())throw editing::EditError(reason);}
        if(person.settings["legLengthVersion"].toInt(1)>=2)for(int side=0;side<2;++side){const double amount=person.settings["parameters"].toObject()[side==0?"body.legLength.left":"body.legLength.right"].toDouble()*.01;if(amount!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_leg_length_unavailable(candidate,linear_preview_.size(),side,amount,std::abs(person.settings["parameters"].toObject()["body.legLength.left"].toDouble()-person.settings["parameters"].toObject()["body.legLength.right"].toDouble())<1e-9);if(!reason.isEmpty())throw editing::EditError(reason);}}
        for(int side=0;side<2;++side)if(person.settings["parameters"].toObject()[side==0?"body.straightLeg.left":"body.straightLeg.right"].toDouble()!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_straight_leg_unavailable(candidate,linear_preview_.size(),side);if(!reason.isEmpty())throw editing::EditError(reason);}
        if(person.settings["parameters"].toObject()["body.back"].toDouble()!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_back_unavailable(candidate,linear_preview_.size());if(!reason.isEmpty())throw editing::EditError(reason);}
        if(person.settings["parameters"].toObject()["body.belly"].toDouble()!=0 && person.settings.value("abdomenFront").toArray().size()!=2)throw editing::EditError("此预设包含局部瘦肚子，请先在目标照片的身材页定位腹部前缘，再应用预设");
        if(person.settings["parameters"].toObject()["body.abdomen"].toDouble()!=0 || person.settings["parameters"].toObject()["body.belly"].toDouble()!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_abdomen_unavailable(candidate,linear_preview_.size(),person.settings["parameters"].toObject()["body.abdomen"].toDouble()==0);if(!reason.isEmpty())throw editing::EditError(reason);}

        if((person.settings["eyeGeometryVersion"].toInt(1)>=2 || person.settings["browGeometryVersion"].toInt(1)>=2 || person.settings["proportionGeometryVersion"].toInt(1)>=2) && !masks.contains("eyeglassFrame")){
            masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));
            person.settings["automaticMasks"]=masks;
        }
        if((person.settings["headGeometryVersion"].toInt(1)>=2 || person.settings["scalpGeometryVersion"].toInt(1)>=2) && !masks.contains("head")){
            masks["head"]=editing::portrait_mask_png(editing::portrait_head_region(editing::portrait_person_json(person)));
            person.settings["automaticMasks"]=masks;
        }
        if((person.settings["scalpGeometryVersion"].toInt(1)>=2 || person.settings["parameters"].toObject()["hair.roots"].toDouble()!=0) && !person.settings.contains("scalpGeometry"))
            person.settings["scalpGeometry"]=editing::portrait_scalp_geometry(editing::portrait_person_json(person));
        if(person.settings["parameters"].toObject()["face.tearFill.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.tearFill.right"].toDouble()!=0)person.settings["tearVolume"]=editing::portrait_tear_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.malarTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.malarTone.right"].toDouble()!=0)person.settings["malarVolume"]=editing::portrait_malar_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.cheekTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.cheekTone.right"].toDouble()!=0)person.settings["cheekVolume"]=editing::portrait_cheek_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.orbitalTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.orbitalTone.right"].toDouble()!=0)person.settings["orbitalVolume"]=editing::portrait_orbital_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.perioralTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.perioralTone.right"].toDouble()!=0)person.settings["perioralVolume"]=editing::portrait_perioral_volume(linear_preview_,editing::portrait_person_json(person));
        if(person.settings["parameters"].toObject()["face.foreheadFill"].toDouble()!=0)person.settings["foreheadVolume"]=editing::portrait_forehead_volume(linear_preview_,editing::portrait_person_json(person));
        if(person.settings["contourAutoLighting"].toBool())person.settings["contourLighting"]=editing::portrait_contour_lighting(linear_preview_,editing::portrait_person_json(person));
        editing::portrait_deformation(linear_preview_.size(),recipe(),1,{},nullptr,true);
        schedule_preview();
        const auto settings=people_[std::size_t(selected_)].settings;bool needs_regions=false;const auto parameters=settings["parameters"].toObject();for(auto i=parameters.begin();i!=parameters.end();++i)if((i.key().startsWith("eye.") || i.key().startsWith("teeth.")) && i.value().toDouble()!=0)needs_regions=true;
        bool needs_materials=settings["hairEdgeVersion"].toInt()>=1 && settings["hairLighting"].toObject()["edgeVersion"].toInt()<2;for(auto i=parameters.begin();i!=parameters.end();++i){if(i.value().toDouble()==0)continue;if((i.key().startsWith("makeup.iris") || i.key().startsWith("makeup.catch")) && !settings.contains("irisMaterials"))needs_materials=true;if(i.key().startsWith("makeup.brow") && !settings.contains("browMaterials"))needs_materials=true;if((i.key().startsWith("makeup.blushGloss") || i.key().startsWith("makeup.blushSparkle")) && !settings.contains("cheekLighting"))needs_materials=true;if(i.key().startsWith("hair.") && (i.key()=="hair.oil" || settings["hairColorVersion"].toInt()>=2 || settings["hairShineVersion"].toInt()>=2) && (!settings.contains("hairLighting") || (i.key()=="hair.oil" && settings["hairLighting"].toObject()["oilVersion"].toInt()<1)))needs_materials=true;if(i.key()=="makeup.lipGloss" && settings["lipFinishVersion"].toInt()>=2 && !settings.contains("lipLighting"))needs_materials=true;}
        if(needs_materials || (needs_regions && !settings["automaticMasks"].toObject().contains("teeth")))analyze_repairs();
        status_->setText(tr("组合已预览；可继续分项调整，最后统一确定"));
    }catch(const std::exception& e){finish_preset(false);status_->setText(QString::fromUtf8(e.what()));}
}
void PortraitDialog::finish_preset(bool apply){
    if(!preset_active_ || (apply && repair_analysis_busy_))return;preset_active_=false;if(!apply && repair_analysis_busy_)cancelled_=true;
    if(!apply)people_=preset_before_;preset_before_.clear();preset_bar_->hide();++preview_epoch_;
    if(apply)remember();else schedule_preview();build_parameters();refresh_ranges();
}

void PortraitDialog::build_presets() {
    auto* description=new QLabel(tr("点选组合即可预览，再调强度。切换其他功能后可继续微调。"),parameter_host_);description->setWordWrap(true);parameter_layout_->addWidget(description);
    auto* list=new QComboBox(parameter_host_);list->setObjectName("portraitPresets");
    const std::vector<std::pair<QString,QJsonObject>> builtins{
        {tr("自然人像"),{{"parameters",QJsonObject{{"skin.smooth",20},{"skin.texture",10},{"color.redness",15},{"light.face",10}}}}},
        {tr("一键美体 · 轻微"),{{"adaptiveBody",true}}},
        {tr("自然整妆"),editing::portrait_makeup_combination(0)},
        {tr("暗光人像补光"),{{"parameters",QJsonObject{{"light.face",40},{"light.body",20},{"light.highlights",90}}}}},
        {tr("逆光人像补光"),{{"parameters",QJsonObject{{"light.face",60},{"light.body",10},{"light.highlights",100}}}}}
    };
    for(const auto& item:builtins){auto current=item.second;current["mouthTopologyVersion"]=2;list->addItem(item.first,current.toVariantMap());}
    if(property("incomingPreset").isValid())list->addItem(tr("来自源照片的设置"),property("incomingPreset"));
    const auto saved=QJsonDocument::fromJson(QSettings().value("portrait/presets").toByteArray()).array();for(const auto& v:saved){const auto p=v.toObject();list->addItem(p["name"].toString(),p["settings"].toObject().toVariantMap());}list->hide();
    auto* cards=new QListWidget(parameter_host_);cards->setObjectName("portraitPresetCards");cards->setViewMode(QListView::IconMode);cards->setResizeMode(QListView::Adjust);cards->setMovement(QListView::Static);cards->setIconSize({80,62});cards->setGridSize({120,100});cards->setFixedHeight(220);
    for(int i=0;i<list->count();++i){QString unavailable;QImage thumbnail_image=preview_.scaled(80,62,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        try{if(selected_>=0 && !linear_preview_.isNull()){auto candidate=recipe();auto people=candidate["people"].toArray();auto person=people[selected_].toObject();person["settings"]=editing::portrait_merge_preset(person["settings"].toObject(),QJsonObject::fromVariantMap(list->itemData(i).toMap()),1);auto derived=person["settings"].toObject();if(derived["parameters"].toObject()["face.tearFill.left"].toDouble()!=0 || derived["parameters"].toObject()["face.tearFill.right"].toDouble()!=0){derived["tearVolume"]=editing::portrait_tear_volume(linear_preview_,person);person["settings"]=derived;}if(derived["parameters"].toObject()["face.malarTone.left"].toDouble()!=0 || derived["parameters"].toObject()["face.malarTone.right"].toDouble()!=0){derived["malarVolume"]=editing::portrait_malar_volume(linear_preview_,person);person["settings"]=derived;}if(derived["parameters"].toObject()["face.cheekTone.left"].toDouble()!=0 || derived["parameters"].toObject()["face.cheekTone.right"].toDouble()!=0){derived["cheekVolume"]=editing::portrait_cheek_volume(linear_preview_,person);person["settings"]=derived;}if(derived["parameters"].toObject()["face.orbitalTone.left"].toDouble()!=0 || derived["parameters"].toObject()["face.orbitalTone.right"].toDouble()!=0){derived["orbitalVolume"]=editing::portrait_orbital_volume(linear_preview_,person);person["settings"]=derived;}if(derived["parameters"].toObject()["face.perioralTone.left"].toDouble()!=0 || derived["parameters"].toObject()["face.perioralTone.right"].toDouble()!=0){derived["perioralVolume"]=editing::portrait_perioral_volume(linear_preview_,person);person["settings"]=derived;}if(derived["parameters"].toObject()["face.foreheadFill"].toDouble()!=0){derived["foreheadVolume"]=editing::portrait_forehead_volume(linear_preview_,person);person["settings"]=derived;}if(derived["contourAutoLighting"].toBool()){derived["contourLighting"]=editing::portrait_contour_lighting(linear_preview_,person);person["settings"]=derived;}people[selected_]=person;candidate["people"]=people;editing::Effect effect;effect.portrait=candidate;thumbnail_image=editing::display_image(editing::apply_effects(linear_preview_.scaled(80,62,Qt::KeepAspectRatio,Qt::SmoothTransformation),{effect}));}}catch(const std::exception& e){unavailable=QString::fromUtf8(e.what());}
        QPixmap thumbnail=QPixmap::fromImage(thumbnail_image);auto* item=new QListWidgetItem(QIcon(thumbnail),list->itemText(i),cards);item->setToolTip(unavailable.isEmpty()?list->itemText(i):list->itemText(i)+tr("\n此图暂不能预览：")+unavailable);}
    cards->setCurrentRow(property("presetIndex").isValid()?property("presetIndex").toInt():-1);list->setCurrentIndex(std::max(0,cards->currentRow()));parameter_layout_->addWidget(cards);
    auto* categories_host=new QWidget(parameter_host_);auto* categories_layout=new QVBoxLayout(categories_host);categories_layout->setContentsMargins(0,0,0,0);

    QStringList categories{"face","feature","head","body","skin","color","eye","teeth","makeup","hair","contour","light"};QStringList labels{tr("面部"),tr("五官"),tr("头部"),tr("身材"),tr("肤质"),tr("肤色"),tr("眼周"),tr("牙齿"),tr("美妆"),tr("头发"),tr("修容"),tr("补光")};std::vector<QCheckBox*> checks;
    for(int i=0;i<categories.size();++i){auto* c=new QCheckBox(labels[i],parameter_host_);c->setObjectName("portraitPresetCategory_"+categories[i]);c->setChecked(true);categories_layout->addWidget(c);checks.push_back(c);}
    auto selected_categories=[checks,categories]{QStringList result;for(std::size_t i=0;i<checks.size();++i)if(checks[i]->isChecked())result<<categories[int(i)];return result;};
    auto* strength=new QSlider(Qt::Horizontal,parameter_host_);strength->setObjectName("portraitPresetStrength");strength->setRange(0,100);strength->setValue(property("presetStrength").isValid()?property("presetStrength").toInt():100);parameter_layout_->addWidget(new QLabel(tr("组合强度"),parameter_host_));parameter_layout_->addWidget(strength);
    auto button=[&](QString text,QString name,auto fn){auto* b=new QPushButton(text,parameter_host_);b->setObjectName(name);b->setEnabled(selected_>=0);parameter_layout_->addWidget(b);connect(b,&QPushButton::clicked,this,fn);};
    button(tr("预览所选组合…"),"portraitPreviewPreset",[this,list,strength,selected_categories]{const auto chosen=QJsonObject::fromVariantMap(list->currentData().toMap());if(chosen["adaptiveBody"].toBool())preview_body_combination(strength->value()*.01,selected_categories());else preview_preset(editing::portrait_portable_settings(chosen,selected_categories()),strength->value()*.01);});
    auto apply_selection=[this,list,strength,selected_categories]{setProperty("presetIndex",list->currentIndex());setProperty("presetStrength",strength->value());const auto chosen=QJsonObject::fromVariantMap(list->currentData().toMap());if(chosen["adaptiveBody"].toBool())preview_body_combination(strength->value()*.01,selected_categories());else preview_preset(editing::portrait_portable_settings(chosen,selected_categories()),strength->value()*.01);};
    connect(cards,&QListWidget::currentRowChanged,this,[list,apply_selection](int i){list->setCurrentIndex(i);apply_selection();});
    connect(strength,&QSlider::valueChanged,this,[apply_selection](int){apply_selection();});
    if(auto* b=findChild<QPushButton*>("portraitPreviewPreset"))b->hide();
    auto* advanced=new QToolButton(parameter_host_);advanced->setText(tr("应用类别与跨图同步"));advanced->setCheckable(true);advanced->setArrowType(Qt::RightArrow);advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(advanced);parameter_layout_->addWidget(categories_host);categories_host->hide();connect(advanced,&QToolButton::toggled,this,[categories_host,advanced](bool open){categories_host->setVisible(open);advanced->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    button(tr("保存当前人物为个人预设…"),"portraitSavePreset",[this,selected_categories]{if(selected_<0)return;bool ok=false;const auto name=QInputDialog::getText(this,tr("保存预设"),tr("名称"),QLineEdit::Normal,{},&ok);if(!ok || name.trimmed().isEmpty())return;QSettings store;auto presets=QJsonDocument::fromJson(store.value("portrait/presets").toByteArray()).array();presets.append(QJsonObject{{"name",name.left(80)},{"settings",editing::portrait_portable_settings(people_[std::size_t(selected_)].settings,selected_categories())}});while(presets.size()>100)presets.removeFirst();store.setValue("portrait/presets",QJsonDocument(presets).toJson(QJsonDocument::Compact));build_parameters();});
    button(tr("删除所选个人预设"),"portraitDeletePreset",[this,list,builtins]{const int i=list->currentIndex()-int(builtins.size())-(property("incomingPreset").isValid()?1:0);if(i<0)return;QSettings store;auto presets=QJsonDocument::fromJson(store.value("portrait/presets").toByteArray()).array();if(i<presets.size())presets.removeAt(i);store.setValue("portrait/presets",QJsonDocument(presets).toJson(QJsonDocument::Compact));build_parameters();});
    button(tr("跨图同步当前人物设置…"),"portraitSync",[this,selected_categories]{if(selected_<0)return;const auto paths=QFileDialog::getOpenFileNames(this,tr("选择要同步的照片"),{},tr("照片 (*.png *.jpg *.jpeg *.tif *.tiff)"));const auto preset=editing::portrait_portable_settings(people_[std::size_t(selected_)].settings,selected_categories());
        for(const auto& path:paths){try{QFile file(path);if(!file.open(QIODevice::ReadOnly))throw editing::EditError("无法读取同步照片");QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);const auto source_hash=hash.result().toHex();PortraitDialog target(editing::import_image(path),{},this);target.setWindowTitle(tr("同步目标 · %1 · 请选择人物并预览应用").arg(QFileInfo(path).fileName()));target.setProperty("incomingPreset",preset.toVariantMap());target.navigation_->setCurrentRow(15);if(run_work_window(target,this)!=QDialog::Accepted)continue;const auto result=target.recipe();bool edited=false;for(const auto& v:result["people"].toArray())if(!v.toObject()["settings"].toObject()["parameters"].toObject().isEmpty())edited=true;if(!edited){status_->setText(tr("目标未应用设置，已跳过"));continue;}sync_targets_.append(QJsonObject{{"path",path},{"recipe",result},{"fileHash",QString::fromLatin1(source_hash)}});setProperty("syncTargets",sync_targets_);}catch(const std::exception& e){status_->setText(tr("同步目标失败：%1").arg(QString::fromUtf8(e.what())));}}
        build_parameters();});
    auto* queued=new QLabel(tr("待提交同步照片：%1。外层确定后加入主窗口；取消不会改目标照片。").arg(sync_targets_.size()),parameter_host_);queued->setWordWrap(true);parameter_layout_->addWidget(queued);
    button(tr("清空待提交同步"),"portraitClearSync",[this]{sync_targets_={};setProperty("syncTargets",sync_targets_);build_parameters();});
}
}
