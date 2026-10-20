#include "huelith/editing/enhancement.h"
#include "huelith/editing/project.h"
#include "huelith/editing/photo_merge.h"
#include "huelith/editing/dng.h"
#include "huelith/editing/metadata.h"
#include "huelith/editing/embedded_preview.h"
#include "huelith/editing/export_queue.h"
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QThread>
#include <QMessageBox>
#include "raw_dialog.h"
#include "huelith/editing/raw_develop.h"
#include "huelith/editing/raw_sidecar.h"
#include <QFile>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <thread>
#include "main_window.h"
#include "canvas_view.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>

QJsonObject enhancement_probe(const QJsonObject& request) {
    using namespace huelith::editing;
    EnhanceOptions options;
    options.mode = EnhanceMode(request["mode"].toInt());
    options.scale = request["scale"].toInt(2);
    options.strength = request["strength"].toDouble(1);
    options.quality = request["quality"].toBool();
    options.directml = request["directml"].toBool(false);
    options.model = request["model"].toString();
    auto image = import_image(request["source"].toString());
    std::atomic_bool cancelled{request["cancelled"].toBool()};
    std::jthread timer([&](std::stop_token stop) {
        const int limit = request["cancelAfterMs"].toInt();
        if (limit <= 0) return;
        QElapsedTimer elapsed; elapsed.start();
        while (!stop.stop_requested() && elapsed.elapsed() < limit) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!stop.stop_requested()) cancelled = true;
    });
    QElapsedTimer elapsed; elapsed.start();
    int events{};
    const auto result = enhance_image(image, options, &cancelled, [&](int, int) { ++events; });
    timer.request_stop();
    auto metadata = result.metadata;
    metadata["width"] = result.image.width(); metadata["height"] = result.image.height();
    metadata["elapsedMs"] = double(elapsed.elapsed()); metadata["progressEvents"] = events;
    if (request.contains("output")) {
        const auto output = result.image.format() == QImage::Format_Grayscale8 ? result.image : display_image(result.image);
        if (!output.save(request["output"].toString())) throw EditError("cannot save enhancement evidence");
    }
    if (result.image.format() == QImage::Format_RGBA32FPx4_Premultiplied) {
        QJsonArray pixel;
        const auto* rgba = reinterpret_cast<const float*>(result.image.constBits());
        for (int i = 0; i < 4; ++i) pixel.append(rgba[i]);
        metadata["firstPixel"] = pixel;
    }
    return metadata;
}

