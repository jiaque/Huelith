#include "work_window.h"
#include "editor_controller.h"
#include "canvas_view.h"
#include "main_window.h"
#include "huelith/editing/photo_merge.h"
#include "huelith/editing/metadata.h"
#include <QPainter>
#include <QSignalBlocker>
#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <future>
#include <algorithm>

namespace huelith::desktop {
void EditorController::show_photo_merge(int mode) {
    using namespace editing;
    QDialog dialog(window_);
    dialog.setObjectName("photoMergeDialog");
    const auto title=mode==0?tr("合并到 HDR"):mode==1?tr("Photomerge 全景"):tr("景深合成");
    dialog.setWindowTitle(title);dialog.resize(920,750);
    QVBoxLayout layout(&dialog);
    QLabel explanation(mode==0?tr("选择曝光包围照片。优先从拍摄参数读取相对曝光 EV（越亮越大），缺失时请手动填写。结果保留线性高动态范围。"):mode==1?
        tr("选择具有重叠区域的照片。合成保留浮点颜色，自动对齐、调整接缝和重叠区曝光。"):tr("选择同一场景、不同焦点的照片，自动对齐并融合清晰区域。"));
    explanation.setWordWrap(true);layout.addWidget(&explanation);
    QTableWidget table(0,2);
    table.setObjectName("photoMergeSources");table.setHorizontalHeaderLabels({tr("照片"),tr("相对曝光 EV")});
    table.horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);table.setSelectionMode(QAbstractItemView::SingleSelection);
    table.setColumnHidden(1,mode!=0);layout.addWidget(&table);
    QHBoxLayout row;
    QPushButton add(tr("添加照片…")),remove(tr("移除")),up(tr("上移")),down(tr("下移"));
    add.setObjectName("photoMergeAdd");remove.setObjectName("photoMergeRemove");
    for(auto* button:{&add,&remove,&up,&down})row.addWidget(button);
    layout.addLayout(&row);
    QFormLayout form;
    QComboBox reference,projection,deghost;
    reference.setObjectName("photoMergeReference");projection.setObjectName("photoMergeProjection");deghost.setObjectName("photoMergeDeghost");
    projection.addItems({tr("球面"),tr("圆柱"),tr("透视")});deghost.addItems({tr("无"),tr("低"),tr("中"),tr("高")});deghost.setCurrentIndex(1);
    if(mode!=1)form.addRow(tr("参考照片"),&reference);
    if(mode==0)form.addRow(tr("移除重影"),&deghost);
    QCheckBox overlay(tr("显示去重影覆盖区域"));overlay.setObjectName("photoMergeDeghostOverlay");if(mode==0)form.addRow(&overlay);
    if(mode==1)form.addRow(tr("投影"),&projection);
    QCheckBox align(tr("自动对齐")),crop(tr("自动裁剪无覆盖区域"));
    align.setObjectName("photoMergeAlign");crop.setObjectName("photoMergeCrop");align.setChecked(true);crop.setChecked(true);
    if(mode!=1)form.addRow(&align);form.addRow(&crop);layout.addLayout(&form);
    QLabel preview;preview.setObjectName("photoMergePreview");preview.setAlignment(Qt::AlignCenter);preview.setMinimumHeight(260);
    layout.addWidget(&preview,1);
    QImage preview_image,preview_mask;
    const auto show_preview=[&]{if(preview_image.isNull())return;auto image=preview_image;
        if(overlay.isChecked()&&!preview_mask.isNull()){image=image.convertToFormat(QImage::Format_ARGB32);for(int y=0;y<image.height();++y)
            for(int x=0;x<image.width();++x)if(preview_mask.constScanLine(y)[x]){const auto color=image.pixelColor(x,y);image.setPixelColor(x,y,QColor((color.red()+255)/2,color.green()/2,color.blue()/2));}}
        preview.setPixmap(QPixmap::fromImage(image).scaled(preview.size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));};
    connect(&overlay,&QCheckBox::toggled,&dialog,show_preview);
    QLabel status;status.setObjectName("photoMergeStatus");status.setWordWrap(true);layout.addWidget(&status);
    QDialogButtonBox buttons(QDialogButtonBox::Cancel);
    auto* inspect=buttons.addButton(tr("预览"),QDialogButtonBox::ActionRole);inspect->setObjectName("photoMergeInspect");
    auto* apply=buttons.addButton(tr("合并"),QDialogButtonBox::AcceptRole);apply->setObjectName("photoMergeApply");layout.addWidget(&buttons);
    auto refresh_reference=[&] {
        const int previous=reference.currentIndex();reference.clear();
        for(int i=0;i<table.rowCount();++i)reference.addItem(table.item(i,0)->text());
        reference.setCurrentIndex(std::clamp(previous,0,std::max(0,table.rowCount()-1)));
    };
    auto add_path=[&](const QString& path,double ev) {
        if(table.rowCount()>=16)return;
        const int index=table.rowCount();table.insertRow(index);
        auto* item=new QTableWidgetItem(QFileInfo(path).fileName());item->setData(Qt::UserRole,path);item->setFlags(item->flags()&~Qt::ItemIsEditable);table.setItem(index,0,item);
        auto* value=new QDoubleSpinBox(&table);value->setRange(-20,20);value->setDecimals(2);value->setValue(ev);table.setCellWidget(index,1,value);
        if(mode==0){try{const auto metadata=read_photo_metadata(path);const double shutter=metadata["ExifIFD:ExposureTime"].toDouble(),aperture=metadata["ExifIFD:FNumber"].toDouble(),iso=metadata["ExifIFD:ISO"].toDouble();
            if(shutter>0&&aperture>0&&iso>0){item->setData(Qt::UserRole+1,std::log2(shutter*iso/(aperture*aperture)));value->setToolTip(tr("由快门、光圈、ISO 计算；可手动修正"));}
            else value->setToolTip(tr("缺少拍摄曝光参数，请手动填写"));}catch(const std::exception& e){value->setToolTip(QString::fromUtf8(e.what()));}}
        connect(value,&QDoubleSpinBox::valueChanged,&dialog,[item]{item->setData(Qt::UserRole+1,{});});
    };
    connect(&add,&QPushButton::clicked,&dialog,[&] {
        const auto files=QFileDialog::getOpenFileNames(&dialog,tr("选择图组"),{},tr("照片 (*.png *.jpg *.jpeg *.tif *.tiff *.dng *.cr3 *.cr2 *.crw *.nef *.nrw *.arw *.sr2 *.srf *.raf *.orf *.rw2)"));
        for(const auto& path:files)add_path(path,0);
        std::optional<double> base;
        for(int i=0;i<table.rowCount();++i)if(table.item(i,0)->data(Qt::UserRole+1).isValid()){
            const double ev=table.item(i,0)->data(Qt::UserRole+1).toDouble();if(!base)base=ev;
            auto* value=qobject_cast<QDoubleSpinBox*>(table.cellWidget(i,1));const QSignalBlocker blocker(value);value->setValue(ev-*base);}
        refresh_reference();
    });
    connect(&remove,&QPushButton::clicked,&dialog,[&]{if(table.currentRow()>=0)table.removeRow(table.currentRow());refresh_reference();});
    auto move=[&](int delta) {
        const int from=table.currentRow(),to=from+delta;if(from<0||to<0||to>=table.rowCount())return;
        const auto path=table.item(from,0)->data(Qt::UserRole);const auto name=table.item(from,0)->text();
        const double value=qobject_cast<QDoubleSpinBox*>(table.cellWidget(from,1))->value();
        table.item(from,0)->setData(Qt::UserRole,table.item(to,0)->data(Qt::UserRole));table.item(from,0)->setText(table.item(to,0)->text());
        qobject_cast<QDoubleSpinBox*>(table.cellWidget(from,1))->setValue(qobject_cast<QDoubleSpinBox*>(table.cellWidget(to,1))->value());
        table.item(to,0)->setData(Qt::UserRole,path);table.item(to,0)->setText(name);qobject_cast<QDoubleSpinBox*>(table.cellWidget(to,1))->setValue(value);
        table.selectRow(to);refresh_reference();
    };
    connect(&up,&QPushButton::clicked,&dialog,[&]{move(-1);});connect(&down,&QPushButton::clicked,&dialog,[&]{move(1);});
    std::atomic_bool cancelled{false};std::atomic_int completed{},total{};
    std::future<AiResult> job;std::optional<AiResult> result;bool full{};
    auto enabled=[&](bool value){for(auto* widget:std::initializer_list<QWidget*>{&table,&add,&remove,&up,&down,&reference,&projection,&deghost,&align,&crop,inspect,apply})widget->setEnabled(value);};
    auto start=[&](bool full_size) {
        if(job.valid())return;
        if(table.rowCount()<2){status.setText(tr("请至少选择两张照片"));return;}
        PhotoMergeOptions options;options.mode=PhotoMergeMode(mode);options.reference=mode==1?0:reference.currentIndex();options.align=align.isChecked();options.crop=crop.isChecked();
        options.deghost=deghost.currentIndex();options.projection=projection.currentIndex();options.preview_edge=full_size?0:1000;
        for(int i=0;i<table.rowCount();++i){options.paths.append(table.item(i,0)->data(Qt::UserRole).toString());options.exposure_ev.push_back(qobject_cast<QDoubleSpinBox*>(table.cellWidget(i,1))->value());}
        if(mode==0 && std::all_of(options.exposure_ev.begin(),options.exposure_ev.end(),[&](double ev){return ev==options.exposure_ev.front();})){
            status.setText(tr("HDR 图组需要不同曝光量。请根据拍摄设置填写每张照片的相对 EV。"));return;
        }
        full=full_size;cancelled=false;completed=0;total=0;enabled(false);dialog.setProperty("photoMergeRunning",true);dialog.setProperty("photoMergeReady",false);status.setText(tr("读取图组…"));
        job=std::async(std::launch::async,[&,options]{return merge_photos(options,&cancelled,[&](int done,int count){completed=done;total=count;});});
    };
    connect(inspect,&QPushButton::clicked,&dialog,[&]{start(false);});connect(apply,&QPushButton::clicked,&dialog,[&]{start(true);});
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QTimer timer;timer.setInterval(40);connect(&timer,&QTimer::timeout,&dialog,[&]{
        if(!job.valid())return;
        if(job.wait_for(std::chrono::seconds(0))!=std::future_status::ready){if(total>0)status.setText(tr("合成 %1 / %2；可以取消").arg(completed.load()).arg(total.load()));return;}
        enabled(true);dialog.setProperty("photoMergeRunning",false);
        try{auto output=job.get();if(full){result=std::move(output);dialog.accept();return;}
            preview_image=display_image(output.image);preview_mask=output.reference;show_preview();
            status.setText(tr("预览完成。完整合并使用原始分辨率。"));dialog.setProperty("photoMergeReady",true);
        }catch(const std::exception& error){status.setText(QString::fromUtf8(error.what()));}
    });timer.start();
    const int accepted=run_work_window(dialog, window_);cancelled=true;if(job.valid()){try{job.get();}catch(...) {}}
    if(accepted!=QDialog::Accepted||!result)return;
    State state;state.name=title;state.size=result->image.size();state.metadata=result->metadata["photoMetadata"].toObject();
    Layer layer;layer.name=title;layer.pixels=std::move(result->image);layer.source_metadata=state.metadata;state.layers.push_back(std::move(layer));
    CanvasSpec spec;spec.width=state.size.width();spec.height=state.size.height();spec.name=state.name.toStdString();
    if(!window_->create_document(spec))throw EditError(tr("无法创建合成文档"));canvas()->load_state(std::move(state),false);refresh();
}
}
