#pragma once
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
namespace huelith::desktop {
class ColorBandSlider final : public QSlider {
  public:
    ColorBandSlider(int channel, int band, QWidget* parent)
        : QSlider(Qt::Horizontal, parent), channel_(channel), band_(band) {
        setRange(-100, 100);
        setFixedHeight(16);
        setFocusPolicy(Qt::StrongFocus);
        setTracking(true);
        setToolTip(tr("拖动或点击调整；方向键微调；双击归零"));
    }
    void set_default(int value) {
        default_ = value;
        setToolTip(tr("拖动或点击调整；方向键微调；双击恢复默认值"));
    }
    void set_colors(QColor low, QColor high) {
        low_ = low;
        high_ = high;
        update();
    }
    std::function<void(bool)> auxiliary_preview;

  protected:
    bool event(QEvent* event) override {
        if (auxiliary_preview) {
            if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove) {
                const auto* mouse = static_cast<QMouseEvent*>(event);
                auxiliary_preview(mouse->buttons().testFlag(Qt::LeftButton) &&
                                  mouse->modifiers().testFlag(Qt::AltModifier));
            } else if (event->type() == QEvent::MouseButtonRelease ||
                       event->type() == QEvent::FocusOut)
                auxiliary_preview(false);
            else if (event->type() == QEvent::KeyRelease &&
                     static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt)
                auxiliary_preview(false);
        }
        return QSlider::event(event);
    }
    void paintEvent(QPaintEvent*) override {
        static constexpr std::array<double, 8> hues{0, 30, 60, 120, 180, 240, 270, 300};
        const double hue = hues[band_] / 360;
        const auto color = [](double h, double s, double v) {
            return QColor::fromHsvF(std::fmod(h + 1, 1.), s, v);
        };
        QLinearGradient gradient(6, 0, width() - 6, 0);
        if (low_.isValid()) {
            gradient.setColorAt(0, low_);
            gradient.setColorAt(1, high_);
        } else if (channel_ < 0) {
            gradient.setColorAt(0, QColor("#8a8a8a"));
            gradient.setColorAt(1, QColor("#8a8a8a"));
        } else if (channel_ == 0) {
            gradient.setColorAt(0, color(hue - .08, .55, .65));
            gradient.setColorAt(.5, color(hue, .55, .65));
            gradient.setColorAt(1, color(hue + .08, .55, .65));
        } else if (channel_ == 1) {
            gradient.setColorAt(0, QColor("#737373"));
            gradient.setColorAt(1, color(hue, .9, .9));
        } else {
            gradient.setColorAt(0, color(hue, .6, .16));
            gradient.setColorAt(.5, color(hue, .65, .65));
            gradient.setColorAt(1, color(hue, .2, 1));
        }
        QPainter painter(this);
        painter.fillRect(QRectF(6, 6, width() - 12, 3), gradient);
        const double x = 6 + (width() - 12.) * (value() - minimum()) / (maximum() - minimum());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#303030"), .8));
        painter.setBrush(isEnabled() ? QColor("#cccccc") : QColor("#707070"));
        painter.drawPolygon(QPolygonF{QPointF(x, 3), QPointF(x + 5, 11), QPointF(x - 5, 11)});
        if (hasFocus()) {
            painter.setPen(QPen(QColor("#999999"), 1, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        setFocus(Qt::MouseFocusReason);
        setSliderDown(true);
        set_at(event->position().x());
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (isSliderDown())
            set_at(event->position().x());
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !isSliderDown())
            return;
        set_at(event->position().x());
        setSliderDown(false);
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        setSliderDown(true);
        setValue(default_);
        setSliderDown(false);
        event->accept();
    }

  private:
    void set_at(double x) {
        const double position = std::clamp((x - 6) / std::max(1., width() - 12.), 0., 1.);
        setValue(qRound(minimum() + (maximum() - minimum()) * position));
    }
    int channel_;
    int band_;
    int default_{};
    QColor low_, high_;
};

} // namespace huelith::desktop
