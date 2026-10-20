#include "raw_dialog.h"
#include <QAction>
#include <QComboBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <algorithm>
#include <array>
#include <cmath>

namespace huelith::desktop {
void RawDialog::setup_inspection(QWidget* parent) {
    auto* overlay = new QHBoxLayout(histogram_);
    overlay->setContentsMargins(0, 0, 0, 0);
    const auto button = [&](const char* name, const QString& tip) {
        auto* control = new QToolButton(parent);
        control->setObjectName(name);
        control->setText(QString::fromUtf8("▲"));
        control->setCheckable(true);
        control->setFixedSize(20, 20);
        control->setToolTip(tip);
        control->setAccessibleName(tip);
        control->installEventFilter(this);
        connect(control, &QToolButton::toggled, this, &RawDialog::refresh_inspection);
        return control;
    };
    shadow_clipping_ = button("rawShadowClipping", tr("阴影溢出提示（U）；悬停临时显示"));
    highlight_clipping_ = button("rawHighlightClipping", tr("高光溢出提示（O）；悬停临时显示"));
    overlay->addWidget(shadow_clipping_, 0, Qt::AlignTop);
    overlay->addStretch();
    overlay->addWidget(highlight_clipping_, 0, Qt::AlignTop);
    color_readout_ = new QLabel(tr("预览 sRGB：将指针移到图像上查看颜色"), parent);
    color_readout_->setObjectName("rawColorReadout");
    histogram_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(histogram_, &QLabel::customContextMenuRequested, this, [this](QPoint point) {
        QMenu menu(this);
        auto* lab = menu.addAction(tr("显示 Lab 颜色读数"));
        lab->setCheckable(true);
        lab->setChecked(lab_readout_);
        connect(lab, &QAction::toggled, this, [this](bool enabled) { lab_readout_ = enabled; });
        for (auto* control : {shadow_clipping_, highlight_clipping_}) {
            auto* action = menu.addAction(control->toolTip());
            action->setCheckable(true);
            action->setChecked(control->isChecked());
            connect(action, &QAction::toggled, control, &QToolButton::setChecked);
        }
        menu.exec(histogram_->mapToGlobal(point));
    });
}
void RawDialog::refresh_inspection() {
    if (!preview_item_ || displayed_preview_.isNull())
        return;
    auto image = healing_visualization(displayed_preview_).convertToFormat(QImage::Format_ARGB32);
    const bool shadows =
        shadow_clipping_->isChecked() || shadow_clipping_->property("hovering").toBool();
    const bool highlights =
        highlight_clipping_->isChecked() || highlight_clipping_->property("hovering").toBool();
    qint64 shadow_count{}, highlight_count{};
    auto sample = (inspection_source_.isNull() ? displayed_preview_ : inspection_source_)
                      .convertToFormat(QImage::Format_ARGB32);
    if (sample.size() != image.size())
        sample = sample.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    for (int y = 0; y < image.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        const auto* input = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const auto color = input[x];
            if (!qAlpha(color))
                continue;
            const int maximum = std::max({qRed(color), qGreen(color), qBlue(color)});
            const bool bright = maximum == 255;
            const bool dark = maximum == 0;
            if (bright)
                ++highlight_count;
            if (dark)
                ++shadow_count;
            if (highlights && bright)
                row[x] = qRgb(255, 0, 0);
            else if (shadows && dark)
                row[x] = qRgb(0, 0, 255);
        }
    }
    setProperty("highlightClippedPixels", highlight_count);
    setProperty("shadowClippedPixels", shadow_count);
    for (auto [control, count] : {std::pair{shadow_clipping_, shadow_count},
                                  std::pair{highlight_clipping_, highlight_count}}) {
        if (control->property("hasClipping").isValid() &&
            control->property("hasClipping").toBool() == (count > 0))
            continue;
        control->setProperty("hasClipping", count > 0);
        QPixmap icon(28, 28);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(count ? Qt::white : QColor("#555555"));
        painter.drawPolygon(QPolygonF{QPointF(14, 3), QPointF(26, 25), QPointF(2, 25)});
        painter.end();
        icon.setDevicePixelRatio(2);
        control->setText({});
        control->setIcon(QIcon(icon));
    }
    preview_item_->setPixmap(QPixmap::fromImage(image));
}
void RawDialog::color_readout(QPointF scene_point) {
    if (inspection_source_.isNull() || source_size_.isEmpty())
        return;
    const QPoint point(
        int(std::floor(scene_point.x() / scene_->sceneRect().width() * inspection_source_.width())),
        int(std::floor(scene_point.y() / scene_->sceneRect().height() *
                       inspection_source_.height())));
    if (!inspection_source_.rect().contains(point)) {
        color_readout_->setText(tr("预览 sRGB：—"));
        return;
    }
    const auto color = inspection_source_.pixelColor(point);
    if (!lab_readout_) {
        color_readout_->setText(tr("R %1   G %2   B %3  （预览 sRGB）")
                                    .arg(color.red())
                                    .arg(color.green())
                                    .arg(color.blue()));
        return;
    }
    const auto decode = [](double c) {
        return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
    };
    const double r = decode(color.redF()), g = decode(color.greenF()), b = decode(color.blueF());
    const auto f = [](double v) {
        return v > .008856451679 ? std::cbrt(v) : v * 7.787037037 + 16. / 116;
    };
    const double x = f((.4360747 * r + .3850649 * g + .1430804 * b) / .96422);
    const double y = f(.2225045 * r + .7168786 * g + .0606169 * b);
    const double z = f((.0139322 * r + .0971045 * g + .7141733 * b) / .82521);
    color_readout_->setText(tr("L %1   a %2   b %3")
                                .arg(116 * y - 16, 0, 'f', 1)
                                .arg(500 * (x - y), 0, 'f', 1)
                                .arg(200 * (y - z), 0, 'f', 1));
}
} // namespace huelith::desktop
