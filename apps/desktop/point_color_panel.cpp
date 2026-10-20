#include "point_color_panel.h"
#include "color_band_slider.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
namespace {
QColor sample_color(const editing::PointColor& sample) {
    return QColor::fromHslF(std::fmod(sample.sample[0], 1.), sample.sample[1], sample.sample[2]);
}
QColor shifted_color(const editing::PointColor& sample, double hue, double saturation,
                     double luminance) {
    const double s = saturation / 100, l = luminance / 100;
    return QColor::fromHslF(
        std::fmod(sample.sample[0] + hue / 600 + 1, 1.),
        std::clamp(sample.sample[1] + s * (s > 0 ? 1 - sample.sample[1] : sample.sample[1]), 0.,
                   1.),
        std::clamp(sample.sample[2] + l * (l > 0 ? 1 - sample.sample[2] : sample.sample[2]), 0.,
                   1.));
}
} // namespace
class PointField final : public QWidget {
  public:
    explicit PointField(bool luminance, QWidget* parent) : QWidget(parent), luminance_(luminance) {
        setMinimumSize(luminance ? 18 : 140, 110);
        if (luminance)
            setFixedWidth(20);
        setFocusPolicy(Qt::StrongFocus);
    }
    editing::PointColor color;
    std::function<void(double, double)> changed;

