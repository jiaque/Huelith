#include "tone_panel.h"
#include "color_band_slider.h"
#include "curve_panel.h"
#include "point_color_panel.h"
#include "raw_section_header.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace huelith::desktop {
namespace {
class HslMixer final : public QWidget {
  public:
    HslMixer(editing::ToneSettings& settings, std::function<void()> changed, QWidget* parent)
        : QWidget(parent), settings_(settings), changed_(std::move(changed)) {
        setObjectName("rawHslMixer");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        auto* adjust = new QHBoxLayout;
        adjust->addWidget(new QLabel(tr("调整"), this));
        mode_ = new QComboBox(this);
        mode_->setObjectName("rawHslMode");
        mode_->addItems({tr("HSL"), tr("颜色")});
        adjust->addWidget(mode_, 1);
        auto* target = new QToolButton(this);
        target->setObjectName("rawHslTarget");
        target->setText(QStringLiteral("◎"));
        target->setCheckable(true);
        target->setToolTip(tr("目标调整：在图像上按住并上下拖动调整对应颜色"));
        adjust->addWidget(target);
        layout->addLayout(adjust);
        channels_ = new QTabBar(this);
        channels_->setObjectName("rawHslTabs");
        colors_ = new QTabBar(this);
        colors_->setObjectName("rawHslColors");
        for (auto* tabs : {channels_, colors_}) {
            tabs->setDrawBase(false);
            tabs->setExpanding(false);
            tabs->setElideMode(Qt::ElideNone);
            tabs->setStyleSheet(
                "QTabBar::tab { background:transparent; color:#aeb2b6; border:0; "
                "border-bottom:2px solid transparent; padding:5px 9px; } "
                "QTabBar::tab:selected { color:#ededed; border-bottom-color:#cccccc; }");
        }
        const QStringList channel_names{tr("色相"), tr("饱和度"), tr("明度")};
        const QStringList names{tr("红色"),   tr("橙色"), tr("黄色"), tr("绿色"),
                                tr("水绿色"), tr("蓝色"), tr("紫色"), tr("洋红色")};
        for (const auto& name : channel_names)
            channels_->addTab(name);
        channels_->addTab(tr("全部"));
        const QStringList swatches{"#d35b65", "#dc8b48", "#c5b952", "#66a661",
                                   "#52aca9", "#638ccd", "#9973bc", "#bc6caa"};
        for (int i = 0; i < 8; ++i) {
            QPixmap swatch(12, 12);
            swatch.fill(QColor(swatches[i]));
            colors_->addTab(QIcon(swatch), {});
            colors_->setTabToolTip(i, names[i]);
            colors_->setAccessibleTabName(i, names[i]);
        }
        layout->addWidget(channels_);
        layout->addWidget(colors_);
        for (int c = 0; c < 3; ++c) {
            groups_[c] = new QWidget(this);
            auto* group = new QVBoxLayout(groups_[c]);
            group->setContentsMargins(0, 0, 0, 0);
            group->setSpacing(2);
            titles_[c] = new QLabel(channel_names[c], groups_[c]);
            group->addWidget(titles_[c]);
            for (int i = 0; i < 8; ++i) {
                auto* row = new QWidget(groups_[c]);
                rows_[c][i] = row;
                auto* box = new QVBoxLayout(row);
                box->setContentsMargins(0, 0, 0, 0);
                box->setSpacing(0);
                auto* top = new QHBoxLayout;
                labels_[c][i] = new QLabel(names[i], row);
                labels_[c][i]->setProperty("bandName", names[i]);
                labels_[c][i]->setProperty("channelName", channel_names[c]);
                top->addWidget(labels_[c][i]);
                top->addStretch();
                auto* spin = new QDoubleSpinBox(row);
                spin->setObjectName(QString("rawHsl%1Band%2").arg(c).arg(i));
                spin->setRange(-100, 100);
                spin->setDecimals(0);
                spin->setFixedWidth(48);
                spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
                spin->setAccessibleName(channel_names[c] + names[i]);
                auto* slider = new ColorBandSlider(c, i, row);
                slider->setObjectName(spin->objectName() + "Slider");
                slider->setAccessibleName(spin->accessibleName());
                spins_[c][i] = spin;
                sliders_[c][i] = slider;
                top->addWidget(spin);
                box->addLayout(top);
                box->addWidget(slider);
                group->addWidget(row);
                connect(slider, &QSlider::valueChanged, spin,
                        [spin](int value) { spin->setValue(value); });
                connect(spin, &QDoubleSpinBox::valueChanged, this,
                        [this, c, i, slider](double value) {
                            const QSignalBlocker blocker(slider);
                            slider->setValue(qRound(value));
                            values(c)[i] = value;
                            if (changed_)
                                changed_();
                        });
            }
            layout->addWidget(groups_[c]);
        }
        connect(channels_, &QTabBar::currentChanged, this, [this] { show_channels(); });
        connect(colors_, &QTabBar::currentChanged, this, [this] { show_channels(); });
        connect(mode_, &QComboBox::currentIndexChanged, this, [this] { show_channels(); });
        refresh();
        show_channels();
    }
    void refresh() {
        for (int c = 0; c < 3; ++c)
            for (int i = 0; i < 8; ++i) {
                const QSignalBlocker block_spin(spins_[c][i]), block_slider(sliders_[c][i]);
                spins_[c][i]->setValue(values(c)[i]);
                sliders_[c][i]->setValue(qRound(values(c)[i]));
            }
    }

