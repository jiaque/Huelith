#include "huelith/editing/image_storage.h"
#include "huelith/editing/project.h"
#include "huelith/editing/export_queue.h"
#include "huelith/editing/photo_operations.h"
#include "huelith/editing/tiff_io.h"
#include "main_window.h"
#include "canvas_view.h"
#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QTimer>
#include <QAction>
#include <QCryptographicHash>
#include <cstring>
#ifdef _WIN32
#include <qt_windows.h>
#endif

namespace {
QByteArray digest(const QImage& image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y=0;y<image.height();++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)),image.bytesPerLine()));
    return hash.result().toHex();
}
}
QJsonObject large_image_probe(const QJsonObject& request) {
    using namespace huelith::editing;
    QElapsedTimer timer;timer.start();
    State state;state.name="Large image acceptance";
    state.size={request["width"].toInt(8192),request["height"].toInt(8192)};
    Layer layer;layer.name="Pixels";layer.kind=LayerKind::pixels;
    layer.pixels=allocate_image(state.size,QImage::Format_RGBA32FPx4_Premultiplied);
    layer.pixels.setColorSpace(working_space());
    quint32 random=1234567;
    const bool noise=request["noise"].toBool();
    for(int y=0;y<state.size.height();++y) {
        auto* row=reinterpret_cast<float*>(layer.pixels.scanLine(y));
        for(int x=0;x<state.size.width();++x) {
            for(int c=0;c<3;++c) {
                random^=random<<13;random^=random>>17;random^=random<<5;
                row[x*4+c]=noise?float(random&0xffffff)/16777216.F:float((x+y*3+c*43)%256)/512.F;
            }
            row[x*4+3]=1.F;
        }
    }
    state.layers.push_back(std::move(layer));
    Project project(std::move(state));
    project.edit("Exposure",[](State& value){value.layers.front().exposure=1;});
    auto image=render(project.state());
    const auto* sample=reinterpret_cast<const float*>(image.constScanLine(image.height()/2))+4*(image.width()/2);
    const auto* input=reinterpret_cast<const float*>(project.state().layers.front().pixels.constScanLine(image.height()/2))+4*(image.width()/2);
    if(std::abs(sample[0]-input[0]*2)>1e-6)throw EditError("large tiled exposure mismatch");
    image={};
    if(!project.undo() || project.state().layers.front().exposure!=0 || !project.redo())throw EditError("large undo/redo mismatch");
    // Exercise the real writable pixel-edit path, with a small selected patch.
    const auto before=digest(project.state().layers.front().pixels);
    project.edit("Selected paint",[](State& value) {
        value.selection=allocate_image(value.size,QImage::Format_Grayscale8);value.selection.fill(0);
        for(int y=510;y<515;++y)for(int x=510;x<515;++x)value.selection.scanLine(y)[x]=255;
        fill_color(value,value.layers.front().id,{512,512},Qt::red,255,false,false);
        value.selection={};
    });
    const auto painted=digest(project.state().layers.front().pixels);
    if(painted==before || !project.undo() || digest(project.state().layers.front().pixels)!=before || !project.redo())
        throw EditError("large pixel edit history mismatch");
    const auto path=request["project"].toString();
    save_project(project.snapshot(),path);
    auto restored=load_project(path);
    if(restored.size!=project.state().size || digest(restored.layers.front().pixels)!=painted)
        throw EditError("large project pixel mismatch");
    // Atomic failed save must leave the already saved project readable.
    bool failed=false;try{save_project(project.snapshot(),path,true);}catch(const EditError&){failed=true;}
    if(!failed)throw EditError("injected save did not fail");
    QJsonObject result{{"width",restored.size.width()},{"height",restored.size.height()},
        {"bytes",double(restored.layers.front().pixels.sizeInBytes())},{"fileBytes",double(QFileInfo(path).size())},
        {"sha256",QString::fromLatin1(painted)},{"undoRedo",true},{"pixelEdit",true},{"atomicFailure",true}};
    if(request.contains("export")) {
        auto composed=render(restored);ExportOptions options;options.bits=16;options.space=QColorSpace::SRgb;
        const auto output=prepare_export_image(composed,options,300);composed={};
        const std::atomic_bool cancelled{false};
        write_tiff(output,request["export"].toString(),16,cancelled,300);
        result["exportBytes"]=double(QFileInfo(request["export"].toString()).size());
    }
    result["elapsedMs"]=double(timer.elapsed());
    return result;
}
QJsonObject portrait_session_probe(huelith::desktop::MainWindow*,const QJsonObject&);
QJsonObject work_window_probe(huelith::desktop::MainWindow* window,const QJsonObject& request) {
    using namespace huelith::editing;
    if(!window)throw EditError("missing main window");
    const auto kind=request["kind"].toString("raw");
    if(kind=="portraitSession")return portrait_session_probe(window,request);
    const auto name=kind=="raw"?"rawDialog":kind=="enhance"?"enhancementDialog":kind=="portrait"?"portraitDialog":"photoMergeDialog";
    if(kind=="portrait")window->open_document(request["source"].toString());
    if(kind=="enhance" && !window->active_canvas()) {
        huelith::CanvasSpec spec;spec.width=64;spec.height=48;spec.name="Enhancement source";
        if(!window->create_document(spec))throw EditError("cannot create enhancement source");
    }
    int stage{};QJsonObject result;QElapsedTimer elapsed;elapsed.start();QTimer timer;timer.setInterval(100);
    QObject::connect(&timer,&QTimer::timeout,window,[&] {
        if(auto* error=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){result["uiError"]=error->text();error->accept();return;}
        QDialog* dialog{};
        for(auto* candidate:QApplication::topLevelWidgets())
            if(candidate->objectName()==name)dialog=qobject_cast<QDialog*>(candidate);
        if(!dialog)return;
        if(elapsed.elapsed()>30000){result["timeout"]=true;dialog->reject();return;}
        if(stage==0) {
            result["nonModal"]=dialog->windowModality()==Qt::NonModal && !QApplication::activeModalWidget();
            result["unowned"]=dialog->parentWidget()==nullptr;
            result["mainEnabled"]=window->isEnabled();result["windowIcon"]=!dialog->windowIcon().isNull();
#ifdef _WIN32
            result["nativeIcon"]=SendMessageW(reinterpret_cast<HWND>(dialog->winId()),WM_GETICON,ICON_SMALL,0)!=0 || SendMessageW(reinterpret_cast<HWND>(dialog->winId()),WM_GETICON,ICON_BIG,0)!=0;
#endif
            dialog->showMinimized();++stage;
        } else if(stage==1) {
            result["childMinimized"]=dialog->isMinimized();result["mainNotMinimized"]=!window->isMinimized();
            huelith::CanvasSpec spec;spec.width=64;spec.height=48;spec.name="While RAW is minimized";
            const bool created=window->create_document(spec);
            if(created)window->active_canvas()->edit("Concurrent edit",[](State& s){s.name="Edited while RAW minimized";});
            result["mainEdited"]=created && window->active_canvas()->project().dirty();
            dialog->showNormal();++stage;
        } else if(stage==2) {
            result["childRestored"]=!dialog->isMinimized();
            window->showMinimized();++stage;
        } else if(stage==3) {
            result["mainMinimizedIndependently"]=window->isMinimized() && !dialog->isMinimized();
            window->showNormal();
            if(request.contains("capture"))dialog->grab().save(request["capture"].toString());
            dialog->reject();++stage;
        }
    });
    timer.start();
    if(kind=="raw")window->open_document(request["source"].toString());
    else {
        const auto text=kind=="enhance"?QStringLiteral("RGB AI 降噪…"):kind=="portrait"?QStringLiteral("人像精修…"):QStringLiteral("Photomerge 全景…");
        QAction* action{};for(auto* a:window->findChildren<QAction*>())if(a->text()==text)action=a;
        if(kind=="enhance")action=window->findChild<QAction*>("imageEnhancement1");
        if(!action)throw EditError("missing work window action");
        action->trigger();
    }
    timer.stop();
    result["stages"]=stage;result["mainVisible"]=window->isVisible();return result;
}
