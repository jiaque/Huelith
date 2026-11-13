#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
namespace {
class PerspectivePreview final : public QWidget {
  public:
    QImage background, texture;
    QPolygonF corners;
    explicit PerspectivePreview(QImage image, QWidget* parent)
        : QWidget(parent), background(std::move(image)) {
        setMinimumSize(480, 320);
        setMouseTracking(true);
        const QRectF box(background.width() * .2, background.height() * .2, background.width() * .6,
                         background.height() * .6);
        corners = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
    }
    bool mapping(QTransform& matrix) const {
        if (texture.isNull())
            return false;
        double sign = 0;
        for (int i = 0; i < 4; ++i) {
            const auto a = corners[(i + 1) % 4] - corners[i],
                       b = corners[(i + 2) % 4] - corners[(i + 1) % 4];
            const double cross = a.x() * b.y() - a.y() * b.x();
            if (std::abs(cross) < 1 || (i && cross * sign <= 0))
                return false;
            sign = cross;
        }
        const QRectF box(QPointF(), texture.size());
        return QTransform::quadToQuad(
            {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()}, corners, matrix);
    }

  protected:
    QTransform view() const {
        const double scale =
            std::min(double(width()) / background.width(), double(height()) / background.height());
        return QTransform::fromScale(scale, scale) *
               QTransform::fromTranslate((width() - background.width() * scale) * .5,
                                         (height() - background.height() * scale) * .5);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#202428"));
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setTransform(view());
        painter.drawImage(0, 0, background);
        QTransform matrix;
        const bool valid = mapping(matrix);
        if (valid) {
            painter.save();
            painter.setTransform(matrix, true);
            painter.drawImage(0, 0, texture);
            painter.restore();
        }
        painter.setPen(QPen(valid ? QColor("#72b7f7") : QColor("#ff865b"), 0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(corners);
        if (valid)
            for (int i = 1; i < 10; ++i) {
                const double u = i / 10.;
                painter.drawLine(matrix.map(QPointF(texture.width() * u, 0)),
                                 matrix.map(QPointF(texture.width() * u, texture.height())));
                painter.drawLine(matrix.map(QPointF(0, texture.height() * u)),
                                 matrix.map(QPointF(texture.width(), texture.height() * u)));
            }
        painter.resetTransform();
        painter.setBrush(Qt::white);
        for (int i = 0; i < 4; ++i) {
            const auto p = view().map(corners[i]);
            painter.drawEllipse(p, 5, 5);
            painter.drawText(p + QPointF(8, -8), QString::number(i + 1));
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        for (int i = 0; i < 4; ++i)
            if (QLineF(view().map(corners[i]), event->position()).length() < 14) {
                drag_ = i;
                return;
            }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (drag_ < 0)
            return;
        auto p = view().inverted().map(event->position());
        p.setX(std::clamp(p.x(), 0., double(background.width())));
        p.setY(std::clamp(p.y(), 0., double(background.height())));
        corners[drag_] = p;
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        drag_ = -1;
    }

  private:
    int drag_{-1};
};
} // namespace
void EditorController::show_perspective() {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    QDialog dialog(window_);
    dialog.setWindowTitle(tr("消失点 · 透视平面贴图"));
    dialog.resize(
        QSize(1000, 760).boundedTo(window_->screen()->availableGeometry().size() - QSize(32, 72)));
    QVBoxLayout layout(&dialog);
    QLabel hint(tr("拖动四个角贴合照片中的平面，再载入图片或粘贴剪贴板图像。蓝色网格表示有效平面；"
                   "交叉或退化的平面不能应用。输出为独立图层，可撤销和继续变换。"));
    hint.setWordWrap(true);
    layout.addWidget(&hint);
    PerspectivePreview preview(
        editing::display_image(editing::render(c->project().state()), QColorSpace::SRgb), &dialog);
    layout.addWidget(&preview, 1);
    QHBoxLayout actions;
    QPushButton load(tr("载入贴图…")), paste(tr("粘贴图像"));
    actions.addWidget(&load);
    actions.addWidget(&paste);
    layout.addLayout(&actions);
    QLabel error;
    error.setWordWrap(true);
    layout.addWidget(&error);
    connect(&load, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getOpenFileName(
            &dialog, tr("贴图"), {}, "Images (*.png *.jpg *.jpeg *.tif *.tiff *.webp)");
        if (path.isEmpty())
            return;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (size.isEmpty() || qint64(size.width()) * size.height() > 64000000) {
            error.setText(tr("贴图不可读或超过 6400 万像素"));
            return;
        }
        const auto image = reader.read();
        if (image.isNull()) {
            error.setText(reader.errorString());
            return;
        }
        preview.texture = editing::display_image(editing::to_working(image), QColorSpace::SRgb);
        preview.update();
        error.clear();
    });
    connect(&paste, &QPushButton::clicked, &dialog, [&] {
        const auto image = QApplication::clipboard()->image();
        if (image.isNull() || qint64(image.width()) * image.height() > 64000000) {
            error.setText(tr("剪贴板没有图像或图像超过 6400 万像素"));
            return;
        }
        preview.texture = editing::display_image(editing::to_working(image), QColorSpace::SRgb);
        preview.update();
        error.clear();
    });
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    QTransform transform;
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!preview.mapping(transform)) {
            error.setText(tr("请载入贴图并调整为有效的四边形平面"));
            return;
        }
        dialog.accept();
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    editing::Layer layer;
    layer.name = tr("透视贴图");
    layer.kind = editing::LayerKind::source;
    layer.pixels = editing::to_working(preview.texture);
    layer.transform = transform;
    const auto id = layer.id;
    c->edit(tr("透视平面贴图"), [&](editing::State& state) { state.layers.push_back(layer); });
    c->active_layer = id;
    c->selected_layers = {id};
    refresh();
}
} // namespace huelith::desktop