  private:
    std::array<double, 8>& values(int channel) {
        return channel == 0   ? settings_.hue
               : channel == 1 ? settings_.saturation_bands
                              : settings_.luminance_bands;
    }
    void show_channels() {
        const bool color_mode = mode_->currentIndex() == 1;
        channels_->setVisible(!color_mode);
        colors_->setVisible(color_mode);
        for (int c = 0; c < 3; ++c) {
            groups_[c]->setVisible(color_mode || channels_->currentIndex() == 3 ||
                                   channels_->currentIndex() == c);
            titles_[c]->setVisible(!color_mode && channels_->currentIndex() == 3);
            for (int i = 0; i < 8; ++i) {
                rows_[c][i]->setVisible(!color_mode || colors_->currentIndex() == i);
                labels_[c][i]->setText(
                    labels_[c][i]->property(color_mode ? "channelName" : "bandName").toString());
            }
        }
    }
    editing::ToneSettings& settings_;
    std::function<void()> changed_;
    QComboBox* mode_{};
    QTabBar* channels_{};
    QTabBar* colors_{};
    std::array<QWidget*, 3> groups_{};
    std::array<QLabel*, 3> titles_{};
    std::array<std::array<QWidget*, 8>, 3> rows_{};
    std::array<std::array<QLabel*, 8>, 3> labels_{};
    std::array<std::array<QDoubleSpinBox*, 8>, 3> spins_{};
    std::array<std::array<QSlider*, 8>, 3> sliders_{};
};

class ColorWheel final : public QWidget {
  public:
    ColorWheel(double& hue, double& saturation, QWidget* parent)
        : QWidget(parent), hue_(hue), saturation_(saturation) {
        setFixedSize(112, 112);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(
            tr("拖动调整色相与饱和度；Shift 只调饱和度，Ctrl 只调色相，Alt 微调；双击恢复中性"));
        wheel_ = QImage(112, 112, QImage::Format_ARGB32);
        wheel_.fill(Qt::transparent);
        for (int y = 0; y < 112; ++y)
            for (int x = 0; x < 112; ++x) {
                const double dx = x - 55.5, dy = 55.5 - y;
                const double radius = std::hypot(dx, dy) / 51;
                if (radius <= 1) {
                    const double pixel_hue =
                        std::fmod(std::atan2(dy, dx) / (2 * std::numbers::pi) + 1, 1.);
                    wheel_.setPixelColor(x, y, QColor::fromHsvF(pixel_hue, radius, 1));
                }
            }
    }
    std::function<void()> changed;

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(width() / 112., height() / 112.);
        painter.drawImage(0, 0, wheel_);
        const double angle = hue_ / 180 * std::numbers::pi;
        const double radius = saturation_ / 100 * 51;
        const QPointF position(55.5 + std::cos(angle) * radius, 55.5 - std::sin(angle) * radius);
        painter.setPen(QPen(Qt::black, 3));
        painter.drawEllipse(position, 4, 4);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawEllipse(position, 4, 4);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        last_ = event->position() * (112. / width());
        start_hue_ = hue_;
        start_saturation_ = saturation_;
        dragging_ = true;
        adjust(event->position() * (112. / width()), event->modifiers());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ && event->buttons().testFlag(Qt::LeftButton))
            adjust(event->position() * (112. / width()), event->modifiers());
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton)
            dragging_ = false;
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape && dragging_) {
            hue_ = start_hue_;
            saturation_ = start_saturation_;
            dragging_ = false;
        } else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10. : 1.;
            hue_ = std::fmod(hue_ + (event->key() == Qt::Key_Left ? -step : step) + 360., 360.);
        } else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
            const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10. : 1.;
            saturation_ = std::clamp(saturation_ +
                (event->key() == Qt::Key_Down ? -step : step), 0., 100.);
        } else {
            QWidget::keyPressEvent(event);
            return;
        }
        update();
        if (changed)
            changed();
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {
        saturation_ = 0;
        update();
        if (changed)
            changed();
    }

  private:
    void adjust(QPointF point, Qt::KeyboardModifiers modifiers) {
        const double dx = point.x() - 55.5, dy = 55.5 - point.y();
        const double hue = std::fmod(std::atan2(dy, dx) * 180 / std::numbers::pi + 360, 360.);
        const double saturation = std::min(100., std::hypot(dx, dy) / 51 * 100);
        if (modifiers.testFlag(Qt::AltModifier)) {
            if (!modifiers.testFlag(Qt::ShiftModifier))
                hue_ = std::fmod(hue_ + (point.x() - last_.x()) * .2 + 360, 360.);
            if (!modifiers.testFlag(Qt::ControlModifier))
                saturation_ = std::clamp(saturation_ - (point.y() - last_.y()) * .2, 0., 100.);
        } else {
            hue_ = modifiers.testFlag(Qt::ShiftModifier) ? start_hue_ : hue;
            saturation_ = modifiers.testFlag(Qt::ControlModifier) ? start_saturation_ : saturation;
        }
        last_ = point;
        update();
        if (changed)
            changed();
    }
    double& hue_;
    double& saturation_;
    QImage wheel_;
    QPointF last_;
    double start_hue_{}, start_saturation_{};
    bool dragging_{};
};
} // namespace
TonePanel::TonePanel(editing::ToneSettings settings, QWidget* parent)
    : QWidget(parent), settings_(std::move(settings)) {
    setObjectName("tonePanel");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    categories_ = new QListWidget(this);
    categories_->setObjectName("toneCategories");
    categories_->addItems(
        {tr("光线"), tr("颜色"), tr("色阶"), tr("曲线"), tr("色彩混合器"), tr("色彩分级")});
    categories_->setFixedWidth(92);
    categories_->setStyleSheet(
        "QListWidget { border: 0; } QListWidget::item { min-height: 30px; padding: 3px 5px; }");
    layout->addWidget(categories_);
    pages_ = new QStackedWidget(this);
    layout->addWidget(pages_, 1);
    const auto notify = [this] {
        if (refresh_details_)
            refresh_details_();
        if (refresh_hsl_)
            refresh_hsl_();
        for (auto* widget : findChildren<QWidget*>())
            if (widget->objectName().startsWith("rawColorWheel"))
                widget->update();
        if (changed)
            changed();
    };
    const auto page = [this] {
        auto* widget = new QWidget(pages_);
        pages_->addWidget(widget);
        return new QFormLayout(widget);
    };
    const auto number = [this, notify](QFormLayout* form, const char* name, const QString& label,
                                       double& value, double low = -100, double high = 100,
                                       double step = 1) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QLatin1String(name));
        spin->setRange(low, high);
        spin->setSingleStep(step);
        spin->setDecimals(step < .1 ? 3 : step < 1 ? 2 : 0);
        spin->setValue(value);
        spin->setFixedWidth(70);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        auto* row = new QWidget(this);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        auto* slider = new ColorBandSlider(-1, 0, row);
        slider->setObjectName(QString::fromLatin1(name) + "Slider");
        slider->setRange(qRound(low / step), qRound(high / step));
        slider->setValue(qRound(value / step));
        slider->set_default(qRound(
            (QLatin1String(name) == "toneGradingBlending"                                    ? 50.
             : QLatin1String(name) == "toneGamma" || QLatin1String(name) == "toneWhitePoint" ? 1.
                                                                                             : 0.) /
            step));
        slider->setTracking(false);
        row_layout->addWidget(slider, 1);
        row_layout->addWidget(spin);
        form->addRow(label, row);
        connect(slider, &QSlider::valueChanged, spin,
                [spin, step](int v) { spin->setValue(v * step); });
        connect(spin, &QDoubleSpinBox::valueChanged, slider, [slider, step](double v) {
            const QSignalBlocker blocker(slider);
            slider->setValue(qRound(v / step));
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [&value, notify](double v) {
            value = v;
            notify();
        });
    };
    auto* light = page();
    number(light, "toneExposure", tr("曝光"), settings_.exposure, -5, 5, .1);
    number(light, "toneContrast", tr("对比度"), settings_.contrast);
    number(light, "toneHighlights", tr("高光"), settings_.highlights);
    number(light, "toneShadows", tr("阴影"), settings_.shadows);
    number(light, "toneWhites", tr("白色"), settings_.whites);
    number(light, "toneBlacks", tr("黑色"), settings_.blacks);
    auto* color = page();
    number(color, "toneTemperature", tr("冷暖偏移"), settings_.temperature);
    number(color, "toneTint", tr("色调"), settings_.tint);
    number(color, "toneVibrance", tr("自然饱和度"), settings_.vibrance);
    number(color, "toneSaturation", tr("饱和度"), settings_.saturation);
    auto* monochrome = new QCheckBox(tr("黑白"), this);
    monochrome->setObjectName("toneMonochrome");
    monochrome->setChecked(settings_.monochrome);
    color->addRow(monochrome);
    connect(monochrome, &QCheckBox::toggled, this, [this, notify](bool enabled) {
        settings_.monochrome = enabled;
        notify();
    });
    auto* levels = page();
    number(levels, "toneBlackPoint", tr("输入黑场"), settings_.black_point, 0, .499, .001);
    number(levels, "toneGamma", tr("中间调"), settings_.gamma, .1, 10, .01);
    number(levels, "toneWhitePoint", tr("输入白场"), settings_.white_point, .5, 1, .001);
    auto* curve = page();
    curves_ = new CurvePanel(settings_, notify, this);
    curve->addRow(curves_);
    auto* curve_target = new QToolButton(curves_);
    curve_target->setObjectName("toneCurveTarget");
    curve_target->setText(tr("◎ 目标调整"));
    curve_target->setCheckable(true);
    static_cast<QVBoxLayout*>(curves_->layout())->insertWidget(0, curve_target);
    connect(curve_target, &QToolButton::toggled, this,
            [this](bool checked) { set_target_mode(checked ? 3 : -1); });
    auto* hsl = page();
    auto* band = new QComboBox(this);
    band->setObjectName("toneBand");
    band->addItems(
        {tr("红"), tr("橙"), tr("黄"), tr("绿"), tr("青"), tr("蓝"), tr("紫"), tr("洋红")});
    hsl->addRow(tr("颜色"), band);
    const std::array<const char*, 3> names{"toneHueBand", "toneSaturationBand",
                                           "toneLuminanceBand"};
    const QStringList labels{tr("色相"), tr("饱和度"), tr("明度")};
    const std::array<std::array<double, 8>*, 3> arrays{&settings_.hue, &settings_.saturation_bands,
                                                       &settings_.luminance_bands};
    for (int i = 0; i < 3; ++i) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QLatin1String(names[i]));
        spin->setRange(-100, 100);
        spin->setValue((*arrays[i])[0]);
        hsl->addRow(labels[i], spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [band, array = arrays[i], notify](double value) {
                    (*array)[band->currentIndex()] = value;
                    notify();
                });
        connect(band, &QComboBox::currentIndexChanged, this, [spin, array = arrays[i]](int index) {
            QSignalBlocker blocker(spin);
            spin->setValue((*array)[index]);
        });
    }
    for (int i = 0; i < hsl->count(); ++i)
        if (auto* widget = hsl->itemAt(i)->widget())
            widget->hide();
    auto* shared_mixer = new HslMixer(settings_, notify, pages_->widget(4));
    hsl->addRow(shared_mixer);
    refresh_hsl_ = [shared_mixer] { shared_mixer->refresh(); };
    auto* target_button = shared_mixer->findChild<QToolButton*>("rawHslTarget");
    auto* target_tabs = shared_mixer->findChild<QTabBar*>("rawHslTabs");
    connect(target_button, &QToolButton::toggled, this, [this, target_tabs](bool checked) {
        set_target_mode(checked ? std::min(2, target_tabs->currentIndex()) : -1);
    });
    connect(target_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (target_mode_ >= 0 && target_mode_ < 3 && index < 3 && target_mode_ != index)
            set_target_mode(index);
    });
    auto* mixer_tabs = new QTabBar(pages_->widget(4));
    mixer_tabs->setObjectName("rawMixerTabs");
    mixer_tabs->addTab(tr("混合器"));
    mixer_tabs->addTab(tr("点颜色"));
    mixer_tabs->setDrawBase(false);
    mixer_tabs->setExpanding(false);
    hsl->insertRow(0, mixer_tabs);
    point_colors_ = new PointColorPanel(settings_, pages_->widget(4));
    point_colors_->changed = [this] {
        if (changed)
            changed();
    };
    point_colors_->preview_changed = [this] {
        if (point_colors_->sampling())
            set_target_mode(-1);
        if (point_view_changed)
            point_view_changed();
    };
    hsl->addRow(point_colors_);
    point_colors_->hide();
    connect(mixer_tabs, &QTabBar::currentChanged, this, [this, shared_mixer](int index) {
        shared_mixer->setVisible(index == 0);
        point_colors_->setVisible(index == 1);
        if (index == 0)
            point_colors_->stop_sampling();
        else
            set_target_mode(-1);
    });
    auto* grading = page();
    auto* zone = new QComboBox(this);
    zone->setObjectName("toneGradingZone");
    zone->addItems({tr("阴影"), tr("中间调"), tr("高光"), tr("全局")});
    zone->hide();
    auto* grading_tabs = new QTabBar(this);
    grading_tabs->setObjectName("toneGradingViews");
    grading_tabs->setExpanding(true);
    grading_tabs->setUsesScrollButtons(false);
    grading_tabs->setStyleSheet("QTabBar::tab { min-width:0; padding:4px 5px; }");
    for (const auto& title : {tr("三向"), tr("阴影"), tr("中间调"), tr("高光"), tr("全局")})
        grading_tabs->addTab(title);
    grading->addRow(grading_tabs);
    auto* wheels = new QWidget(this);
    auto* wheel_layout = new QGridLayout(wheels);
    wheel_layout->setContentsMargins(0, 0, 0, 0);
    std::array<ColorWheel*, 4> wheel_items{};
    std::array<QWidget*, 4> wheel_containers{};
    for (int i = 0; i < 4; ++i) {
        auto* container = new QWidget(wheels);
        wheel_containers[i] = container;
        auto* box = new QVBoxLayout(container);
        box->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(zone->itemText(i), container);
        label->setAlignment(Qt::AlignCenter);
        box->addWidget(label);
        auto* wheel =
            new ColorWheel(settings_.grading_hue[i], settings_.grading_saturation[i], container);
        wheel_items[i] = wheel;
        wheel->setObjectName(QString("rawColorWheel%1").arg(i));
        wheel->changed = [this, zone, i] {
            zone->setCurrentIndex(i);
            set_settings(settings_);
            if (changed)
                changed();
        };
        box->addWidget(wheel, 0, Qt::AlignCenter);
        auto* luminance_slider = new ColorBandSlider(0, 0, container);
        luminance_slider->set_colors(QColor("#444444"), QColor("#eeeeee"));
        luminance_slider->setFixedWidth(112);
        luminance_slider->setObjectName(QString("toneGradingLight%1").arg(i));
        luminance_slider->setRange(-100, 100);
        luminance_slider->setValue(qRound(settings_.grading_luminance[i]));
        luminance_slider->setAccessibleName(zone->itemText(i) + tr("明度"));
        box->addWidget(luminance_slider, 0, Qt::AlignCenter);
        connect(luminance_slider, &QSlider::valueChanged, this, [this, zone, i](int value) {
            settings_.grading_luminance[i] = value;
            zone->setCurrentIndex(i);
            set_settings(settings_);
            if (changed)
                changed();
        });
    }
    const auto show_wheels = [wheel_layout, wheel_containers, wheel_items, zone](int view) {
        for (int i = 0; i < 4; ++i) {
            wheel_layout->removeWidget(wheel_containers[i]);
            const bool show = view == 0 ? i < 3 : i == view - 1;
            wheel_containers[i]->setVisible(show);
            wheel_items[i]->setFixedSize(view ? 180 : 112, view ? 180 : 112);
            if (show)
                wheel_layout->addWidget(wheel_containers[i],
                                        view     ? 0
                                        : i == 1 ? 0
                                                 : 1,
                                        view     ? 0
                                        : i == 2 ? 1
                                                 : 0,
                                        1, view || i == 1 ? 2 : 1);
        }
        if (view)
            zone->setCurrentIndex(view - 1);
    };
    connect(grading_tabs, &QTabBar::currentChanged, this, show_wheels);
    show_wheels(0);
    grading->addRow(wheels);
    refresh_grading_ = [this, wheel_items] {
        for (int i = 0; i < 4; ++i) {
            wheel_items[i]->update();
            auto* light = findChild<QSlider*>(QString("toneGradingLight%1").arg(i));
            const QSignalBlocker block(light);
            light->setValue(qRound(settings_.grading_luminance[i]));
        }
    };
    const std::array<std::array<double, 4>*, 3> grade_arrays{
        &settings_.grading_hue, &settings_.grading_saturation, &settings_.grading_luminance};
    const std::array<const char*, 3> grade_names{"toneGradingHue", "toneGradingSaturation",
                                                 "toneGradingLuminance"};
    auto* grading_numbers = new QWidget(this);
    auto* grading_numbers_form = new QFormLayout(grading_numbers);
    grading_numbers_form->setContentsMargins(0, 0, 0, 0);
    grading->addRow(grading_numbers);
    grading_numbers->hide();
    connect(grading_tabs, &QTabBar::currentChanged, grading_numbers,
            [grading_numbers](int index) { grading_numbers->setVisible(index != 0); });
    for (int i = 0; i < 3; ++i) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QLatin1String(grade_names[i]));
        spin->setRange(i == 2 ? -100 : 0, i == 0 ? 360 : 100);
        spin->setValue((*grade_arrays[i])[0]);
        grading_numbers_form->addRow(labels[i], spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, zone, array = grade_arrays[i], notify](double value) {
                    (*array)[zone->currentIndex()] = value;
                    refresh_grading_();
                    notify();
                });
        connect(zone, &QComboBox::currentIndexChanged, this,
                [spin, array = grade_arrays[i]](int index) {
                    QSignalBlocker blocker(spin);
                    spin->setValue((*array)[index]);
                });
    }
    number(grading, "toneGradingBlending", tr("混合"), settings_.grading_blending, 0, 100);
    number(grading, "toneGradingBalance", tr("平衡"), settings_.grading_balance);
    categories_->addItems({tr("效果"), tr("细节")});
    auto* effects = page();
    number(effects, "toneTexture", tr("纹理"), settings_.texture);
    number(effects, "toneClarity", tr("清晰度"), settings_.clarity);
    number(effects, "toneDehaze", tr("去薄雾"), settings_.dehaze);
    number(effects, "toneVignette", tr("晕影"), settings_.vignette);
    auto* vignette_style = new QComboBox(this);
    vignette_style->setObjectName("toneVignetteStyle");
    vignette_style->addItems({tr("高光优先"), tr("颜色优先"), tr("绘画叠加")});
    effects->addRow(tr("样式"), vignette_style);
    number(effects, "toneVignetteMidpoint", tr("中点"), settings_.vignette_midpoint, 0, 100);
    number(effects, "toneVignetteRoundness", tr("圆度"), settings_.vignette_roundness);
    number(effects, "toneVignetteFeather", tr("羽化"), settings_.vignette_feather, 0, 100);
    number(effects, "toneVignetteHighlights", tr("高光"), settings_.vignette_highlights, 0, 100);
    number(effects, "toneGrain", tr("颗粒"), settings_.grain, 0, 100);
    number(effects, "toneGrainSize", tr("大小"), settings_.grain_size, 0, 100);
    number(effects, "toneGrainRoughness", tr("粗糙度"), settings_.grain_roughness, 0, 100);
    refresh_film_ = [this, vignette_style] {
        const QSignalBlocker blocker(vignette_style);
        vignette_style->setCurrentIndex(int(settings_.vignette_style));
        vignette_style->setEnabled(settings_.vignette != 0);
        for (const auto* name :
             {"toneVignetteMidpoint", "toneVignetteRoundness", "toneVignetteFeather",
              "toneVignetteHighlights", "toneGrainSize", "toneGrainRoughness"}) {
            const bool grain = QByteArray(name).startsWith("toneGrain");
            const bool enabled =
                grain ? settings_.grain != 0
                      : settings_.vignette != 0 &&
                            (QByteArray(name) != "toneVignetteHighlights" ||
                             (settings_.vignette < 0 && settings_.vignette_style != 2));
            findChild<QDoubleSpinBox*>(QLatin1String(name))->parentWidget()->setEnabled(enabled);
        }
    };
    connect(vignette_style, &QComboBox::currentIndexChanged, this, [this, notify](int index) {
        settings_.vignette_style = index;
        settings_.legacy_film_effects = false;
        refresh_film_();
        notify();
    });
    for (const auto* name :
         {"toneVignette", "toneVignetteMidpoint", "toneVignetteRoundness", "toneVignetteFeather",
          "toneVignetteHighlights", "toneGrain", "toneGrainSize", "toneGrainRoughness"})
        connect(findChild<QDoubleSpinBox*>(QLatin1String(name)), &QDoubleSpinBox::valueChanged,
                this, [this, notify] {
                    settings_.legacy_film_effects = false;
                    refresh_film_();
                    notify();
                });
    refresh_film_();
    for (const auto& [name, value] :
         {std::pair{"toneVignetteMidpointSlider", 50}, std::pair{"toneVignetteFeatherSlider", 50},
          std::pair{"toneGrainSizeSlider", 25}, std::pair{"toneGrainRoughnessSlider", 50}})
        static_cast<ColorBandSlider*>(findChild<QSlider*>(QLatin1String(name)))->set_default(value);
    auto* details = page();
    number(details, "toneSharpening", tr("锐化"), settings_.sharpening, 0, 150);
    number(details, "toneSharpenRadius", tr("半径"), settings_.details.radius, .5, 3, .1);
    number(details, "toneSharpenDetail", tr("细节"), settings_.details.detail, 0, 100);
    number(details, "toneSharpenMasking", tr("蒙版"), settings_.details.masking, 0, 100);
    details->addRow(new QLabel(tr("手动减少杂色"), this));
    number(details, "toneNoiseLuminance", tr("明度"), settings_.details.luminance, 0, 100);
    number(details, "toneNoiseLuminanceDetail", tr("细节"), settings_.details.luminance_detail, 0,
           100);
    number(details, "toneNoiseLuminanceContrast", tr("对比度"),
           settings_.details.luminance_contrast, 0, 100);
    number(details, "toneNoiseColor", tr("颜色"), settings_.details.color, 0, 100);
    number(details, "toneNoiseColorDetail", tr("细节"), settings_.details.color_detail, 0, 100);
    number(details, "toneNoiseColorSmoothness", tr("平滑度"), settings_.details.color_smoothness, 0,
           100);
    details->addRow(new QLabel(tr("请在 100% 或更大倍率检查锐化与降噪效果。"), this));
    for (const auto* name :
         {"toneSharpening", "toneSharpenRadius", "toneSharpenDetail", "toneSharpenMasking"})
        connect(findChild<QDoubleSpinBox*>(QLatin1String(name)), &QDoubleSpinBox::valueChanged,
                this, [this, notify] {
                    settings_.details.legacy_sharpening = false;
                    notify();
                });
    for (const auto& [name, value] : {std::pair{"toneSharpenRadius", 1.},
                                      {"toneSharpenDetail", 25.},
                                      {"toneNoiseLuminanceDetail", 50.},
                                      {"toneNoiseColorDetail", 50.},
                                      {"toneNoiseColorSmoothness", 50.}}) {
        auto* spin = findChild<QDoubleSpinBox*>(QLatin1String(name));
        static_cast<ColorBandSlider*>(findChild<QSlider*>(QString::fromLatin1(name) + "Slider"))
            ->set_default(qRound(value / spin->singleStep()));
    }
    refresh_details_ = [this] {
        for (const auto* name : {"toneNoiseLuminanceDetail", "toneNoiseLuminanceContrast"})
            findChild<QDoubleSpinBox*>(QLatin1String(name))
                ->parentWidget()
                ->setEnabled(settings_.details.luminance > 0);
        for (const auto* name : {"toneNoiseColorDetail", "toneNoiseColorSmoothness"})
            findChild<QDoubleSpinBox*>(QLatin1String(name))
                ->parentWidget()
                ->setEnabled(settings_.details.color > 0);
    };
    refresh_details_();
    for (const auto* name :
         {"toneSharpening", "toneSharpenRadius", "toneSharpenDetail", "toneSharpenMasking"}) {
        const int mode = QLatin1String(name) == "toneSharpenMasking" ? 1 : 2;
        auto* slider = static_cast<ColorBandSlider*>(
            findChild<QSlider*>(QString::fromLatin1(name) + "Slider"));
        slider->setToolTip(tr("按住 Alt 拖动查看锐化辅助预览；松开恢复彩图。"));
        slider->auxiliary_preview = [this, mode](bool active) {
            const int next = active ? mode : 0;
            if (next == detail_preview_)
                return;
            detail_preview_ = next;
            if (detail_view_changed)
                detail_view_changed();
        };
    }
    categories_->addItem(tr("校正"));
    auto* calibration = page();
    auto* process = new QComboBox(this);
    process->setObjectName("rawCalibrationProcess");
    process->addItem(tr("Huelith 1（当前）"), 1);
    process->setToolTip(
        tr("工程保留处理版本。此版本使用 Huelith 原色校正，不对应 Adobe 处理版本。"));
    calibration->addRow(tr("处理版本"), process);
    calibration->addRow(new QLabel(tr("阴影"), this));
    number(calibration, "toneCalibrationShadowTint", tr("色调"), settings_.calibration.shadow_tint);
    static_cast<ColorBandSlider*>(findChild<QSlider*>("toneCalibrationShadowTintSlider"))
        ->set_colors(QColor("#69ad70"), QColor("#b56aaf"));
    const std::array<const char*, 3> primary_names{"Red", "Green", "Blue"};
    const QStringList primary_labels{tr("主要红色"), tr("主要绿色"), tr("主要蓝色")};
    const std::array<QColor, 3> low{QColor("#b64a8a"), QColor("#bab457"), QColor("#5fa39e")};
    const std::array<QColor, 3> high{QColor("#c5a252"), QColor("#4caaa1"), QColor("#9468bc")};
    const std::array<QColor, 3> saturated{QColor("#ca5656"), QColor("#62ae64"), QColor("#587dd2")};
    for (int i = 0; i < 3; ++i) {
        calibration->addRow(new QLabel(primary_labels[i], this));
        const auto prefix = QByteArray("toneCalibration") + primary_names[i];
        number(calibration, (prefix + "Hue").constData(), tr("色相"), settings_.calibration.hue[i]);
        number(calibration, (prefix + "Saturation").constData(), tr("饱和度"),
               settings_.calibration.saturation[i]);
        static_cast<ColorBandSlider*>(
            findChild<QSlider*>(QString::fromLatin1(prefix + "HueSlider")))
            ->set_colors(low[i], high[i]);
        static_cast<ColorBandSlider*>(
            findChild<QSlider*>(QString::fromLatin1(prefix + "SaturationSlider")))
            ->set_colors(QColor("#777777"), saturated[i]);
    }
    connect(categories_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    categories_->setCurrentRow(0);
}
void TonePanel::use_accordion() {
    categories_->hide();
    pages_->hide();
    auto* accordion = new QWidget(this);
    accordion->setObjectName("rawAccordion");
    auto* box = new QVBoxLayout(accordion);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(1);
    box->setSizeConstraint(QLayout::SetMinimumSize);
    layout()->setSizeConstraint(QLayout::SetMinimumSize);
    // Keep the existing shared controls and their bindings; only change their presentation.
    std::vector<QWidget*> pages;
    while (pages_->count()) {
        auto* page = pages_->widget(0);
        pages_->removeWidget(page);
        pages.push_back(page);
    }
    refresh_extra_ = [this] { point_colors_->refresh(); };
    for (const int index : {0, 1, 6, 3, 4, 5, 7, 2, 8}) {
        const auto section = raw_section_header(
            accordion, categories_->item(index)->text(), QString("rawSection%1").arg(index),
            QString("rawSectionEnable%1").arg(index), index == 0, settings_.sections[index]);
        auto* header = section.expand;
        auto* enabled = section.enabled;
        box->addLayout(section.layout);
        connect(enabled, &QToolButton::toggled, this, [this, index](bool value) {
            settings_.sections[index] = value;
            if (changed)
                changed();
        });
        pages[index]->setObjectName(QString("rawSectionBody%1").arg(index));
        box->addWidget(pages[index]);
        pages[index]->setVisible(index == 0);
        connect(header, &QToolButton::toggled, pages[index],
                [header, page = pages[index]](bool open) {
                    page->setVisible(open);
                    header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
                });
    }
    for (auto* slider : findChildren<QSlider*>())
        slider->setTracking(true);
    for (auto* page : pages) {
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        form->setSizeConstraint(QLayout::SetMinimumSize);
        page->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        form->setContentsMargins(12, 6, 12, 8);
        form->setVerticalSpacing(4);
        for (int r = 0; r < form->rowCount(); ++r) {
            auto* field = form->itemAt(r, QFormLayout::FieldRole);
            auto* label_item = form->itemAt(r, QFormLayout::LabelRole);
            if (!field || !field->widget() || !label_item || !label_item->widget())
                continue;
            auto* row = field->widget();
            auto* slider = row->findChild<QSlider*>(QString{}, Qt::FindDirectChildrenOnly);
            auto* spin = row->findChild<QDoubleSpinBox*>(QString{}, Qt::FindDirectChildrenOnly);
            if (!slider || !spin)
                continue;
            auto* label = qobject_cast<QLabel*>(label_item->widget());
            const QString title = label ? label->text() : QString{};
            const auto removed = form->takeRow(r);
            delete removed.labelItem;
            delete removed.fieldItem;
            if (label)
                label->deleteLater();
            delete row->layout();
            auto* rows = new QVBoxLayout(row);
            rows->setContentsMargins(0, 0, 0, 0);
            rows->setSpacing(0);
            auto* top = new QHBoxLayout;
            top->addWidget(new QLabel(title, row));
            top->addStretch();
            spin->setFixedWidth(58);
            top->addWidget(spin);
            rows->addLayout(top);
            rows->addWidget(slider);
            form->insertRow(r, row);
        }
    }
    layout()->addWidget(accordion);
}
void TonePanel::set_category(int index) {
    categories_->setCurrentRow(index);
}
bool TonePanel::point_color_sampling() const {
    return point_colors_ && point_colors_->sampling();
}
void TonePanel::sample_point_color(QColor color) {
    if (point_colors_)
        point_colors_->sample(color);
}
void TonePanel::stop_point_sampling() {
    if (point_colors_)
        point_colors_->stop_sampling();
}
std::optional<editing::PointColor> TonePanel::point_color_visualized() const {
    return point_colors_ ? point_colors_->visualized() : std::nullopt;
}
void TonePanel::set_curve_histogram(const QImage& image) {
    if (curves_)
        curves_->histogram(image);
}
void TonePanel::set_settings(editing::ToneSettings settings) {
    settings_ = std::move(settings);
    if (curves_)
        curves_->refresh();
    if (refresh_grading_)
        refresh_grading_();
    for (int i = 0; i < 9; ++i)
        if (auto* enabled = findChild<QToolButton*>(QString("rawSectionEnable%1").arg(i))) {
            const QSignalBlocker blocker(enabled);
            enabled->setChecked(settings_.sections[i]);
        }
    if (refresh_hsl_)
        refresh_hsl_();
    if (refresh_extra_)
        refresh_extra_();
    const std::pair<const char*, double> values[]{
        {"toneExposure", settings_.exposure},
        {"rawExposure", settings_.exposure},
        {"toneContrast", settings_.contrast},
        {"toneHighlights", settings_.highlights},
        {"toneShadows", settings_.shadows},
        {"toneWhites", settings_.whites},
        {"toneBlacks", settings_.blacks},
        {"toneTemperature", settings_.temperature},
        {"toneTint", settings_.tint},
        {"toneSaturation", settings_.saturation},
        {"toneVibrance", settings_.vibrance},
        {"toneTexture", settings_.texture},
        {"toneClarity", settings_.clarity},
        {"toneDehaze", settings_.dehaze},
        {"toneVignette", settings_.vignette},
        {"toneGrain", settings_.grain},
        {"toneVignetteMidpoint", settings_.vignette_midpoint},
        {"toneVignetteRoundness", settings_.vignette_roundness},
        {"toneVignetteFeather", settings_.vignette_feather},
        {"toneVignetteHighlights", settings_.vignette_highlights},
        {"toneGrainSize", settings_.grain_size},
        {"toneGrainRoughness", settings_.grain_roughness},
        {"toneSharpening", settings_.sharpening},
        {"toneSharpenRadius", settings_.details.radius},
        {"toneSharpenDetail", settings_.details.detail},
        {"toneSharpenMasking", settings_.details.masking},
        {"toneNoiseLuminance", settings_.details.luminance},
        {"toneNoiseLuminanceDetail", settings_.details.luminance_detail},
        {"toneNoiseLuminanceContrast", settings_.details.luminance_contrast},
        {"toneNoiseColor", settings_.details.color},
        {"toneNoiseColorDetail", settings_.details.color_detail},
        {"toneNoiseColorSmoothness", settings_.details.color_smoothness},
        {"toneBlackPoint", settings_.black_point},
        {"toneWhitePoint", settings_.white_point},
        {"toneGamma", settings_.gamma},
        {"toneCalibrationShadowTint", settings_.calibration.shadow_tint},
        {"toneCalibrationRedHue", settings_.calibration.hue[0]},
        {"toneCalibrationGreenHue", settings_.calibration.hue[1]},
        {"toneCalibrationBlueHue", settings_.calibration.hue[2]},
        {"toneCalibrationRedSaturation", settings_.calibration.saturation[0]},
        {"toneCalibrationGreenSaturation", settings_.calibration.saturation[1]},
        {"toneCalibrationBlueSaturation", settings_.calibration.saturation[2]}};
    for (const auto& [name, value] : values)
        if (auto* spin = findChild<QDoubleSpinBox*>(QLatin1String(name))) {
            QSignalBlocker blocker(spin);
            spin->setValue(value);
        }
    const auto band = findChild<QComboBox*>("toneBand")->currentIndex();
    for (const auto& [name, value] :
         {std::pair{"toneHueBand", settings_.hue[band]},
          std::pair{"toneSaturationBand", settings_.saturation_bands[band]},
          std::pair{"toneLuminanceBand", settings_.luminance_bands[band]}}) {
        auto* spin = findChild<QDoubleSpinBox*>(QLatin1String(name));
        QSignalBlocker blocker(spin);
        spin->setValue(value);
    }
    findChild<QWidget*>("toneCurve")->update();
    if (refresh_details_)
        refresh_details_();
    if (refresh_film_)
        refresh_film_();
    for (auto* spin : findChildren<QDoubleSpinBox*>()) {
        if (auto* slider =
                spin->parentWidget()->findChild<QSlider*>(QString{}, Qt::FindDirectChildrenOnly)) {
            const QSignalBlocker blocker(slider);
            slider->setValue(qRound(spin->value() / spin->singleStep()));
        }
    }
    auto* mono = findChild<QCheckBox*>("toneMonochrome");
    const QSignalBlocker mono_blocker(mono);
    mono->setChecked(settings_.monochrome);
    const auto zone = findChild<QComboBox*>("toneGradingZone")->currentIndex();
    for (const auto& [name, value] :
         {std::pair{"toneGradingHue", settings_.grading_hue[zone]},
          std::pair{"toneGradingSaturation", settings_.grading_saturation[zone]},
          std::pair{"toneGradingLuminance", settings_.grading_luminance[zone]},
          std::pair{"toneGradingBalance", settings_.grading_balance},
          std::pair{"toneGradingBlending", settings_.grading_blending}}) {
        auto* spin = findChild<QDoubleSpinBox*>(QLatin1String(name));
        const QSignalBlocker blocker(spin);
        spin->setValue(value);
        if (auto* slider =
                spin->parentWidget()->findChild<QSlider*>(QString{}, Qt::FindDirectChildrenOnly)) {
            const QSignalBlocker slider_blocker(slider);
            slider->setValue(qRound(value / spin->singleStep()));
        }
    }
}
void TonePanel::set_target_mode(int mode) {
    if (target_mode_ == mode)
        return;
    finish_target(true);
    target_mode_ = std::clamp(mode, -1, 3);
    if (mode >= 0)
        stop_point_sampling();
    for (const auto& item : {std::pair{"rawHslTarget", mode >= 0 && mode < 3},
                             std::pair{"toneCurveTarget", mode == 3}}) {
        auto* button = findChild<QToolButton*>(QLatin1String(item.first));
        const QSignalBlocker blocker(button);
        button->setChecked(item.second);
    }
    if (mode == 3)
        findChild<QTabBar*>("toneCurveTabs")->setCurrentIndex(0);
    if (mode >= 0 && mode < 3) {
        auto* tabs = findChild<QTabBar*>("rawHslTabs");
        tabs->setCurrentIndex(mode);
    }
    if (point_view_changed)
        point_view_changed();
}
bool TonePanel::begin_target(QColor color) {
    if (target_mode_ < 0 || color.alphaF() <= 0)
        return false;
    target_weights_.fill(0);
    if (target_mode_ < 3) {
        if (color.hsvSaturationF() < .001)
            return false;
        constexpr std::array<double, 8> centers{0, 30, 60, 120, 180, 240, 270, 300};
        const double hue = color.hsvHueF() * 360;
        double maximum{};
        for (int i = 0; i < 8; ++i) {
            const double distance =
                std::min(std::abs(hue - centers[i]), 360 - std::abs(hue - centers[i]));
            target_weights_[i] = std::max(0., 1 - distance / 60);
            maximum = std::max(maximum, target_weights_[i]);
        }
        for (auto& w : target_weights_)
            w /= maximum;
    } else {
        const auto& b = settings_.curve_splits;
        const std::array<double, 4> centers{b[0] / 2, (b[0] + b[1]) / 2, (b[1] + b[2]) / 2,
                                            (b[2] + 1) / 2};
        const double value = .2126 * color.redF() + .7152 * color.greenF() + .0722 * color.blueF();
        if (value <= centers[0])
            target_weights_[0] = 1;
        else if (value >= centers[3])
            target_weights_[3] = 1;
        else
            for (int i = 0; i < 3; ++i)
                if (value >= centers[i] && value < centers[i + 1]) {
                    const double t = (value - centers[i]) / (centers[i + 1] - centers[i]);
                    target_weights_[i] = 1 - t;
                    target_weights_[i + 1] = t;
                    break;
                }
    }
    target_base_ = settings_;
    return true;
}
void TonePanel::move_target(double amount) {
    if (!target_base_)
        return;
    auto next = *target_base_;
    if (target_mode_ == 3) {
        for (int i = 0; i < 4; ++i)
            next.parametric_curve[i] =
                std::clamp(next.parametric_curve[i] + amount * target_weights_[i], -100., 100.);
    } else {
        auto& values = target_mode_ == 0   ? next.hue
                       : target_mode_ == 1 ? next.saturation_bands
                                           : next.luminance_bands;
        for (int i = 0; i < 8; ++i)
            values[i] = std::clamp(values[i] + amount * target_weights_[i], -100., 100.);
    }
    set_settings(std::move(next));
    if (changed)
        changed();
}
void TonePanel::finish_target(bool cancel) {
    if (!target_base_)
        return;
    const auto original = *target_base_;
    target_base_.reset();
    if (cancel) {
        set_settings(original);
        if (changed)
            changed();
    }
}
} // namespace huelith::desktop