QJsonObject enhancement_ui(huelith::desktop::MainWindow* window, const QJsonObject& request) {
    using namespace huelith::editing;
    if (!window || !window->active_canvas()) throw EditError("missing window");
    const int mode = request["mode"].toInt();
    if(request.contains("fixture")) {
        State fixture;fixture.size={384,256};fixture.name="Layer enhancement fixture";
        Layer background;background.name="Unrelated backdrop";background.kind=LayerKind::fill;background.fill=QColor("#14813b");
        fixture.layers.push_back(background);
        Layer target;target.name="Selected photograph";target.pixels=import_image(request["fixture"].toString());
        target.transform.translate(65,45);target.transform.rotate(13);target.transform.scale(.8,.65);
        target.locked=request["locked"].toBool();if(request["unsupported"].toBool())target.kind=LayerKind::fill;
        target.mask=QImage(fixture.size,QImage::Format_Grayscale8);target.mask.fill(220);target.opacity=.7F;
        Effect blur;blur.blur_radius=.3;target.effects.push_back(blur);target.source_metadata={{"test","retained"}};
        if(request["warp"].toBool()){target.warped=true;target.warp_grid[4]+=QPointF(.03,-.02);}
        const auto id=target.id;
        Layer group;group.name="Transformed group";group.kind=LayerKind::group;group.transform.translate(21,14);group.transform.scale(1.15,1.15);group.children.push_back(target);
        fixture.layers.push_back(group);window->active_canvas()->load_state(std::move(fixture),false);
        window->active_canvas()->active_layer=id;window->active_canvas()->selected_layers={id};
        if(request["multi"].toBool())window->active_canvas()->selected_layers.push_back(background.id);
    }
    const auto source_id=window->active_canvas()->active_layer;
    const auto source_document=window->active_canvas();
    QAction* action{};
    action=window->findChild<QAction*>(QString(request["layer"].toBool()?"layerEnhancement%1":"imageEnhancement%1").arg(mode));
    if (!action) throw EditError("missing enhancement menu");
    QElapsedTimer elapsed; elapsed.start();
    bool started{}, previewed{};
    QTimer timer;
    timer.setInterval(30);
    QString status;
    QObject::connect(&timer, &QTimer::timeout, window, [&] {
        if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){status=message->text();message->accept();return;}
        QDialog* dialog{};for(auto* widget:QApplication::topLevelWidgets())
            if(widget->objectName()=="enhancementDialog")dialog=qobject_cast<QDialog*>(widget);
        if(!dialog)return;
        if (elapsed.elapsed() > 120000) { status = "UI timeout"; dialog->reject(); return; }
        if (!started) {
            started = true;
            if(mode==0) dialog->findChild<QComboBox*>("enhancementScale")->setCurrentIndex(request["scale"].toInt(2)==4?1:0);
            dialog->findChild<QComboBox*>("enhancementProvider")->setCurrentIndex(request["directml"].toBool() ? 0 : 1);
            dialog->findChild<QPushButton*>("enhancementInspect")->click();
        } else if (!previewed && dialog->property("enhancementReady").toBool()) {
            previewed = true;
            status = dialog->findChild<QLabel*>("enhancementStatus")->text();
            if (request.contains("capture")) dialog->grab().save(request["capture"].toString());
            if(request["changeSource"].toBool())
                window->active_canvas()->edit("Concurrent source change",[](State& state){state.name+=" changed";});
            if (request["cancel"].toBool()) dialog->reject();
            else dialog->findChild<QPushButton*>("enhancementApply")->click();
        } else if (started && !dialog->property("enhancementRunning").toBool() && !previewed) {
            status = dialog->findChild<QLabel*>("enhancementStatus")->text();
            dialog->reject();
        }
    });
    timer.start();
    const auto original = window->active_canvas()->project().snapshot();
    const auto count = window->document_count();
    action->trigger();
    timer.stop();
    const auto& state = window->active_canvas()->project().state();
    QJsonObject details;
    if(request["layer"].toBool()) {
        const auto* old=find_layer(original.state.layers,source_id);
        const auto* preserved=find_layer(state.layers,source_id);
        const auto* enhanced=find_layer(state.layers,window->active_canvas()->active_layer);
        details["revisionDelta"]=double(source_document->project().revision()-original.revision);
        details["originalPixelsPreserved"]=preserved && old && preserved->pixels==old->pixels;
        details["originalVisible"]=preserved && preserved->visible;
        details["selectedChanged"]=enhanced && enhanced->id!=source_id;
        if(enhanced && old){details["sourceWidth"]=old->pixels.width();details["pixelWidth"]=enhanced->pixels.width();details["pixelHeight"]=enhanced->pixels.height();
            details["maskPreserved"]=enhanced->mask==old->mask && enhanced->mask_transform==old->mask_transform;
            details["opacityPreserved"]=enhanced->opacity==old->opacity;details["warpPreserved"]=enhanced->warp_grid==old->warp_grid && enhanced->warped==old->warped;
            const auto a=layer_bounds(original.state,source_id),b=layer_bounds(state,enhanced->id);
            details["boundsError"]=std::max({std::abs(a.x()-b.x()),std::abs(a.y()-b.y()),std::abs(a.width()-b.width()),std::abs(a.height()-b.height())});
            details["alphaCorner"]=double(reinterpret_cast<const float*>(enhanced->pixels.constBits())[3]);
            details["blurRadius"]=enhanced->effects.empty()?0:enhanced->effects[0].blur_radius;
        }
        if(!state.selection.isNull()){details["outsideSelection"]=state.selection.constScanLine(0)[0];int maximum=0;for(int y=0;y<state.selection.height();++y)for(int x=0;x<state.selection.width();++x)maximum=std::max(maximum,int(state.selection.constScanLine(y)[x]));details["selectionMaximum"]=maximum;}
        if(request.contains("save")){save_project(window->active_canvas()->project().snapshot(),request["save"].toString());const auto loaded=load_project(request["save"].toString());details["roundtrip"]=render(loaded)==render(state);}
        if(request["undoCheck"].toBool()){const auto after=render(state);auto& project=window->active_canvas()->project();project.undo();details["undoRestores"]=render(project.state())==render(original.state);project.redo();details["redoRestores"]=render(project.state())==after;}
    }
    return {{"layerResult",details},{"previewed", previewed}, {"status", status}, {"documentsBefore", count},
        {"documentsAfter", window->document_count()}, {"width", state.size.width()}, {"height", state.size.height()},
        {"hasSelection", !state.selection.isNull()}, {"dirty", window->active_canvas()->project().dirty()},
        {"elapsedMs", double(elapsed.elapsed())}, {"originalRevision", double(original.revision)}};
}

