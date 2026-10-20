#include <QWheelEvent>
#include "huelith/editing/healing.h"
#include "portrait_dialog.h"
#include "raw_preview_view.h"
#include "main_window.h"
#include "canvas_view.h"
#include "huelith/editing/project.h"
#include "huelith/editing/portrait_effects.h"
#include <QApplication>
#include <QFontDatabase>
#include <QDir>
#include <QJsonArray>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QAction>
#include <QAbstractButton>
#include <QToolButton>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QGraphicsPathItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QGraphicsView>
#include <QTabWidget>
#include <QTabBar>
#include <QSlider>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QInputDialog>
#include <QTimer>
#include <QLineEdit>
QJsonObject portrait_ui_command(const QJsonObject& o) {
    if(o.contains("paletteEntries"))QSettings().setValue("portrait/colors",QJsonDocument(o["paletteEntries"].toArray()).toJson(QJsonDocument::Compact));
    static QPointer<huelith::desktop::PortraitDialog> window;
    const auto op=o["op"].toString();
    if(op=="portrait_geometry") {
        if(o.contains("tearVolume"))return huelith::editing::portrait_tear_volume(huelith::editing::import_image(o["source"].toString()),o["person"].toObject());if(o.contains("malarVolume"))return huelith::editing::portrait_malar_volume(huelith::editing::import_image(o["source"].toString()),o["person"].toObject());if(o.contains("cheekVolume"))return huelith::editing::portrait_cheek_volume(huelith::editing::import_image(o["source"].toString()),o["person"].toObject());if(o.contains("orbitalVolume"))return huelith::editing::portrait_orbital_volume(huelith::editing::import_image(o["source"].toString()),o["person"].toObject());if(o.contains("perioralVolume"))return huelith::editing::portrait_perioral_volume(huelith::editing::import_image(o["source"].toString()),o["person"].toObject());
        if(o.contains("foreheadVolume"))return huelith::editing::portrait_forehead_volume(huelith::editing::import_image(o["source"].toString()),o["person"].toObject());
        if(o.contains("contourLighting"))return huelith::editing::portrait_contour_lighting(huelith::editing::import_image(o["source"].toString()),o["person"].toObject());
        if(o.contains("styleOnly")){auto source=huelith::editing::import_image(o["source"].toString());const auto result=huelith::editing::portrait_style_region(source,source.size(),o["recipe"].toObject(),source.rect(),1);huelith::editing::display_image(result).save(o["styleOnly"].toString());return {{"ok",true}};}
        if(o.contains("blemishModel")){auto evidence=huelith::editing::portrait_blemish_evidence(huelith::editing::import_image(o["source"].toString()),o["person"].toObject(),o["blemishModel"].toString());if(!evidence.save(o["mask"].toString()))throw huelith::editing::EditError("evidence output failed");return {{"ok",true}};}
        if(o.contains("nailWorker")){return huelith::editing::run_ai_job(QJsonObject{{"operation","portrait-nails"},{"model",o["model"]}},huelith::editing::import_image(o["source"].toString())).metadata;}
        if(o.contains("repairWorker")){const auto result=huelith::editing::run_ai_job(QJsonObject{{"operation","portrait-repair"},{"directml",false},{"person",o["person"]},{"model",o["model"]}},huelith::editing::import_image(o["source"].toString()));return result.metadata;}
        if(o.contains("repairAnalysis"))return huelith::editing::portrait_repair_analysis(huelith::editing::import_image(o["source"].toString()),o["person"].toObject(),o.contains("blemishEvidence")?QImage(o["blemishEvidence"].toString()):QImage{});
        if(o.contains("portableSkin")){const auto portable=huelith::editing::portrait_portable_settings(o["portableSkin"].toObject(),o["bodyPreset"].toBool()?QStringList{"body"}:o["facePreset"].toBool()?QStringList{"face"}:o["contourPreset"].toBool()?QStringList{"contour"}:o["hairPreset"].toBool()?QStringList{"hair"}:o["makeupPreset"].toBool()?QStringList{"makeup"}:QStringList{"skin"});return {{"portable",portable},{"merged",huelith::editing::portrait_merge_preset(o["targetSettings"].toObject(),portable,1)}};}
        if(o.contains("makeupRepair")){const auto source=huelith::editing::import_image(o["source"].toString());return {{"stroke",huelith::editing::portrait_makeup_repair(source,o["person"].toObject(),o["makeupRepair"].toObject(),o["manualSource"].toBool())}};}
        if(o.contains("makeupRepairMask")){huelith::editing::portrait_makeup_repair_mask(o["person"].toObject(),o["makeupRepairMask"].toString(),QSize(o["width"].toInt(),o["height"].toInt())).save(o["output"].toString());return {{"ok",true}};}
        if(o.contains("healingPath")){const auto image=huelith::editing::import_image(o["source"].toString());const auto stroke=huelith::editing::healing_from_json(o["healingPath"].toObject());const auto matched=huelith::editing::automatic_healing_path(image,stroke,QImage(o["sourceMask"].toString()).convertToFormat(QImage::Format_Grayscale8));const auto saved=huelith::editing::healing_json(matched);if(o.contains("render"))huelith::editing::display_image(huelith::editing::apply_healing(image,QJsonArray{saved})).save(o["render"].toString());return {{"stroke",saved}};}
        if(o.contains("healingOffset")){const auto image=huelith::editing::import_image(o["source"].toString());const auto stroke=huelith::editing::healing_from_json(o["healingOffset"].toObject());const auto offset=huelith::editing::automatic_healing_offset(image,stroke,0,QImage(o["sourceMask"].toString()).convertToFormat(QImage::Format_Grayscale8));return {{"offset",QJsonArray{offset.x(),offset.y()}}};}

        if(o.contains("sampleColor")){const auto point=o["sampleColor"].toArray();return {{"color",huelith::editing::portrait_sample_skin_color(huelith::editing::import_image(o["source"].toString()),{point[0].toInt(),point[1].toInt()})}};}
        if(o.contains("styleMask")){
            const auto dimensions=o["size"].toArray();const auto person=o["recipe"].toObject()["people"].toArray()[0].toObject();
            const auto mask=huelith::editing::portrait_effect_mask(person,o["styleMask"].toString(),{dimensions[0].toInt(),dimensions[1].toInt()});
            if(!mask.save(o["mask"].toString()))throw huelith::editing::EditError("mask output failed");return {{"ok",true}};
        }
        if(o["headAnalysis"].toBool()){
            auto recipe=o["recipe"].toObject();auto people=recipe["people"].toArray();
            for(int i=0;i<people.size();++i){auto person=people[i].toObject();auto settings=person["settings"].toObject();auto masks=settings["automaticMasks"].toObject();const auto head=huelith::editing::portrait_head_region(person);settings["scalpGeometry"]=huelith::editing::portrait_scalp_geometry(person);masks["head"]=huelith::editing::portrait_mask_png(head);settings["automaticMasks"]=masks;person["settings"]=settings;people[i]=person;if(o.contains("mask"))head.save(o["mask"].toString());}
            recipe["people"]=people;return {{"recipe",recipe}};
        }
        if(o["frameAnalysis"].toBool()){
            auto recipe=o["recipe"].toObject();auto people=recipe["people"].toArray();const auto source=huelith::editing::import_image(o["source"].toString());
            for(int i=0;i<people.size();++i){auto person=people[i].toObject();auto settings=person["settings"].toObject();auto masks=settings["automaticMasks"].toObject();const auto frame=huelith::editing::portrait_frame_protection(source,person);masks["eyeglassFrame"]=huelith::editing::portrait_mask_png(frame);settings["automaticMasks"]=masks;person["settings"]=settings;people[i]=person;if(o.contains("mask"))frame.save(o["mask"].toString());}
            recipe["people"]=people;return {{"recipe",recipe}};
        }
        const auto dimensions=o["size"].toArray();
        QElapsedTimer timer;timer.start();
        const auto mesh=huelith::editing::portrait_deformation({dimensions[0].toInt(),dimensions[1].toInt()},o["recipe"].toObject(),o["amount"].toDouble(1));
        QJsonObject result{{"milliseconds",timer.nsecsElapsed()/1e6},{"meshSize",QJsonArray{mesh.width(),mesh.height()}},{"identity",mesh.isNull()}};
        if(o.contains("field") && !mesh.isNull()) {QFile file(o["field"].toString());if(!file.open(QIODevice::WriteOnly))throw huelith::editing::EditError("field output failed");for(int y=0;y<mesh.height();++y)file.write(reinterpret_cast<const char*>(mesh.constScanLine(y)),mesh.width()*4*sizeof(float));}
        if(o.contains("render")){const auto source=huelith::editing::import_image(o["source"].toString());
            const auto rendered=huelith::editing::apply_portrait(source,o["recipe"].toObject(),o["amount"].toDouble(1));
            huelith::editing::display_image(rendered).save(o["render"].toString());
            if(o.contains("linearRender")){QFile file(o["linearRender"].toString());if(!file.open(QIODevice::WriteOnly))throw huelith::editing::EditError("linear output failed");for(int y=0;y<rendered.height();++y)file.write(reinterpret_cast<const char*>(rendered.constScanLine(y)),rendered.width()*4*sizeof(float));}
            if(o["colorMetrics"].toBool()){
                double maximum=0,total=0;int changed=0;
                for(int y=0;y<source.height();++y){const auto* a=reinterpret_cast<const float*>(source.constScanLine(y));const auto* b=reinterpret_cast<const float*>(rendered.constScanLine(y));
                    for(int x=0;x<source.width();++x){const int i=x*4;
                        const double delta=.2627*(b[i]-a[i])+.6780*(b[i+1]-a[i+1])+.0593*(b[i+2]-a[i+2]);maximum=std::max(maximum,std::abs(delta));
                        const double error=std::abs(b[i]-a[i])+std::abs(b[i+1]-a[i+1])+std::abs(b[i+2]-a[i+2]);if(error>1e-6){++changed;total+=error;}
                    }
                }result["maximumLuminanceError"]=maximum;result["changedPixels"]=changed;result["totalColorChange"]=total;
            }
        }
        return result;
    }
    if(op=="portrait_close"){delete window;return {{"ok",true}};}
    if(op=="portrait_window") {
        // The offscreen Windows backend has no platform font discovery.
        if(QGuiApplication::platformName()=="offscreen"){
            static const int font=QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");Q_UNUSED(font);
        }
        delete window;
        if(o.contains("project")) {
            const auto saved=huelith::editing::load_project(o["project"].toString());
            window=new huelith::desktop::PortraitDialog(saved.layers.at(0).pixels,saved.layers.at(0).effects.at(0).portrait);
        } else window=new huelith::desktop::PortraitDialog(huelith::editing::import_image(o["path"].toString()),o["recipe"].toObject());
        window->setProperty("modelDirectory",o["models"].toString());window->show();
    }
    if(!window)throw huelith::editing::EditError("portrait window missing");
    if(o.contains("workspaceTask"))window->findChild<QListWidget*>("portraitTasks")->setCurrentRow(o["workspaceTask"].toInt());
    if(o.contains("resize")){const auto size=o["resize"].toArray();window->resize(size[0].toInt(),size[1].toInt());}
    if(o.contains("comparePressed")){auto* b=window->findChild<QToolButton*>("portraitCompare");QMetaObject::invokeMethod(b,o["comparePressed"].toBool()?"pressed":"released",Qt::DirectConnection);}
    if(o.contains("dragParameter")){
        auto* slider=window->findChild<QSlider*>("portraitSlider_"+o["dragParameter"].toString());
        if(!slider)throw huelith::editing::EditError("portrait slider missing");
        const auto values=o["dragValues"].toArray();
        QJsonArray frames;slider->setSliderDown(true);
        QElapsedTimer elapsed;elapsed.start();qint64 last_capture=-1;
        for(const auto& value:values){slider->setValue(value.toInt());
            QElapsedTimer step;step.start();while(step.elapsed()<o["stepMs"].toInt(30))QApplication::processEvents(QEventLoop::AllEvents,5);
            const auto state=window->state();

            if(o.contains("dragCaptureDirectory") && state["previewFrames"].toInteger()!=last_capture){last_capture=state["previewFrames"].toInteger();QDir dir(o["dragCaptureDirectory"].toString());dir.mkpath(".");window->grab().scaledToWidth(1280,Qt::SmoothTransformation).save(dir.filePath(QString("frame-%1.png").arg(elapsed.elapsed(),6,10,QChar('0'))));}
            auto* view=window->findChild<QGraphicsView*>("portraitView");const auto box=QRect(view->viewport()->mapTo(window,QPoint{}),view->viewport()->size());
            frames.append(QJsonObject{{"ms",elapsed.elapsed()},{"frames",state["previewFrames"]},{"history",state["history"]},{"revision",state["displayedRevision"]},{"viewport",QJsonArray{box.x(),box.y(),box.width(),box.height()}},{"zoom",view->transform().m11()}});
        }
        if(o.contains("dragCapture"))window->grab().save(o["dragCapture"].toString());
        slider->setSliderDown(false);window->setProperty("dragFrames",frames);
    }
    if(o.contains("select"))window->findChild<QListWidget*>("portraitPeople")->setCurrentRow(o["select"].toInt());
    if(o.contains("focus"))window->focus_person(o["focus"].toInt());
    if(o.contains("linked"))window->findChild<QCheckBox*>("portraitLinked")->setChecked(o["linked"].toBool());
    if(o.contains("side"))window->findChild<QComboBox*>("portraitSide")->setCurrentIndex(o["side"].toInt());
    if(o.contains("confirmPrevious"))window->confirm_person(o["confirmPrevious"].toInt());
    if(o.contains("panel"))window->findChild<QListWidget*>("portraitNavigation")->setCurrentRow(o["panel"].toInt());
    if(o.contains("headMode"))window->findChild<QTabBar*>("portraitHeadTabs")->setCurrentIndex(o["headMode"].toInt());
    if(o.contains("volumeMode"))if(auto* tabs=window->findChild<QTabBar*>("portraitVolumeTabs"))tabs->setCurrentIndex(o["volumeMode"].toInt());
    if(o.contains("slimMode") || (o.contains("panel") && o["panel"].toInt()==4))if(auto* tabs=window->findChild<QTabBar*>("portraitSlimTabs"))tabs->setCurrentIndex(o.contains("slimMode")?o["slimMode"].toInt():1);
    if(o.contains("blemishPage"))window->findChild<QTabBar*>("portraitBlemishTabs")->setCurrentIndex(o["blemishPage"].toInt());
    if(o.contains("blemishTool"))window->findChild<QTabBar*>("portraitBlemishTools")->setCurrentIndex(o["blemishTool"].toInt());
    if(o.contains("wrinkleMode"))window->findChild<QTabBar*>("portraitWrinkleTabs")->setCurrentIndex(o["wrinkleMode"].toInt());
    if(o.contains("eraseMode"))window->findChild<QTabBar*>("portraitEraseTabs")->setCurrentIndex(o["eraseMode"].toInt());
    for(const auto& entry:QList<QPair<QString,QString>>{{"bodyMode","portraitBodyTabs"},{"bodyTool","portraitBodyManualTabs"},{"bodySide","portraitBodySide"}})if(o.contains(entry.first)){auto* tabs=window->findChild<QTabBar*>(entry.second);if(!tabs)throw huelith::editing::EditError("body tabs missing");tabs->setCurrentIndex(o[entry.first].toInt());}
    for(const auto& entry:QList<QPair<QString,QString>>{{"makeupPage","portraitMakeupPages"},{"paintType","portraitPaintTabs"},{"paintLightMode","portraitPaintLightTabs"},{"hairColorFamily","portraitHairColorFamily"},{"hairPage","portraitHairTabs"},{"hairDyeMode","portraitHairDyeTabs"}})if(o.contains(entry.first)){auto* tabs=window->findChild<QTabBar*>(entry.second);if(!tabs)throw huelith::editing::EditError("hair tabs missing");tabs->setCurrentIndex(o[entry.first].toInt());}
    if(o.contains("oilMode"))window->findChild<QTabBar*>("portraitOilTabs")->setCurrentIndex(o["oilMode"].toInt());
    if(o.contains("skinMode"))window->findChild<QTabBar*>("portraitSkinTabs")->setCurrentIndex(o["skinMode"].toInt());
    if(o.contains("smoothTarget"))window->findChild<QTabBar*>("portraitSmoothTarget")->setCurrentIndex(o["smoothTarget"].toInt());
    if(o.contains("smoothMode"))window->findChild<QTabBar*>("portraitSmoothMode")->setCurrentIndex(o["smoothMode"].toInt());
    if(o.contains("styleTab")){auto* tabs=window->findChild<QTabBar*>("portraitStyleTabs");if(!tabs)throw huelith::editing::EditError("style tabs missing");tabs->setCurrentIndex(o["styleTab"].toInt());}
    if(o.contains("presetCard")){auto* cards=window->findChild<QListWidget*>("portraitPresetCards");cards->setCurrentRow(o["presetCard"].toInt());}
    if(o.contains("hold")){auto* b=window->findChild<QAbstractButton*>(o["hold"].toString());if(!b)throw huelith::editing::EditError("button missing");QMetaObject::invokeMethod(b,o["pressed"].toBool()?"pressed":"released",Qt::DirectConnection);}
    if(o.contains("history"))window->history_step(o["history"].toInt());
    if(o.contains("parameter")){auto* control=window->findChild<QDoubleSpinBox*>("portraitParam_"+o["parameter"].toString());if(!control)throw huelith::editing::EditError("parameter control missing");control->setValue(o["value"].toDouble());}
    if(o.contains("hairZoneMode")){auto* control=window->findChild<QComboBox*>("portraitHairZoneMode");const int index=control->findData(o["hairZoneMode"].toString());control->setCurrentIndex(index);QMetaObject::invokeMethod(control,"activated",Qt::DirectConnection,Q_ARG(int,index));}
    for(const auto* key:{"strandWidth","strandDensity"})if(o.contains(key))window->findChild<QSlider*>(QString("portraitHair_")+key)->setValue(o[key].toInt());
    if(o.contains("styleSection"))window->findChild<QTabBar*>("portraitStyleTabs")->setCurrentIndex(o["styleSection"].toInt());
    if(o.contains("action")){auto* action=window->findChild<QAction*>(o["action"].toString());if(!action)throw huelith::editing::EditError("portrait action missing");action->trigger();}
    if(o.contains("click")){auto* control=window->findChild<QAbstractButton*>(o["click"].toString());if(!control)throw huelith::editing::EditError("portrait button missing");control->click();}
    if(o.contains("repairSelect"))window->findChild<QListWidget*>("portraitRepairList")->setCurrentRow(o["repairSelect"].toInt());
    if(o.contains("repairEnabled")){auto* item=window->findChild<QListWidget*>("portraitRepairList")->currentItem();if(!item)throw huelith::editing::EditError("repair selection missing");item->setCheckState(o["repairEnabled"].toBool()?Qt::Checked:Qt::Unchecked);}
    if(o.contains("repairFeather"))window->findChild<QSlider*>("portraitRepairFeather")->setValue(o["repairFeather"].toInt());
    if(o.contains("repairOpacity"))window->findChild<QSlider*>("portraitRepairOpacity")->setValue(o["repairOpacity"].toInt());
    if(o.contains("blemishSelect"))window->findChild<QListWidget*>("portraitBlemishes")->setCurrentRow(o["blemishSelect"].toInt());
    if(o.contains("toggle")){auto* control=window->findChild<QCheckBox*>(o["toggle"].toString());if(!control)throw huelith::editing::EditError("portrait checkbox missing");control->setChecked(o["checked"].toBool());}
    if(o.contains("presetIndex"))window->findChild<QComboBox*>("portraitPresets")->setCurrentIndex(o["presetIndex"].toInt());
    if(o.contains("presetCard")){auto* cards=window->findChild<QListWidget*>("portraitPresetCards");if(!cards || !cards->item(o["presetCard"].toInt()))throw huelith::editing::EditError("portrait preset card missing");cards->setCurrentRow(o["presetCard"].toInt());}
    if(o.contains("paletteIndex")){auto* palette=window->findChild<QComboBox*>("portraitSkinPalette");const int index=o["paletteIndex"].toInt();palette->setCurrentIndex(index);QMetaObject::invokeMethod(palette,"activated",Qt::DirectConnection,Q_ARG(int,index));}
    if(o.contains("savePaletteName")){QTimer::singleShot(0,[name=o["savePaletteName"].toString()]{auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());if(dialog){dialog->setTextValue(name);dialog->accept();}});window->findChild<QPushButton*>("portraitSaveSkinColor")->click();}
    if(o.contains("presetStrength"))window->findChild<QSlider*>("portraitPresetStrength")->setValue(o["presetStrength"].toInt());
    if(o.contains("presetCategory"))window->findChild<QCheckBox*>("portraitPresetCategory_"+o["presetCategory"].toString())->setChecked(o["checked"].toBool());
    if(o.contains("incomingPreset"))window->setProperty("incomingPreset",o["incomingPreset"].toObject().toVariantMap());
    if(o.contains("previewPreset")){window->findChild<QPushButton*>("portraitPreviewPreset")->click();window->findChild<QPushButton*>(o["previewPreset"].toBool()?"portraitPresetApply":"portraitPresetDiscard")->click();}
    if(o.contains("accessoryOcclusion")){auto* c=window->findChild<QComboBox*>("portraitAccessoryOcclusion");const int index=c->findData(o["accessoryOcclusion"].toString());c->setCurrentIndex(index);QMetaObject::invokeMethod(c,"activated",Qt::DirectConnection,Q_ARG(int,index));}
    if(o.contains("rangeKey")){auto* control=window->findChild<QComboBox*>("portraitRangeKey");control->setCurrentIndex(control->findData(o["rangeKey"].toString()));}
    if(o.contains("rangeVisible"))window->findChild<QCheckBox*>("portraitRangeVisible")->setChecked(o["rangeVisible"].toBool());
    if(o.contains("rangeMode")){window->findChild<QTabWidget*>("portraitInspectorTabs")->setCurrentIndex(1);window->findChild<QPushButton*>(QString("portraitRangeMode%1").arg(o["rangeMode"].toInt()))->click();}
    for(const auto* name:{"portraitVolumeAmount","portraitSlimAmount","portraitLiquifySize","portraitLiquifyHardness","portraitLiquifyPressure"})if(o.contains(name)){auto* control=window->findChild<QSpinBox*>(QString(name)+"Value");if(!control)throw huelith::editing::EditError("portrait numeric control missing");control->setValue(o[name].toInt());}
    if(o.contains("hover")){const auto a=o["hover"].toArray();auto* view=window->findChild<QGraphicsView*>("portraitView");const auto rect=view->scene()->sceneRect();const auto pos=view->mapFromScene(QPointF(a[0].toDouble()*rect.width(),a[1].toDouble()*rect.height()));QMouseEvent move(QEvent::MouseMove,QPointF(pos),QPointF(view->viewport()->mapToGlobal(pos)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&move);}
    if(o.contains("setSlider")){auto* c=window->findChild<QSlider*>(o["setSlider"].toString());if(!c)throw huelith::editing::EditError("slider missing");c->setValue(o["value"].toInt());}
    if(o.contains("brushSize"))window->findChild<QSlider*>("portraitBrushSize")->setValue(o["brushSize"].toInt());
    if(o.contains("liquifyTool")){auto* combo=window->findChild<QComboBox*>("portraitLiquifyTool");combo->setCurrentIndex(o["liquifyTool"].toInt());QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,o["liquifyTool"].toInt()));}
    if(o.contains("stroke")){
        auto* view=window->findChild<QGraphicsView*>("portraitView");const auto points=o["stroke"].toArray();const auto rect=view->scene()->sceneRect();
        for(int i=0;i<points.size();++i){const auto a=points[i].toArray();const QPointF pos=view->mapFromScene({a[0].toDouble()*rect.width(),a[1].toDouble()*rect.height()});
            const auto type=i==0?QEvent::MouseButtonPress:i==points.size()-1?QEvent::MouseButtonRelease:QEvent::MouseMove;
            QMouseEvent event(type,pos,view->viewport()->mapToGlobal(pos),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&event);
            QElapsedTimer step;step.start();while(step.elapsed()<20)QApplication::processEvents(QEventLoop::AllEvents,5);
        }
    }
    if(o.contains("paintPath")){
        auto* view=window->findChild<QGraphicsView*>("portraitView");const auto points=o["paintPath"].toArray();const auto rect=view->scene()->sceneRect();QJsonArray frames,events;
        for(int i=0;i<points.size();++i){const auto a=points[i].toArray();const QPointF point=view->mapFromScene({a[0].toDouble()*rect.width(),a[1].toDouble()*rect.height()});
            QElapsedTimer input;input.start();QMouseEvent event(i==0?QEvent::MouseButtonPress:QEvent::MouseMove,point,view->viewport()->mapToGlobal(point),i==0?Qt::LeftButton:Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&event);events.append(input.nsecsElapsed()/1e6);window->setProperty("paintEventMilliseconds",events);QApplication::processEvents();
            if(o.contains("paintStepMs")){QElapsedTimer step;step.start();while(step.elapsed()<o["paintStepMs"].toInt())QApplication::processEvents(QEventLoop::AllEvents,5);frames.append(qint64(window->property("portraitPreviewFrames").toULongLong()));window->setProperty("paintFrames",frames);}
            if(i==points.size()-1 && o["paintEnd"].toString()!="hold"){if(o["paintEnd"].toString()=="deactivate"){QEvent end(QEvent::WindowDeactivate);QApplication::sendEvent(window,&end);}else if(o["paintEnd"].toString()=="ungrab"){QEvent end(QEvent::UngrabMouse);QApplication::sendEvent(view->viewport(),&end);}else{QMouseEvent release(QEvent::MouseButtonRelease,point,view->viewport()->mapToGlobal(point),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&release);}}
        }
    }
    if(o.contains("paintRelease")){auto* view=window->findChild<QGraphicsView*>("portraitView");const auto a=o["paintRelease"].toArray();const auto rect=view->scene()->sceneRect();const QPointF p=view->mapFromScene({a[0].toDouble()*rect.width(),a[1].toDouble()*rect.height()});QMouseEvent release(QEvent::MouseButtonRelease,p,view->viewport()->mapToGlobal(p),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&release);}
    if(o.contains("viewSpace")){auto* view=window->findChild<QGraphicsView*>("portraitView");QKeyEvent event(o["viewSpace"].toBool()?QEvent::KeyPress:QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);QApplication::sendEvent(view,&event);}
    if(o["viewFocusOut"].toBool()){auto* view=window->findChild<QGraphicsView*>("portraitView");QFocusEvent event(QEvent::FocusOut);QApplication::sendEvent(view,&event);}
    if(o.contains("brushWheel")){auto* view=window->findChild<QGraphicsView*>("portraitView");const auto pos=view->viewport()->rect().center();QWheelEvent event(pos,view->viewport()->mapToGlobal(pos),{},QPoint(0,o["brushWheel"].toInt()),Qt::NoButton,o["wheelModifier"].toString()=="Ctrl"?Qt::ControlModifier:Qt::ShiftModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view->viewport(),&event);}
    if(o.contains("canvasKey")){auto* view=window->findChild<QGraphicsView*>("portraitView");const auto key=o["canvasKey"].toString();const int code=key=="Space"?Qt::Key_Space:key=="H"?Qt::Key_H:Qt::Key_B;QKeyEvent event(o["keyRelease"].toBool()?QEvent::KeyRelease:QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(view,&event);}
    if(o.contains("navigationMouse")){
        auto* view=window->findChild<QGraphicsView*>("portraitView");const auto a=o["navigationMouse"].toArray();const QPointF p(a[1].toDouble(),a[2].toDouble());const auto kind=a[0].toString();
        const auto type=kind=="press"?QEvent::MouseButtonPress:kind=="release"?QEvent::MouseButtonRelease:QEvent::MouseMove;
        QMouseEvent event(type,p,view->viewport()->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&event);
    }
    if(o.contains("navigationEnd")){auto* view=window->findChild<QGraphicsView*>("portraitView");QEvent event(o["navigationEnd"].toString()=="ungrab"?QEvent::UngrabMouse:QEvent::WindowDeactivate);QApplication::sendEvent(event.type()==QEvent::UngrabMouse?static_cast<QObject*>(view->viewport()):static_cast<QObject*>(window),&event);}
    if(o.contains("paint")){
        auto* view=window->findChild<QGraphicsView*>("portraitView");const auto a=o["paint"].toArray();const auto rect=view->scene()->sceneRect();const QPointF p=view->mapFromScene({a[0].toDouble()*rect.width(),a[1].toDouble()*rect.height()});
        const auto modifiers=o["alt"].toBool()?Qt::AltModifier:Qt::NoModifier;
        const auto button=o["rightClick"].toBool()?Qt::RightButton:Qt::LeftButton;
        QMouseEvent press(QEvent::MouseButtonPress,p,view->viewport()->mapToGlobal(p),button,button,modifiers);QApplication::sendEvent(view->viewport(),&press);
        QMouseEvent release(QEvent::MouseButtonRelease,p,view->viewport()->mapToGlobal(p),button,Qt::NoButton,modifiers);QApplication::sendEvent(view->viewport(),&release);
    }
    if(o["liquify"].toBool()) {
        if(window->state()["selected"].toInt(-1)<0)throw huelith::editing::EditError("liquify test has no selected person");
        window->findChild<QListWidget*>("portraitNavigation")->setCurrentRow(4);
        auto* liquifyButton=window->findChild<QPushButton*>("portraitLiquify");if(!liquifyButton)throw huelith::editing::EditError("liquify button missing");liquifyButton->click();
        window->findChild<QSlider*>("portraitLiquifySize")->setValue(300);window->findChild<QSlider*>("portraitLiquifyPressure")->setValue(100);
        auto* view=window->findChild<QGraphicsView*>("portraitView");const auto state=window->state();const auto face=state["recipe"].toObject()["people"].toArray()[state["selected"].toInt()].toObject()["face"].toArray();const auto rect=view->scene()->sceneRect();
        const QPointF a=view->mapFromScene({(face[0].toDouble()+face[2].toDouble()*.5)*rect.width(),(face[1].toDouble()+face[3].toDouble()*.7)*rect.height()}),b=a+QPointF(14,0);
        QMouseEvent press(QEvent::MouseButtonPress,a,view->viewport()->mapToGlobal(a),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&press);
        for(int i=1;i<=6;++i){const auto point=a+(b-a)*(i/6.);QMouseEvent move(QEvent::MouseMove,point,view->viewport()->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&move);}
        QMouseEvent release(QEvent::MouseButtonRelease,b,view->viewport()->mapToGlobal(b),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&release);
        if(o.contains("nestedAccept") && !o["nestedAccept"].toBool())window->history_step(-1);
    }
    if(o["detect"].toBool())window->analyze();
    if(o["cancelCompute"].toBool())window->findChild<QPushButton*>("portraitCancelCompute")->click();
    if(o["accept"].toBool())window->accept();
    if(o["reject"].toBool())window->reject();
    if(o.contains("doubleClickParameter") || o.contains("doubleClickControl")){const auto name=o.contains("doubleClickControl")?o["doubleClickControl"].toString():"portraitSlider_"+o["doubleClickParameter"].toString();auto* slider=window->findChild<QSlider*>(name);if(!slider)throw huelith::editing::EditError("missing reset slider");const QPointF pos=slider->rect().center();QMouseEvent dbl(QEvent::MouseButtonDblClick,pos,slider->mapToGlobal(pos),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(slider,&dbl);}
    if(o.contains("capture")){auto* target=o.contains("captureWidget")?window->findChild<QWidget*>(o["captureWidget"].toString()):window.data();if(!target)throw huelith::editing::EditError("capture widget missing");target->grab().save(o["capture"].toString());}
    auto state=window->state();
    if(o["workspaceLayout"].toBool()){
        QJsonObject bounds;
        for(const auto* name:{"portraitTasks","portraitView","portraitInspector","portraitBottomBar","portraitPeopleBar"}){
            auto* widget=window->findChild<QWidget*>(name);if(!widget)throw huelith::editing::EditError("workspace widget missing");
            const auto pos=widget->mapTo(window,QPoint{});bounds[name]=QJsonArray{pos.x(),pos.y(),widget->width(),widget->height()};
        }
        state["workspaceLayout"]=bounds;state["workspaceTask"]=window->property("portraitWorkspaceTask").toInt();
        QJsonArray names;auto* tasks=window->findChild<QListWidget*>("portraitTasks");for(int i=0;i<tasks->count();++i)names.append(tasks->item(i)->text());state["workspaceTasks"]=names;QJsonArray controls;for(auto* button:window->findChildren<QAbstractButton*>())if(button->isVisible())controls.append(button->objectName());state["visibleControls"]=controls;
    }
    if(o["suggestionOutline"].toBool()){
        auto* view=window->findChild<QGraphicsView*>("portraitView");
        for(auto* item:view->scene()->items())if(item->data(0).toString()=="portraitCursor"){
            const auto path=static_cast<QGraphicsPathItem*>(item)->path();const auto rect=path.boundingRect();
            state["suggestionOutline"]=QJsonObject{{"elements",path.elementCount()},{"bounds",QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()}},{"previewWidth",view->scene()->sceneRect().width()}};
        }
    }
    if(o.contains("previewField")){const auto mesh=window->preview_mesh();state["previewMeshSize"]=QJsonArray{mesh.width(),mesh.height()};QFile file(o["previewField"].toString());if(!file.open(QIODevice::WriteOnly))throw huelith::editing::EditError("preview field output failed");for(int y=0;y<mesh.height();++y)file.write(reinterpret_cast<const char*>(mesh.constScanLine(y)),mesh.width()*4*sizeof(float));}
    if(o.contains("previewImage"))window->preview_image().save(o["previewImage"].toString());
    if(o["brushControls"].toBool()){
        QJsonObject values;for(const auto* name:{"portraitLiquifySize","portraitLiquifyHardness","portraitLiquifyPressure"}){auto* slider=window->findChild<QSlider*>(name);auto* number=window->findChild<QSpinBox*>(QString(name)+"Value");if(!slider || !number)throw huelith::editing::EditError("liquify controls missing");values[name]=QJsonArray{slider->value(),number->value()};values[QString(name)+"Count"]=window->findChildren<QSlider*>(name).size();}state["brushControls"]=values;
        auto* view=window->findChild<QGraphicsView*>("portraitView");for(auto* item:view->scene()->items())if(item->data(0).toString()=="portraitCursor"){const auto rect=static_cast<QGraphicsPathItem*>(item)->path().boundingRect();state["brushCircleSceneDiameter"]=rect.width();state["brushCircleScreenDiameter"]=rect.width()*view->transform().m11();}state["previewWidth"]=view->scene()->sceneRect().width();
    }
    state["paintEventMilliseconds"]=window->property("paintEventMilliseconds").toJsonArray();state["paintFrames"]=window->property("paintFrames").toJsonArray();if(o["paletteState"].toBool())state["paletteEntries"]=QJsonDocument::fromJson(QSettings().value("portrait/colors").toByteArray()).array();state["visible"]=window->isVisible();state["result"]=window->result();state["dragFrames"]=window->property("dragFrames").toJsonArray();state["windowIcon"]=!window->windowIcon().isNull();state["modal"]=QApplication::activeModalWidget()!=nullptr;state["dpr"]=window->devicePixelRatioF();auto* accept=window->findChild<QPushButton*>("portraitAccept");const auto accept_rect=QRect(accept->mapTo(window,QPoint{}),accept->size());state["acceptVisible"]=accept->isVisible() && window->rect().contains(accept_rect);
    if(o.contains("control")){auto* w=window->findChild<QWidget*>(o["control"].toString());if(w){QJsonObject c{{"visible",w->isVisible()},{"enabled",w->isEnabled()}};if(auto* b=qobject_cast<QAbstractButton*>(w))c["checked"]=b->isChecked();if(auto* spin=qobject_cast<QDoubleSpinBox*>(w))c["value"]=spin->value();state["control"]=c;}}

    if(o.contains("mask")){const auto person=state["recipe"].toObject()["people"].toArray()[state["selected"].toInt()].toObject();huelith::editing::portrait_effect_mask(person,o["maskKey"].toString("skin.smooth"),{768,768}).save(o["mask"].toString());}
    if(o.contains("render")){
        const auto source=huelith::editing::import_image(o["source"].toString());
        huelith::editing::Effect effect;effect.name="Portrait";effect.portrait=window->recipe();effect.amount=o["amount"].toDouble(1);
        const auto rendered=huelith::editing::apply_effects(source,{effect});
        huelith::editing::display_image(rendered).save(o["render"].toString());
        state["identity"]=rendered==source;
        if(o["checkRegion"].toBool()) {
            const QRect rect(source.width()/4,source.height()/4,source.width()/2,source.height()/2);
            const auto roi=huelith::editing::portrait_region(source,window->recipe(),rect);const auto expected=rendered.copy(rect);double maximum{};
            for(int y=0;y<roi.height();++y){const auto* a=reinterpret_cast<const float*>(roi.constScanLine(y));const auto* b=reinterpret_cast<const float*>(expected.constScanLine(y));for(int x=0;x<roi.width()*4;++x)maximum=std::max(maximum,std::abs(double(a[x])-b[x]));}
            state["regionError"]=maximum;
        }
    }
    if(o.contains("save")) {
        huelith::editing::State doc;doc.name="Portrait validation";huelith::editing::Layer layer;layer.name="Portrait";
        layer.pixels=huelith::editing::import_image(o["source"].toString());doc.size=layer.pixels.size();
        huelith::editing::Effect e;e.portrait=window->recipe();e.name=QStringLiteral("人像精修");layer.effects.push_back(e);doc.layers.push_back(layer);
        huelith::editing::save_project({doc,1},o["save"].toString());
        const auto loaded=huelith::editing::load_project(o["save"].toString());
        state["roundtrip"]=loaded.layers[0].effects[0].portrait==e.portrait;
    }
    auto* navigation=static_cast<huelith::desktop::RawPreviewView*>(window->findChild<QGraphicsView*>("portraitView"));
    if(o.contains("readSlider")){auto* slider=window->findChild<QSlider*>(o["readSlider"].toString());if(!slider)throw huelith::editing::EditError("slider missing");state["sliderValue"]=slider->value();}
    const auto center=navigation->mapToScene(navigation->viewport()->rect().center());state["navigation"]=QJsonObject{{"scale",navigation->transform().m11()},{"temporary",navigation->temporary_pan()},{"center",QJsonArray{center.x(),center.y()}},{"cursor",int(navigation->viewport()->cursor().shape())}};
    return state;
}

