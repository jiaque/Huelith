#include "tone_panel.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace huelith::desktop {
namespace {
class CurveEditor final : public QWidget {
  public:
    explicit CurveEditor(std::vector<QPointF>& points, QWidget* parent)
        : QWidget(parent), points_(points) {
        setMinimumSize(200, 200);
        setObjectName("toneCurve");
        setToolTip(tr("单击添加节点，拖动调整；右键删除中间节点"));
    }
    std::function<void()> changed;

  protected:
    void paintEvent(QPaintEvent*) override {
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
    std::vector<QPointF>& points_;
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
    categories_->addItems({tr("光线"), tr("颜色"), tr("色阶"), tr("曲线"), tr("颜色混合")});
    categories_->setFixedWidth(92);
    layout->addWidget(categories_);
    pages_ = new QStackedWidget(this);
    layout->addWidget(pages_, 1);
    const auto notify = [this] {
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
        form->addRow(label, spin);
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
    auto* levels = page();
    number(levels, "toneBlackPoint", tr("输入黑场"), settings_.black_point, 0, .499, .001);
    number(levels, "toneGamma", tr("中间调"), settings_.gamma, .1, 10, .01);
    number(levels, "toneWhitePoint", tr("输入白场"), settings_.white_point, .5, 1, .001);
    auto* curve = page();
    auto* editor = new CurveEditor(settings_.curve, this);
    editor->changed = notify;
    curve->addRow(editor);
    auto* reset = new QPushButton(tr("重置曲线"), this);
    reset->setAutoDefault(false);
    curve->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [this, editor, notify] {
        settings_.curve = {{0, 0}, {1, 1}};
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
    connect(categories_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    categories_->setCurrentRow(0);
}
void TonePanel::set_category(int index) {
    categories_->setCurrentRow(index);
}
void TonePanel::set_settings(editing::ToneSettings settings) {
    settings_ = std::move(settings);
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
}
} // namespace huelith::desktop