QJsonObject photo_merge_probe(const QJsonObject& request) {
    using namespace huelith::editing;
    PhotoMergeOptions options;
    options.mode=PhotoMergeMode(request["mode"].toInt());
    for(const auto& path:request["paths"].toArray())options.paths.append(path.toString());
    for(const auto& ev:request["ev"].toArray())options.exposure_ev.push_back(ev.toDouble());
    options.align=request["align"].toBool(true);options.crop=request["crop"].toBool(true);
    options.reference=request["reference"].toInt();options.deghost=request["deghost"].toInt();
    options.projection=request["projection"].toInt();
    std::atomic_bool cancelled{request["cancelled"].toBool()};
    QElapsedTimer elapsed;elapsed.start();
    const auto result=merge_photos(options,&cancelled);
    auto output=result.metadata;output["width"]=result.image.width();output["height"]=result.image.height();output["elapsedMs"]=double(elapsed.elapsed());
    if(request.contains("output"))display_image(result.image).save(request["output"].toString());
    QJsonArray samples;
    float maximum{};
    for(int y=0;y<result.image.height();++y) {
        const auto* row=reinterpret_cast<const float*>(result.image.constScanLine(y));
        for(int x=0;x<result.image.width();++x)for(int c=0;c<3;++c)maximum=std::max(maximum,row[x*4+c]);
    }
    for(const auto& point:request["samples"].toArray()) {
        const auto p=point.toArray();const int x=p[0].toInt(),y=p[1].toInt();
        if(x<0||y<0||x>=result.image.width()||y>=result.image.height())throw EditError("sample outside merge");
        const auto* row=reinterpret_cast<const float*>(result.image.constScanLine(y));
        samples.append(QJsonArray{row[x*4],row[x*4+1],row[x*4+2],row[x*4+3]});
    }
    output["samples"]=samples;output["maximumLinear"]=maximum;return output;
}

QJsonObject photo_merge_ui(huelith::desktop::MainWindow* window,const QJsonObject& request) {
    using namespace huelith::editing;
    if(!window)throw EditError("missing merge window");
    const int mode=request["mode"].toInt();
    const QString title=mode==0?QStringLiteral("合并到 HDR…"):mode==1?QStringLiteral("Photomerge 全景…"):QStringLiteral("景深合成…");
    QAction* action{};for(auto* item:window->findChildren<QAction*>())if(item->text()==title)action=item;
    if(!action)throw EditError("merge menu missing");
    bool started{},previewed{};QString status;QElapsedTimer elapsed;elapsed.start();QTimer timer;timer.setInterval(40);
    QObject::connect(&timer,&QTimer::timeout,window,[&]{
        QDialog* dialog{};for(auto* widget:QApplication::topLevelWidgets())if(widget->objectName()=="photoMergeDialog")dialog=qobject_cast<QDialog*>(widget);if(!dialog)return;
        if(elapsed.elapsed()>120000){status="merge UI timeout";dialog->reject();return;}
        if(!started){started=true;
            auto* table=dialog->findChild<QTableWidget*>("photoMergeSources");const auto paths=request["paths"].toArray(),ev=request["ev"].toArray();
            auto* reference=dialog->findChild<QComboBox*>("photoMergeReference");
            for(int i=0;i<paths.size();++i){table->insertRow(i);auto* item=new QTableWidgetItem(paths[i].toString());item->setData(Qt::UserRole,paths[i].toString());table->setItem(i,0,item);
                auto* value=new QDoubleSpinBox(table);value->setRange(-20,20);value->setValue(ev.size()>i?ev[i].toDouble():0);table->setCellWidget(i,1,value);if(reference)reference->addItem(paths[i].toString());}
            if(reference)reference->setCurrentIndex(request["reference"].toInt());
            if(auto* align=dialog->findChild<QCheckBox*>("photoMergeAlign"))align->setChecked(request["align"].toBool(true));
            dialog->findChild<QPushButton*>("photoMergeInspect")->click();
        }else if(!previewed&&dialog->property("photoMergeReady").toBool()){
            previewed=true;status=dialog->findChild<QLabel*>("photoMergeStatus")->text();if(request.contains("capture"))dialog->grab().save(request["capture"].toString());
            if(request["cancel"].toBool())dialog->reject();else dialog->findChild<QPushButton*>("photoMergeApply")->click();
        }else if(!dialog->property("photoMergeRunning").toBool()&&!previewed){status=dialog->findChild<QLabel*>("photoMergeStatus")->text();dialog->reject();}
    });
    const int count=window->document_count();timer.start();action->trigger();timer.stop();
    return {{"previewed",previewed},{"status",status},{"documentsBefore",count},{"documentsAfter",window->document_count()},
        {"width",window->active_canvas()?window->active_canvas()->project().state().size.width():0}};
}