QJsonObject portrait_session_probe(huelith::desktop::MainWindow* main,const QJsonObject& request) {
    using namespace huelith::editing;
    if(!main)throw EditError("missing main window");
    main->open_document(request["source"].toString());auto* canvas=main->active_canvas();
    if(!canvas)throw EditError("missing canvas");
    const auto id=canvas->active_layer;
    canvas->edit("Seed saved portrait",[&](State& state){auto* layer=find_layer(state.layers,id);Effect effect;effect.name="Portrait";effect.portrait=request["recipe"].toObject();layer->effects.push_back(effect);});
    const auto revision=canvas->project().revision();const auto original=find_layer(canvas->project().state().layers,id)->effects.back().portrait;
    QJsonObject result;QElapsedTimer elapsed;elapsed.start();QTimer timer;timer.setInterval(50);
    int sync_stage=0;QPointer<huelith::desktop::PortraitDialog> source_dialog,target_dialog;const int documents_before=main->document_count();
    if(request.contains("syncSource"))QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
    QObject::connect(&timer,&QTimer::timeout,main,[&]{
        if(request.contains("syncSource")){
            if(elapsed.elapsed()>90000){result["timeout"]=true;result["syncStage"]=sync_stage;QJsonArray tops;for(auto* top:QApplication::topLevelWidgets())if(top->isVisible())tops.append(top->windowTitle());result["visible"]=tops;if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();if(target_dialog)target_dialog->reject();if(source_dialog)source_dialog->reject();return;}
            if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());file && sync_stage==1){const QFileInfo info(request["syncSource"].toString());file->setDirectory(info.absolutePath());file->selectFile(info.fileName());sync_stage=2;QTimer::singleShot(500,file,[file,info]{if(auto* edit=file->findChild<QLineEdit*>("fileNameEdit"))edit->setText(info.fileName());QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);});return;}
            if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());modal && modal->objectName()=="portraitPresetPreview"){modal->accept();sync_stage=4;return;}
            for(auto* top:QApplication::topLevelWidgets())if(auto* dialog=dynamic_cast<huelith::desktop::PortraitDialog*>(top)){
                if(sync_stage==2 && dialog!=source_dialog && dialog->state()["ready"].toBool() && !dialog->property("syncModelsSet").toBool()){dialog->setProperty("modelDirectory",QDir::current().filePath("build/windows-release/models"));dialog->setProperty("syncModelsSet",true);dialog->analyze();}
                if(!dialog->isVisible() || !dialog->state()["ready"].toBool() || !dialog->state()["count"].toInt())continue;
                if(sync_stage==0){source_dialog=dialog;dialog->select_person(0);dialog->findChild<QListWidget*>("portraitNavigation")->setCurrentRow(5);dialog->set_parameter("skin.smooth",25);dialog->findChild<QListWidget*>("portraitNavigation")->setCurrentRow(15);sync_stage=1;QTimer::singleShot(0,dialog,[dialog]{dialog->findChild<QPushButton*>("portraitSync")->click();});return;}
                if(sync_stage==2 && dialog!=source_dialog){target_dialog=dialog;dialog->findChild<QListWidget*>("portraitPeople")->setCurrentRow(0);dialog->findChild<QListWidget*>("portraitNavigation")->setCurrentRow(15);dialog->findChild<QComboBox*>("portraitPresets")->setCurrentIndex(5);sync_stage=3;QTimer::singleShot(0,dialog,[dialog]{dialog->findChild<QPushButton*>("portraitPreviewPreset")->click();});return;}
                if(sync_stage==3 && dialog==target_dialog && dialog->findChild<QPushButton*>("portraitPresetApply")->isVisible()){dialog->findChild<QPushButton*>("portraitPresetApply")->click();sync_stage=4;return;}
                if(sync_stage==4 && dialog==target_dialog){dialog->accept();sync_stage=5;return;}
                if(sync_stage==5 && dialog==source_dialog && dialog->property("syncTargets").toJsonArray().size()==1){result["stagedTargets"]=1;sync_stage=6;if(request["accept"].toBool())dialog->accept();else dialog->reject();return;}
            }
            return;
        }
        for(auto* top:QApplication::topLevelWidgets())if(auto* dialog=dynamic_cast<huelith::desktop::PortraitDialog*>(top)){
            if(!dialog->isVisible())continue;
            if(elapsed.elapsed()>10000){result["timeout"]=true;dialog->reject();return;}
            if(!dialog->state()["ready"].toBool() || dialog->state()["count"].toInt()==0)return;
            dialog->findChild<QListWidget*>("portraitPeople")->setCurrentRow(0);
            auto* linked=dialog->findChild<QCheckBox*>("portraitLinked");linked->setChecked(!linked->isChecked());
            if(request["concurrentEdit"].toBool())canvas->edit("Concurrent name change",[](State& state){state.name="Concurrent edit retained";});
            result["changedInside"]=dialog->recipe()!=original;
            if(request["accept"].toBool())dialog->findChild<QPushButton*>("portraitAccept")->click();else dialog->reject();
            return;
        }
    });
    timer.start();auto* action=main->findChild<QAction*>("portraitAction");if(!action)throw EditError("missing portrait action");action->trigger();timer.stop();
    result["revisionDelta"]=double(canvas->project().revision()-revision);
    const auto after=find_layer(canvas->project().state().layers,id)->effects.back().portrait;
    result["changedAfter"]=after!=original;
    result["newDocuments"]=main->document_count()-documents_before;
    if(request.contains("syncSource") && request["accept"].toBool() && sync_stage==6){const auto* target=main->active_canvas();const auto* layer=find_layer(target->project().state().layers,target->active_layer);result["syncedParameters"]=layer->effects.back().portrait["people"].toArray()[0].toObject()["settings"].toObject()["parameters"];}
    if(request["concurrentEdit"].toBool()){result["originalName"]=canvas->project().state().name;auto* copy=main->active_canvas();result["copyHasPortrait"]=copy!=canvas && !find_layer(copy->project().state().layers,id)->effects.back().portrait.isEmpty();}
    if(request["accept"].toBool() && !request["concurrentEdit"].toBool()){canvas->project().undo();result["oneUndoRestores"]=find_layer(canvas->project().state().layers,id)->effects.back().portrait==original;}
    return result;
}

