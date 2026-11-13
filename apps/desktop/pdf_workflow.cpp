#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/pdf_import.h"
#include "main_window.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QSpinBox>
#include <QThread>
#include <future>

namespace huelith::desktop {
bool EditorController::open_pdf(const QString& path, bool place) {
    QDialog settings(window_);
    settings.setWindowTitle(tr("导入 PDF 页面"));
    QFormLayout form(&settings);
    QSpinBox first, last, ppi;
    first.setRange(1, 10000);
    last.setRange(1, 10000);
    ppi.setRange(36, 1200);
    ppi.setValue(144);
    first.setObjectName("pdfFirst");
    last.setObjectName("pdfLast");
    ppi.setObjectName("pdfPpi");
    QLineEdit password;
    password.setEchoMode(QLineEdit::Password);
    form.addRow(tr("起始页"), &first);
    form.addRow(tr("结束页"), &last);
    form.addRow(tr("分辨率 ppi"), &ppi);
    form.addRow(tr("密码（如有）"), &password);
    QLabel note(
        tr("页面会栅格化为可编辑图像；一次最多 50 页。PDF 中的文字和矢量对象不会保留为独立对象。"));
    note.setWordWrap(true);
    note.setMaximumWidth(460);
    form.addRow(&note);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &settings, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &settings, &QDialog::reject);
    if (settings.exec() != QDialog::Accepted)
        return false;
    const QPointer<CanvasView> target(canvas());
    std::atomic_bool cancelled{};
    const auto secret = password.text();
    password.clear();
    auto job = std::async(std::launch::async,
                          [&, secret, a = first.value(), b = last.value(), dpi = ppi.value()] {
                              return editing::import_pdf_pages(path, secret, a, b, dpi, &cancelled);
                          });
    QProgressDialog progress(tr("正在读取 PDF 页面…"), tr("取消"), 0, 0, window_);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    while (job.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        QApplication::processEvents();
        if (progress.wasCanceled())
            cancelled = true;
        QThread::msleep(10);
    }
    if (cancelled) {
        try {
            (void)job.get();
        } catch (const std::exception&) {
        }
        return false;
    }
    const auto pages = job.get();
    progress.close();
    int number = first.value();
    for (const auto& image : pages.images) {
        editing::Layer layer;
        layer.kind = editing::LayerKind::source;
        layer.source_id = QUuid::createUuid();
        layer.name = QFileInfo(path).completeBaseName() + tr(" 第 %1 页").arg(number++);
        layer.pixels = editing::to_working(image);
        if (place && target)
            target->edit(tr("置入 PDF 页面"), [&](editing::State& state) {
                state.layers.push_back(layer);
                target->active_layer = layer.id;
            });
        else {
            CanvasSpec spec;
            spec.name = layer.name.toStdString();
            spec.width = image.width();
            spec.height = image.height();
            spec.resolution_ppi = ppi.value();
            if (!window_->create_document(spec))
                throw editing::EditError(tr("无法创建 PDF 页面文档"));
            canvas()->edit(tr("导入 PDF"), [&](editing::State& state) { state.layers = {layer}; });
            canvas()->active_layer = layer.id;
        }
    }
    update_recent(path);
    refresh();
    return true;
}
} // namespace huelith::desktop