QJsonObject dng_probe(const QJsonObject& request) {
    using namespace huelith::editing;
    Layer source;source.linked_path=request["source"].toString();source.raw_recipe=request["recipe"].toObject();
    QFile original(source.linked_path);if(!original.open(QIODevice::ReadOnly))throw EditError("DNG source missing");source.raw_source=original.readAll();original.close();
    const auto before=raw_sensor_info(source.raw_source);const auto digest=QCryptographicHash::hash(source.raw_source,QCryptographicHash::Sha256);
    const QString path=request["output"].toString();const std::atomic_bool cancelled{request["cancelled"].toBool()};
    save_raw_dng(source,path,&cancelled);QFile output(path);if(!output.open(QIODevice::ReadOnly))throw EditError("DNG output missing");const auto bytes=output.readAll();
    Layer reopened;reopened.linked_path=path;reopened.raw_source=bytes;
    QJsonObject result{{"before",before},{"after",raw_sensor_info(bytes)},{"recipe",load_raw_sidecar(reopened)},{"bytes",double(bytes.size())}};
    if(!original.open(QIODevice::ReadOnly))throw EditError("DNG original unreadable after saving");
    result["originalUnchanged"]=QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256)==digest;
    if(request["compareDevelop"].toBool()) {
        const auto a=develop_raw(source.linked_path,RawOptions{},cancelled).image,b=develop_raw(path,RawOptions{},cancelled).image;
        result["sameSize"]=a.size()==b.size();double sum{},maximum{};
        if(a.size()!=b.size())return result;
        for(int y=0;y<a.height();++y){const auto* first=reinterpret_cast<const float*>(a.constScanLine(y));const auto* second=reinterpret_cast<const float*>(b.constScanLine(y));
            for(int x=0;x<a.width()*4;++x){const double error=std::abs(double(first[x])-second[x]);sum+=error;maximum=std::max(maximum,error);}}
        result["meanError"]=sum/(double(a.width())*a.height()*4);result["maxError"]=maximum;
    }
    return result;
}

