#include "tone_panel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace huelith::desktop {
namespace {
class ColorWheel final : public QWidget {
  public:
    ColorWheel(double& hue, double& saturation, QWidget* parent)
        : QWidget(parent), hue_(hue), saturation_(saturation) {
        setFixedSize(112, 112);
        setToolTip(tr("拖动调整色相与饱和度，双击恢复中性"));
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
        adjust(event->position());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons().testFlag(Qt::LeftButton))
            adjust(event->position());
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {
        saturation_ = 0;
        update();
        if (changed)
            changed();
    }

  private:
    void adjust(QPointF point) {
        const double dx = point.x() - 55.5, dy = 55.5 - point.y();
        hue_ = std::fmod(std::atan2(dy, dx) * 180 / std::numbers::pi + 360, 360.);
        saturation_ = std::min(100., std::hypot(dx, dy) / 51 * 100);
        update();
        if (changed)
            changed();
    }
    double& hue_;
    double& saturation_;
    QImage wheel_;
};
class CurveEditor final : public QWidget {
  public:
    explicit CurveEditor(std::vector<QPointF>& points, QWidget* parent)
        : QWidget(parent), points_source_(&points) {
        setMinimumSize(200, 200);
        setObjectName("toneCurve");
        setToolTip(tr("单击添加节点，拖动调整；右键删除中间节点"));
    }
    std::function<void()> changed;
    void set_points(std::vector<QPointF>& points) {
        points_source_ = &points;
        selected_ = -1;
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        const auto& points_ = *points_source_;
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#202428"));
        painter.setRenderHint(QPainter::Antialiasing);
        const auto area = rect().adjusted(10, 10, -10, -10);
        painter.setPen(QColor("#40464c"));
        for (int i = 0; i <= 4; ++i) {
            painter.drawLine(area.left() + area.width() * i / 4, area.top(),
                             area.left() + area.width() * i / 4, area.bottom());
            painter.drawLine(area.left(), area.top() + area.height() * i / 4, area.right(),
                             area.top() + area.height() * i / 4);
        }
        QPolygonF line;
        for (const auto point : points_)
            line.append(QPointF(area.left() + point.x() * area.width(),
                                area.bottom() - point.y() * area.height()));
        painter.setPen(QPen(QColor("#d8dce1"), 1.5));
        painter.drawPolyline(line);
        painter.setBrush(QColor("#348dec"));
        for (const auto point : line)
            painter.drawEllipse(point, 4, 4);
    }
    void mousePressEvent(QMouseEvent* event) override {
        auto& points_ = *points_source_;
        const auto point = normalized(event->position());
        selected_ = -1;
        double nearest = .04;
        for (std::size_t i = 0; i < points_.size(); ++i) {
            const auto distance = QLineF(points_[i], point).length();
            if (distance < nearest) {
                selected_ = int(i);
                nearest = distance;
            }
        }
        if (event->button() == Qt::RightButton) {
            if (selected_ > 0 && selected_ + 1 < int(points_.size())) {
                points_.erase(points_.begin() + selected_);
                if (changed)
                    changed();
                update();
            }
            selected_ = -1;
            return;
        }
        if (selected_ < 0 && points_.size() < 64 && point.x() > .002 && point.x() < .998) {
            auto position = std::upper_bound(points_.begin(), points_.end(), point.x(),
                                             [](double x, QPointF p) { return x < p.x(); });
            if (point.x() - (position - 1)->x() < .002 || position->x() - point.x() < .002)
                return;
            selected_ = int(position - points_.begin());
            points_.insert(position, point);
            if (changed)
                changed();
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        auto& points_ = *points_source_;
        if (selected_ < 0 || !event->buttons().testFlag(Qt::LeftButton))
            return;
        auto point = normalized(event->position());
        if (selected_ == 0)
            point.setX(0);
        else if (selected_ + 1 == int(points_.size()))
            point.setX(1);
        else
            point.setX(std::clamp(point.x(), points_[selected_ - 1].x() + .001,
                                  points_[selected_ + 1].x() - .001));
        points_[selected_] = point;
        update();
        if (changed)
            changed();
    }

  private:
    QPointF normalized(QPointF p) const {
        const auto area = rect().adjusted(10, 10, -10, -10);
        return {std::clamp((p.x() - area.left()) / area.width(), 0., 1.),
                std::clamp((area.bottom() - p.y()) / area.height(), 0., 1.)};
    }
    std::vector<QPointF>* points_source_;
    int selected_{-1};
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
        {tr("光线"), tr("颜色"), tr("色阶"), tr("曲线"), tr("颜色混合"), tr("色彩分级")});
    categories_->setFixedWidth(92);
    categories_->setStyleSheet(
        "QListWidget { border: 0; } QListWidget::item { min-height: 30px; padding: 3px 5px; }");
    layout->addWidget(categories_);
    pages_ = new QStackedWidget(this);
    layout->addWidget(pages_, 1);
    const auto notify = [this] {
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
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName(QString::fromLatin1(name) + "Slider");
        slider->setRange(qRound(low / step), qRound(high / step));
        slider->setValue(qRound(value / step));
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
    number(color, "toneSaturation", tr("饱和度"), settings_.saturation);
    number(color, "toneVibrance", tr("自然饱和度"), settings_.vibrance);
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
    auto* channel = new QComboBox(this);
    channel->setObjectName("toneCurveChannel");
    channel->addItems({tr("RGB"), tr("红"), tr("绿"), tr("蓝")});
    curve->addRow(tr("通道"), channel);
    auto* editor = new CurveEditor(settings_.curve, this);
    connect(channel, &QComboBox::currentIndexChanged, this, [this, editor](int index) {
        editor->set_points(index == 0 ? settings_.curve : settings_.channel_curves[index - 1]);
    });
    editor->changed = notify;
    curve->addRow(editor);
    auto* reset = new QPushButton(tr("重置曲线"), this);
    reset->setAutoDefault(false);
    curve->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [this, editor, channel, notify] {
        auto& points = channel->currentIndex() == 0
                           ? settings_.curve
                           : settings_.channel_curves[channel->currentIndex() - 1];
        points = {{0, 0}, {1, 1}};
        editor->update();
        notify();
    });
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
    auto* grading = page();
    auto* zone = new QComboBox(this);
    zone->setObjectName("toneGradingZone");
    zone->addItems({tr("阴影"), tr("中间调"), tr("高光")});
    grading->addRow(tr("范围"), zone);
    const std::array<std::array<double, 3>*, 3> grade_arrays{
        &settings_.grading_hue, &settings_.grading_saturation, &settings_.grading_luminance};
    const std::array<const char*, 3> grade_names{"toneGradingHue", "toneGradingSaturation",
                                                 "toneGradingLuminance"};
    for (int i = 0; i < 3; ++i) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QLatin1String(grade_names[i]));
        spin->setRange(i == 2 ? -100 : 0, i == 0 ? 360 : 100);
        spin->setValue((*grade_arrays[i])[0]);
        grading->addRow(labels[i], spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [zone, array = grade_arrays[i], notify](double value) {
                    (*array)[zone->currentIndex()] = value;
                    notify();
                });
        connect(zone, &QComboBox::currentIndexChanged, this,
                [spin, array = grade_arrays[i]](int index) {
                    QSignalBlocker blocker(spin);
                    spin->setValue((*array)[index]);
                });
    }
    number(grading, "toneGradingBalance", tr("平衡"), settings_.grading_balance);
    categories_->addItems({tr("效果"), tr("细节")});
    auto* effects = page();
    number(effects, "toneTexture", tr("纹理"), settings_.texture);
    number(effects, "toneClarity", tr("清晰度"), settings_.clarity);
    number(effects, "toneDehaze", tr("去薄雾"), settings_.dehaze);
    number(effects, "toneVignette", tr("晕影"), settings_.vignette);
    number(effects, "toneGrain", tr("颗粒"), settings_.grain, 0, 100);
    auto* details = page();
    number(details, "toneSharpening", tr("锐化"), settings_.sharpening, 0, 100);
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
    // Keep the existing shared controls and their bindings; only change their presentation.
    std::vector<QWidget*> pages;
    while (pages_->count()) {
        auto* page = pages_->widget(0);
        pages_->removeWidget(page);
        pages.push_back(page);
    }
    auto* hsl_form = qobject_cast<QFormLayout*>(pages[4]->layout());
    for (int i = 0; i < hsl_form->count(); ++i)
        if (auto* widget = hsl_form->itemAt(i)->widget())
            widget->hide();
    auto* mixer = new QWidget(pages[4]);
    auto* mixer_form = new QFormLayout(mixer);
    mixer_form->setContentsMargins(0, 0, 0, 0);
    auto* channel = new QComboBox(mixer);
    channel->setObjectName("rawHslChannel");
    channel->addItems({tr("色相"), tr("饱和度"), tr("明度")});
    mixer_form->addRow(channel);
    const QStringList names{tr("红色"), tr("橙色"), tr("黄色"), tr("绿色"),
                            tr("青色"), tr("蓝色"), tr("紫色"), tr("洋红色")};
    std::array<QDoubleSpinBox*, 8> hsl_spins{};
    const auto band_array = [this](int c) -> std::array<double, 8>& {
        return c == 0   ? settings_.hue
               : c == 1 ? settings_.saturation_bands
                        : settings_.luminance_bands;
    };
    for (int i = 0; i < 8; ++i) {
        auto* row = new QWidget(mixer);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(-100, 100);
        auto* spin = new QDoubleSpinBox(row);
        spin->setObjectName(QString("rawHslBand%1").arg(i));
        spin->setRange(-100, 100);
        spin->setDecimals(0);
        spin->setFixedWidth(58);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        layout->addWidget(slider, 1);
        layout->addWidget(spin);
        mixer_form->addRow(names[i], row);
        hsl_spins[i] = spin;
        connect(slider, &QSlider::valueChanged, spin, [spin](int value) { spin->setValue(value); });
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, channel, band_array, i, slider](double value) {
                    const QSignalBlocker blocker(slider);
                    slider->setValue(qRound(value));
                    band_array(channel->currentIndex())[i] = value;
                    if (changed)
                        changed();
                });
    }
    const auto refresh_hsl = [channel, hsl_spins, band_array] {
        for (int i = 0; i < 8; ++i) {
            const QSignalBlocker blocker(hsl_spins[i]);
            const auto value = band_array(channel->currentIndex())[i];
            hsl_spins[i]->setValue(value);
            auto* slider = hsl_spins[i]->parentWidget()->findChild<QSlider*>();
            const QSignalBlocker slider_blocker(slider);
            slider->setValue(qRound(value));
        }
    };
    connect(channel, &QComboBox::currentIndexChanged, this, [refresh_hsl] { refresh_hsl(); });
    hsl_form->addRow(mixer);
    auto* grading_form = qobject_cast<QFormLayout*>(pages[5]->layout());
    auto* wheels = new QWidget(pages[5]);
    auto* wheel_layout = new QGridLayout(wheels);
    wheel_layout->setContentsMargins(0, 0, 0, 0);
    std::array<ColorWheel*, 3> wheel_items{};
    for (int i = 0; i < 3; ++i) {
        auto* container = new QWidget(wheels);
        auto* wheel_box = new QVBoxLayout(container);
        wheel_box->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(i == 0   ? tr("阴影")
                                 : i == 1 ? tr("中间调")
                                          : tr("高光"),
                                 container);
        label->setAlignment(Qt::AlignCenter);
        auto* wheel =
            new ColorWheel(settings_.grading_hue[i], settings_.grading_saturation[i], container);
        wheel->setObjectName(QString("rawColorWheel%1").arg(i));
        wheel->changed = [this] {
            set_settings(settings_);
            if (changed)
                changed();
        };
        wheel_items[i] = wheel;
        wheel_box->addWidget(label);
        wheel_box->addWidget(wheel, 0, Qt::AlignCenter);
        wheel_layout->addWidget(container, i == 1 ? 0 : 1, i == 2 ? 1 : 0, 1, i == 1 ? 2 : 1);
    }
    grading_form->insertRow(0, wheels);
    refresh_extra_ = [refresh_hsl, wheel_items] {
        refresh_hsl();
        for (auto* wheel : wheel_items)
            wheel->update();
    };
    for (const int index : {0, 1, 6, 3, 4, 5, 7, 2}) {
        auto* header = new QToolButton(accordion);
        header->setObjectName(QString("rawSection%1").arg(index));
        header->setText(categories_->item(index)->text());
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        header->setMinimumHeight(34);
        header->setCheckable(true);
        header->setChecked(index == 0);
        header->setArrowType(index == 0 ? Qt::DownArrow : Qt::RightArrow);
        box->addWidget(header);
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
void TonePanel::set_settings(editing::ToneSettings settings) {
    settings_ = std::move(settings);
    if (refresh_extra_)
        refresh_extra_();
    const std::pair<const char*, double> values[]{{"toneExposure", settings_.exposure},
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
                                                  {"toneSharpening", settings_.sharpening},
                                                  {"toneBlackPoint", settings_.black_point},
                                                  {"toneWhitePoint", settings_.white_point},
                                                  {"toneGamma", settings_.gamma}};
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
          std::pair{"toneGradingBalance", settings_.grading_balance}}) {
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
} // namespace huelith::desktop