  protected:
    void paintEvent(QPaintEvent*) override {
        QImage field(128, 64, QImage::Format_RGB32);
        for (int y = 0; y < field.height(); ++y)
            for (int x = 0; x < field.width(); ++x)
                field.setPixelColor(
                    x, y,
                    shifted_color(color, luminance_ ? color.shift[0] : x * 200. / 127 - 100,
                                  luminance_ ? color.shift[1] : 100 - y * 200. / 63,
                                  luminance_ ? 100 - y * 200. / 63 : color.shift[2]));
        QPainter painter(this);
        painter.drawImage(rect(), field);
        const QPointF point(luminance_ ? width() / 2. : (color.shift[0] + 100) / 200 * width(),
                            (100 - color.shift[luminance_ ? 2 : 1]) / 200 * height());
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::black, 3));
        painter.drawEllipse(point, 4, 4);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(point, 4, 4);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            setFocus();
            set_at(event->position());
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons().testFlag(Qt::LeftButton))
            set_at(event->position());
    }
    void keyPressEvent(QKeyEvent* event) override {
        double x = color.shift[0], y = color.shift[luminance_ ? 2 : 1];
        if (event->key() == Qt::Key_Left)
            --x;
        else if (event->key() == Qt::Key_Right)
            ++x;
        else if (event->key() == Qt::Key_Up)
            ++y;
        else if (event->key() == Qt::Key_Down)
            --y;
        else {
            QWidget::keyPressEvent(event);
            return;
        }
        if (changed)
            changed(std::clamp(x, -100., 100.), std::clamp(y, -100., 100.));
    }

  private:
    void set_at(QPointF point) {
        if (changed)
            changed(std::clamp(point.x() / width() * 200 - 100, -100., 100.),
                    std::clamp(100 - point.y() / height() * 200, -100., 100.));
    }
    bool luminance_;
};
class PointRange final : public QWidget {
  public:
    PointRange(int channel, QWidget* parent) : QWidget(parent), channel_(channel) {
        setMinimumHeight(42);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(tr("拖动四个端点调整范围与过渡；左右键微调，Tab切换端点"));
    }
    std::array<double, 4> values{};
    editing::PointColor color;
    std::function<void(std::array<double, 4>)> changed;

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QLinearGradient gradient(6, 0, width() - 6, 0);
        for (int i = 0; i <= 24; ++i) {
            const double t = i / 24.;
            gradient.setColorAt(
                t, QColor::fromHslF(
                       channel_ == 0 ? std::fmod(color.sample[0] + t + .5, 1.) : color.sample[0],
                       channel_ == 1 ? t : std::max(.3, color.sample[1]), channel_ == 2 ? t : .5));
        }
        painter.fillRect(QRectF(6, 16, width() - 12, 14), gradient);
        painter.fillRect(QRectF(6, 16, (values[0] + (channel_ == 0 ? .5 : 0)) * (width() - 12), 14),
                         QColor(0, 0, 0, 100));
        for (int i = 0; i < 4; ++i) {
            const double x = 6 + (values[i] + (channel_ == 0 ? .5 : 0)) * (width() - 12);
            painter.setPen(QPen(i == active_ && hasFocus() ? QColor("#69beff") : Qt::white, 1));
            painter.drawLine(QPointF(x, 13), QPointF(x, 34));
            painter.setBrush(i == active_ ? Qt::white : QColor("#999999"));
            const double y = i % 2 == 0 ? 13 : 34;
            painter.drawPolygon(QPolygonF{QPointF(x, y), QPointF(x - 4, y + (i % 2 == 0 ? -5 : 5)),
                                          QPointF(x + 4, y + (i % 2 == 0 ? -5 : 5))});
        }
        painter.setPen(QColor("#b5b5b5"));
        painter.drawText(QRect(6, 0, width() - 12, 12), Qt::AlignLeft,
                         QString("%1 / %2     —     %3 / %4")
                             .arg(values[0] * 100, 0, 'f', 0)
                             .arg(values[1] * 100, 0, 'f', 0)
                             .arg(values[2] * 100, 0, 'f', 0)
                             .arg(values[3] * 100, 0, 'f', 0));
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        setFocus();
        const double value = normalized(event->position().x());
        active_ = 0;
        for (int i = 1; i < 4; ++i)
            if (std::abs(values[i] - value) < std::abs(values[active_] - value))
                active_ = i;
        set_at(value);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons().testFlag(Qt::LeftButton))
            set_at(normalized(event->position().x()));
    }
    bool focusNextPrevChild(bool next) override {
        if ((next && active_ < 3) || (!next && active_ > 0)) {
            active_ += next ? 1 : -1;
            update();
            return true;
        }
        return QWidget::focusNextPrevChild(next);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
            set_at(values[active_] + (event->key() == Qt::Key_Left ? -.005 : .005));
        else
            QWidget::keyPressEvent(event);
    }

  private:
    double normalized(double x) const {
        return std::clamp((x - 6) / std::max(1., width() - 12.), 0., 1.) - (channel_ == 0 ? .5 : 0);
    }
    void set_at(double value) {
        values[active_] =
            std::clamp(value, active_ == 0 ? (channel_ == 0 ? -.5 : 0.) : values[active_ - 1],
                       active_ == 3 ? (channel_ == 0 ? .5 : 1.) : values[active_ + 1]);
        if (changed)
            changed(values);
        update();
    }
    int channel_, active_{};
};
PointColorPanel::PointColorPanel(editing::ToneSettings& settings, QWidget* parent)
    : QWidget(parent), settings_(settings) {
    setObjectName("rawPointColorPanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto* top = new QHBoxLayout;
    picker_ = new QToolButton(this);
    picker_->setText(tr("取样"));
    picker_->setCheckable(true);
    picker_->setObjectName("rawPointPicker");
    picker_->setToolTip(tr("点击照片选择颜色；Esc取消"));
    top->addWidget(picker_);
    swatches_ = new QTabBar(this);
    swatches_->setObjectName("rawPointSwatches");
    swatches_->setTabsClosable(true);
    swatches_->setExpanding(false);
    swatches_->setDrawBase(false);
    top->addWidget(swatches_, 1);
    layout->addLayout(top);
    status_ = new QLabel(tr("用吸管在照片上选择颜色，最多8个样本。"), this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    controls_ = new QWidget(this);
    auto* box = new QVBoxLayout(controls_);
    box->setContentsMargins(0, 0, 0, 0);
    auto* fields = new QHBoxLayout;
    field_ = new PointField(false, this);
    field_->setObjectName("rawPointField");
    light_ = new PointField(true, this);
    light_->setObjectName("rawPointLightField");
    fields->addWidget(field_, 1);
    fields->addWidget(light_);
    box->addLayout(fields);
    comparison_ = new QLabel(this);
    comparison_->setFixedHeight(16);
    comparison_->setObjectName("rawPointBeforeAfter");
    box->addWidget(comparison_);
    const QStringList labels{tr("色相"), tr("饱和度"), tr("明度")};
    const auto number = [&](const QString& name, const QString& label, int low, int high,
                            std::function<void(double)> assign) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label, this));
        row->addStretch();
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(name);
        spin->setRange(low, high);
        spin->setDecimals(0);
        spin->setFixedWidth(58);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        row->addWidget(spin);
        box->addLayout(row);
        auto* slider = new ColorBandSlider(-1, 0, this);
        slider->setObjectName(name + "Slider");
        slider->setRange(low, high);
        slider->set_default(low == 0 ? 50 : 0);
        box->addWidget(slider);
        connect(slider, &QSlider::valueChanged, spin, [spin](int value) { spin->setValue(value); });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, assign](double value) {
            const QSignalBlocker blocker(slider);
            slider->setValue(qRound(value));
            assign(value);
            notify();
        });
        return spin;
    };
    for (int i = 0; i < 3; ++i)
        shifts_[i] = number(QString("rawPointShift%1").arg(i), labels[i], -100, 100,
                            [this, i](double value) {
                                if (auto* c = current())
                                    c->shift[i] = value;
                            });
    amount_ = number("rawPointRange", tr("范围"), 0, 100, [this](double value) {
        if (auto* c = current())
            c->range = value;
    });
    visualize_ = new QCheckBox(tr("可视化范围"), this);
    visualize_->setObjectName("rawPointVisualize");
    box->addWidget(visualize_);
    for (int i = 0; i < 3; ++i) {
        box->addWidget(new QLabel(labels[i] + tr("范围"), this));
        ranges_[i] = new PointRange(i, this);
        ranges_[i]->setObjectName(QString("rawPointRange%1").arg(i));
        box->addWidget(ranges_[i]);
        ranges_[i]->changed = [this, i](auto values) {
            if (auto* c = current()) {
                (i == 0   ? c->hue_range
                 : i == 1 ? c->saturation_range
                          : c->luminance_range) = values;
                notify();
            }
        };
    }
    layout->addWidget(controls_);
    connect(picker_, &QToolButton::toggled, this, [this] {
        if (preview_changed)
            preview_changed();
    });
    connect(visualize_, &QCheckBox::toggled, this, [this] {
        if (preview_changed)
            preview_changed();
    });
    connect(swatches_, &QTabBar::currentChanged, this, [this] {
        refresh();
        if (preview_changed)
            preview_changed();
    });
    connect(swatches_, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (index >= 0 && index < int(settings_.point_colors.size())) {
            settings_.point_colors.erase(settings_.point_colors.begin() + index);
            notify();
        }
    });
    field_->changed = [this](double x, double y) {
        if (auto* c = current()) {
            c->shift[0] = x;
            c->shift[1] = y;
            notify();
        }
    };
    light_->changed = [this](double, double y) {
        if (auto* c = current()) {
            c->shift[2] = y;
            notify();
        }
    };
    refresh();
}
editing::PointColor* PointColorPanel::current() {
    const int i = swatches_->currentIndex();
    return i >= 0 && i < int(settings_.point_colors.size())
               ? &settings_.point_colors[std::size_t(i)]
               : nullptr;
}
void PointColorPanel::notify() {
    refresh();
    if (changed)
        changed();
}
void PointColorPanel::refresh() {
    const QSignalBlocker tabs_blocker(swatches_);
    while (swatches_->count() > int(settings_.point_colors.size()))
        swatches_->removeTab(swatches_->count() - 1);
    while (swatches_->count() < int(settings_.point_colors.size()))
        swatches_->addTab(QString{});
    for (int i = 0; i < swatches_->count(); ++i) {
        QPixmap icon(18, 18);
        icon.fill(sample_color(settings_.point_colors[std::size_t(i)]));
        swatches_->setTabIcon(i, QIcon(icon));
        swatches_->setTabToolTip(i, tr("颜色样本 %1").arg(i + 1));
    }
    const auto* c = current();
    controls_->setEnabled(c != nullptr);
    if (!c)
        return;
    status_->setText(tr("调整当前样本；范围端点控制选中颜色及柔和过渡。"));
    field_->color = light_->color = *c;
    field_->update();
    light_->update();
    QPixmap comparison(256, 16);
    comparison.fill(sample_color(*c));
    {
        QPainter p(&comparison);
        p.fillRect(128, 0, 128, 16, shifted_color(*c, c->shift[0], c->shift[1], c->shift[2]));
    }
    comparison_->setPixmap(comparison);
    comparison_->setScaledContents(true);
    for (int i = 0; i < 3; ++i) {
        const QSignalBlocker block(shifts_[i]);
        shifts_[i]->setValue(c->shift[i]);
        auto* slider = findChild<QSlider*>(QString("rawPointShift%1Slider").arg(i));
        const QSignalBlocker slider_block(slider);
        slider->setValue(qRound(c->shift[i]));
        const auto low = shifted_color(*c, i == 0 ? -100 : c->shift[0], i == 1 ? -100 : c->shift[1],
                                       i == 2 ? -100 : c->shift[2]);
        const auto high = shifted_color(*c, i == 0 ? 100 : c->shift[0], i == 1 ? 100 : c->shift[1],
                                        i == 2 ? 100 : c->shift[2]);
        static_cast<ColorBandSlider*>(slider)->set_colors(low, high);
        ranges_[i]->color = *c;
        ranges_[i]->values = i == 0   ? c->hue_range
                             : i == 1 ? c->saturation_range
                                      : c->luminance_range;
        ranges_[i]->update();
    }
    const QSignalBlocker amount_block(amount_);
    amount_->setValue(c->range);
    auto* slider = findChild<QSlider*>("rawPointRangeSlider");
    const QSignalBlocker block(slider);
    slider->setValue(qRound(c->range));
}
bool PointColorPanel::sample(QColor color) {
    if (settings_.point_colors.size() >= 8) {
        status_->setText(tr("已达到8个样本，请先删除不需要的色块。"));
        return false;
    }
    settings_.point_colors.push_back(editing::sample_point_color(color));
    refresh();
    swatches_->setCurrentIndex(int(settings_.point_colors.size()) - 1);
    picker_->setChecked(false);
    notify();
    return true;
}
bool PointColorPanel::sampling() const {
    return picker_->isChecked();
}
void PointColorPanel::stop_sampling() {
    picker_->setChecked(false);
}
std::optional<editing::PointColor> PointColorPanel::visualized() const {
    const int i = swatches_->currentIndex();
    if (!visualize_->isChecked() || i < 0 || i >= int(settings_.point_colors.size()))
        return {};
    return settings_.point_colors[std::size_t(i)];
}
} // namespace huelith::desktop