QJsonObject metadata_probe(const QJsonObject& request) {
    using namespace huelith::editing;
    State state;state.name="Metadata validation";
    if(request.contains("projectInput"))state=load_project(request["projectInput"].toString());
    else {Layer layer;layer.pixels=import_image(request["source"].toString());layer.name="Original";
        state.size=layer.pixels.size();state.metadata=read_photo_metadata(request["source"].toString());
        layer.source_metadata=state.metadata;state.layers.push_back(layer);}
    const auto caption=exif_caption(state.metadata);
    if(request["border"].toBool())state=exif_border(state,12,50,12,Qt::white,Qt::black,caption,
        request.contains("logo")?import_image(request["logo"].toString()):QImage{});
    if(request.contains("project"))save_project({state,1},request["project"].toString());
    if(request.contains("output")) {
        ExportQueue queue;ExportOptions options;options.path=request["output"].toString();
        options.format=request["format"].toString("png").toLatin1();options.bits=request["bits"].toInt(8);
        options.conflict=ExportConflict::overwrite;
        if(request.contains("size")){auto size=request["size"].toArray();options.size={size[0].toInt(),size[1].toInt()};}
        if(request.contains("flags")){auto flags=request["flags"].toArray();options.metadata={flags[0].toBool(),flags[1].toBool(),flags[2].toBool(),flags[3].toBool(),flags[4].toBool()};}
        (void)queue.add({state,1},options);queue.start();QElapsedTimer elapsed;elapsed.start();
        while(elapsed.elapsed()<60000){const auto task=queue.items().front();
            if(task.status==ExportStatus::complete)break;
            if(task.status==ExportStatus::failed||task.status==ExportStatus::cancelled)throw EditError(task.error);
            QThread::msleep(10);}
        if(queue.items().front().status!=ExportStatus::complete)throw EditError("Metadata export timeout");
    }
    return {{"metadata",state.metadata},{"caption",caption},{"width",state.size.width()},{"height",state.size.height()},
        {"layers",int(state.layers.size())},{"editableText",std::any_of(state.layers.begin(),state.layers.end(),[](const Layer& l){return l.kind==LayerKind::text;})}};
}
QJsonObject dng_ui(QDialog* dialog,const QJsonObject& request) {
    if(!dialog)throw huelith::editing::EditError("RAW dialog missing");
    if(request["embedded"].toBool()){
        QTimer answer;answer.setInterval(20);QObject::connect(&answer,&QTimer::timeout,dialog,[&]{
            if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))message->button(QMessageBox::Yes)->click();});
        answer.start();auto* button=dialog->findChild<QPushButton*>("rawOpenEmbedded");const bool offered=button->isVisible();button->click();answer.stop();
        const auto& sources=static_cast<huelith::desktop::RawDialog*>(dialog)->results();
        return {{"offered",offered},{"accepted",dialog->result()==QDialog::Accepted},{"count",int(sources.size())},
            {"rawBytes",double(sources.front().raw_source.size())},{"width",sources.front().pixels.width()},{"name",sources.front().name}};
    }
    bool file_seen{};QTimer timer;timer.setInterval(20);
    QObject::connect(&timer,&QTimer::timeout,dialog,[&]{
        if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){
            file_seen=true;if(request["cancel"].toBool())file->reject();
            else{file->selectFile(request["output"].toString());QMetaObject::invokeMethod(file,"accept");}
        }
    });
    timer.start();dialog->findChild<QPushButton*>("rawSaveDng")->click();timer.stop();
    QElapsedTimer elapsed;elapsed.start();
    while(!request["cancel"].toBool()&&!dialog->property("rawDngSaved").toBool()&&elapsed.elapsed()<30000){
        QApplication::processEvents();QThread::msleep(15);}
    if(request.contains("capture"))dialog->grab().save(request["capture"].toString());
    return {{"fileDialog",file_seen},{"saved",dialog->property("rawDngSaved").toBool()},{"open",dialog->isVisible()}};
}
QJsonObject border_ui(huelith::desktop::MainWindow* window,const QJsonObject& request) {
    using namespace huelith::editing;if(!window||!window->active_canvas())throw EditError("Main canvas missing");
    QAction* action{};for(auto* item:window->findChildren<QAction*>())if(item->text()==QStringLiteral("EXIF 照片边框…"))action=item;
    if(!action)throw EditError("Border action missing");bool shown{};QTimer timer;timer.setInterval(30);
    QObject::connect(&timer,&QTimer::timeout,window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog||dialog->objectName()!="exifBorderDialog")return;
        shown=true;dialog->findChild<QCheckBox*>("exifBorderBatch")->setChecked(request["batch"].toBool());
        if(request.contains("capture"))dialog->grab().save(request["capture"].toString());
        if(request["cancel"].toBool())dialog->reject();else dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    const int count=window->document_count();timer.start();action->trigger();timer.stop();
    return {{"shown",shown},{"before",count},{"after",window->document_count()},{"editableText",window->active_canvas()->project().state().layers.back().kind==LayerKind::text}};
}
QJsonObject embedded_probe(const QJsonObject& request) {
    using namespace huelith::editing;Layer layer;QFile file(request["source"].toString());
    if(!file.open(QIODevice::ReadOnly))throw EditError("Embedded source missing");layer.raw_source=file.readAll();layer.source_metadata=request["metadata"].toObject();
    const auto preview=embedded_preview(layer);if(request.contains("output"))display_image(preview.image).save(request["output"].toString());
    QImage dark=preview.image.copy();for(int y=0;y<dark.height();++y){auto* row=reinterpret_cast<float*>(dark.scanLine(y));for(int x=0;x<dark.width();++x)for(int c=0;c<3;++c)row[x*4+c]*=.5F;}
    return {{"width",preview.image.width()},{"height",preview.image.height()},{"bits",preview.bits},{"description",preview.description},
        {"sameExposure",preview_exposure_reference(preview.image,preview.image)},{"darkExposure",preview_exposure_reference(dark,preview.image)}};
}
