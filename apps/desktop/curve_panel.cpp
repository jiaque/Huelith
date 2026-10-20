#include "curve_panel.h"
#include "color_band_slider.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
namespace {
class CurveCanvas final : public QWidget {
  public:
    CurveCanvas(editing::ToneSettings& settings, QWidget* parent) : QWidget(parent), s(settings) {
        setObjectName("toneCurve");
        setMinimumSize(200, 210);
        setMaximumHeight(310);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }
    editing::ToneSettings& s;
    int mode{}, selected{-1};
    std::array<std::array<int, 256>, 4> bins{};
    std::function<void()> changed, selection_changed;
    std::vector<QPointF>& points() {
        return mode <= 1 ? s.curve : s.channel_curves[mode - 2];
    }
    void refresh() {
        if (selected >= int(points().size()))
            selected = -1;
        update();
        if (selection_changed)
            selection_changed();
    }
    void set_value(bool input, double value) {
        if (selected < 0)
            return;
        auto p = points()[selected];
        if (input) {
            if (selected == 0 || selected + 1 == int(points().size()))
                return;
            p.setX(std::clamp(value, points()[selected - 1].x() + .001,
                              points()[selected + 1].x() - .001));
        } else
            p.setY(value);
        points()[selected] = p;
        notify();
    }
    QRectF graph() const {
        return QRectF(rect()).adjusted(12, 12, -12, -25);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto area = graph();
        p.fillRect(area, QColor(60, 63, 66));
        const auto& histogram = bins[mode < 2 ? 0 : mode - 1];
        const int peak = *std::max_element(histogram.begin(), histogram.end());
        if (peak) {
            QPolygonF h;
            h << area.bottomLeft();
            for (int i = 0; i < 256; ++i)
                h << QPointF(area.left() + i / 255. * area.width(),
                             area.bottom() - histogram[i] / double(peak) * area.height() * .9);
            h << area.bottomRight();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(28, 30, 32));
            p.drawPolygon(h);
        }
        p.setPen(QPen(QColor(105, 108, 110), .6));
        for (int i = 0; i <= 4; ++i) {
            p.drawLine(QPointF(area.left() + i * area.width() / 4, area.top()),
                       QPointF(area.left() + i * area.width() / 4, area.bottom()));
            p.drawLine(QPointF(area.left(), area.top() + i * area.height() / 4),
                       QPointF(area.right(), area.top() + i * area.height() / 4));
        }
        p.setPen(QPen(QColor(135, 135, 135), 1, Qt::DashLine));
        p.drawLine(area.bottomLeft(), area.topRight());
        QPolygonF line;
        for (int i = 0; i <= 256; ++i) {
            const double x = i / 256., y = mode == 0
                                               ? editing::evaluate_parametric_curve(x, s)
                                               : editing::evaluate_point_curve(
                                                     x, points(), s.legacy_curve_interpolation);
            line << QPointF(area.left() + x * area.width(), area.bottom() - y * area.height());
        }
        const QColor colors[]{QColor(220, 220, 220), QColor(220, 220, 220), QColor(240, 85, 85),
                              QColor(80, 210, 100), QColor(75, 150, 250)};
        p.setPen(QPen(colors[mode], 1.7));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);
        if (mode > 0) {
            for (std::size_t i = 0; i < points().size(); ++i) {
                p.setBrush(int(i) == selected ? QColor(70, 160, 235) : QColor(230, 230, 230));
                p.drawEllipse(QPointF(area.left() + points()[i].x() * area.width(),
                                      area.bottom() - points()[i].y() * area.height()),
                              3.5, 3.5);
            }
        }
        QLinearGradient ramp(area.bottomLeft(), area.bottomRight());
        ramp.setColorAt(0, Qt::black);
        ramp.setColorAt(1, Qt::white);
        p.fillRect(QRectF(area.left(), area.bottom() + 5, area.width(), 9), ramp);
        if (mode == 0) {
            p.setPen(Qt::black);
            p.setBrush(QColor(195, 195, 195));
            for (double split : s.curve_splits) {
                const double x = area.left() + split * area.width();
                p.drawPolygon(QPolygonF{QPointF(x, area.bottom() + 10),
                                        QPointF(x - 5, area.bottom() + 19),
                                        QPointF(x + 5, area.bottom() + 19)});
            }
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        setFocus();
        start_ = s;
        const auto area = graph();
        const QPointF n = normalized(e->position());
        if (mode == 0) {
            split_ = -1;
            double nearest = 9;
            for (int i = 0; i < 3; ++i) {
                const double d =
                    std::abs(e->position().x() - area.left() - s.curve_splits[i] * area.width());
                if (d < nearest && e->position().y() > area.bottom()) {
                    split_ = i;
                    nearest = d;
                }
            }
            setProperty("curveDragging", split_ >= 0);
            return;
        }
        selected = -1;
        double nearest = 8;
        for (std::size_t i = 0; i < points().size(); ++i) {
            const QPointF pos(area.left() + points()[i].x() * area.width(),
                              area.bottom() - points()[i].y() * area.height());
            const double d = QLineF(pos, e->position()).length();
            if (d < nearest) {
                selected = int(i);
                nearest = d;
            }
        }
        if (e->button() == Qt::RightButton) {
            erase();
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        if (selected < 0 && points().size() < 64 && n.x() > .001 && n.x() < .999) {
            auto at = std::upper_bound(points().begin(), points().end(), n.x(),
                                       [](double x, QPointF p) { return x < p.x(); });
            if (n.x() - (at - 1)->x() < .002 || at->x() - n.x() < .002)
                return;
            selected = int(at - points().begin());
            points().insert(at, n);
        }
        setProperty("curveDragging", selected >= 0);
        notify();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!e->buttons().testFlag(Qt::LeftButton) || !property("curveDragging").toBool())
            return;
        auto point = normalized(e->position());
        if (mode == 0 && split_ >= 0) {
            s.curve_splits[split_] =
                std::clamp(point.x(), split_ == 0 ? .01 : s.curve_splits[split_ - 1] + .01,
                           split_ == 2 ? .99 : s.curve_splits[split_ + 1] - .01);
            notify();
            return;
        }
        if (selected < 0 || mode == 0)
            return;
        if (selected == 0)
            point.setX(0);
        else if (selected + 1 == int(points().size()))
            point.setX(1);
        else
            point.setX(std::clamp(point.x(), points()[selected - 1].x() + .001,
                                  points()[selected + 1].x() - .001));
        points()[selected] = point;
        notify();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        setProperty("curveDragging", false);
        split_ = -1;
        notify();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape && property("curveDragging").toBool()) {
            s = start_;
            setProperty("curveDragging", false);
            split_ = -1;
            notify();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
            erase();
            e->accept();
            return;
        }
        if (mode > 0 && selected >= 0) {
            const double step = (e->modifiers().testFlag(Qt::ShiftModifier) ? 10. : 1.) / 255;
            const auto p = points()[selected];
            switch (e->key()) {
            case Qt::Key_Left:
                set_value(true, p.x() - step);
                break;
            case Qt::Key_Right:
                set_value(true, p.x() + step);
                break;
            case Qt::Key_Up:
                set_value(false, std::min(1., p.y() + step));
                break;
            case Qt::Key_Down:
                set_value(false, std::max(0., p.y() - step));
                break;
            default:
                QWidget::keyPressEvent(e);
                return;
            }
            e->accept();
            return;
        }
        QWidget::keyPressEvent(e);
    }

  private:
    editing::ToneSettings start_;
    int split_{-1};
    QPointF normalized(QPointF p) const {
        const auto a = graph();
        return {std::clamp((p.x() - a.left()) / a.width(), 0., 1.),
                std::clamp((a.bottom() - p.y()) / a.height(), 0., 1.)};
    }
    void notify() {
        refresh();
        if (changed)
            changed();
    }
    void erase() {
        if (mode > 0 && selected > 0 && selected + 1 < int(points().size())) {
            points().erase(points().begin() + selected);
            selected = -1;
            notify();
        }
    }
};
} // namespace
CurvePanel::CurvePanel(editing::ToneSettings& settings, std::function<void()> changed,
                       QWidget* parent)
    : QWidget(parent) {
    setObjectName("curvePanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabBar(this);
    tabs->setObjectName("toneCurveTabs");
    tabs->setExpanding(false);
    for (const auto& text :
         QStringList{QStringLiteral("∿"), QStringLiteral("○"), QStringLiteral("●"),
                     QStringLiteral("●"), QStringLiteral("●")})
        tabs->addTab(text);
    const QStringList tips{tr("参数曲线"), tr("点曲线：RGB"), tr("红色通道"), tr("绿色通道"),
                           tr("蓝色通道")};
    for (int i = 0; i < 5; ++i)
        tabs->setTabToolTip(i, tips[i]);
    tabs->setTabTextColor(2, QColor(230, 70, 70));
    tabs->setTabTextColor(3, QColor(80, 210, 110));
    tabs->setTabTextColor(4, QColor(70, 140, 250));
    root->addWidget(tabs);
    auto* channel = new QComboBox(this);
    channel->setObjectName("toneCurveChannel");
    channel->addItems({tr("RGB"), tr("红"), tr("绿"), tr("蓝")});
    channel->hide();
    auto* canvas = new CurveCanvas(settings, this);
    root->addWidget(canvas);
    auto* preset = new QComboBox(this);
    preset->setObjectName("toneCurvePreset");
    preset->addItems({tr("线性"), tr("中等对比度"), tr("强对比度"), tr("自定")});
    root->addWidget(preset);
    const std::array<std::vector<QPointF>, 3> presets{
        {{{0, 0}, {1, 1}},
         {{0, 0}, {.25, .2}, {.5, .5}, {.75, .8}, {1, 1}},
         {{0, 0}, {.25, .15}, {.5, .5}, {.75, .85}, {1, 1}}}};
    connect(preset, &QComboBox::activated, this,
            [this, &settings, canvas, tabs, presets, changed](int index) {
                if (index >= 3)
                    return;
                if (canvas->mode == 0)
                    tabs->setCurrentIndex(1);
                canvas->points() = presets[index];
                settings.legacy_curve_interpolation = false;
                canvas->selected = -1;
                refresh();
                changed();
            });
    auto* parameters = new QWidget(this);
    auto* param_layout = new QVBoxLayout(parameters);
    param_layout->setContentsMargins(0, 0, 0, 0);
    root->addWidget(parameters);
    std::array<QDoubleSpinBox*, 4> values{};
    std::array<ColorBandSlider*, 4> sliders{};
    const QStringList labels{tr("阴影"), tr("暗调"), tr("亮调"), tr("高光")};
    for (int i = 3; i >= 0; --i) {
        auto* line = new QHBoxLayout;
        line->addWidget(new QLabel(labels[i], this));
        auto* value = new QDoubleSpinBox(this);
        values[i] = value;
        value->setObjectName(QString("toneParametric%1").arg(i));
        value->setRange(-100, 100);
        value->setDecimals(0);
        value->setButtonSymbols(QAbstractSpinBox::NoButtons);
        value->setFixedWidth(58);
        line->addStretch();
        line->addWidget(value);
        param_layout->addLayout(line);
        auto* slider = new ColorBandSlider(-1, 0, this);
        sliders[i] = slider;
        slider->setObjectName(QString("toneParametric%1Slider").arg(i));
        slider->setRange(-100, 100);
        slider->set_default(0);
        slider->setTracking(false);
        param_layout->addWidget(slider);
        connect(slider, &QSlider::valueChanged, value, [value](int n) { value->setValue(n); });
        connect(value, &QDoubleSpinBox::valueChanged, this,
                [&, i, slider, canvas, changed](double v) {
                    settings.parametric_curve[i] = v;
                    const QSignalBlocker block(slider);
                    slider->setValue(qRound(v));
                    canvas->update();
                    changed();
                });
    }
    auto* precise = new QWidget(this);
    auto* numbers = new QHBoxLayout(precise);
    numbers->setContentsMargins(0, 0, 0, 0);
    root->addWidget(precise);
    auto* input = new QDoubleSpinBox(this);
    auto* output = new QDoubleSpinBox(this);
    input->setObjectName("toneCurveInput");
    output->setObjectName("toneCurveOutput");
    for (auto* value : {input, output}) {
        value->setRange(0, 255);
        value->setDecimals(0);
        value->setFixedWidth(62);
    }
    numbers->addWidget(new QLabel(tr("输入"), this));
    numbers->addWidget(input);
    numbers->addWidget(new QLabel(tr("输出"), this));
    numbers->addWidget(output);
    canvas->selection_changed = [canvas, input, output] {
        const QSignalBlocker a(input), b(output);
        const bool valid = canvas->selected >= 0;
        input->setEnabled(valid && canvas->selected > 0 &&
                          canvas->selected + 1 < int(canvas->points().size()));
        output->setEnabled(valid);
        if (valid) {
            input->setValue(canvas->points()[canvas->selected].x() * 255);
            output->setValue(canvas->points()[canvas->selected].y() * 255);
        }
    };
    connect(input, &QDoubleSpinBox::valueChanged, this,
            [canvas](double v) { canvas->set_value(true, v / 255); });
    connect(output, &QDoubleSpinBox::valueChanged, this,
            [canvas](double v) { canvas->set_value(false, v / 255); });
    auto* reset = new QPushButton(tr("重置曲线"), this);
    reset->setObjectName("toneCurveReset");
    reset->setAutoDefault(false);
    root->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [&, canvas, changed] {
        if (canvas->mode == 0) {
            settings.parametric_curve = {};
            settings.curve_splits = {.25, .5, .75};
        } else
            canvas->points() = {{0, 0}, {1, 1}};
        canvas->selected = -1;
        refresh();
        changed();
    });
    connect(tabs, &QTabBar::currentChanged, this, [canvas, channel, parameters, precise](int mode) {
        canvas->mode = mode;
        canvas->selected = -1;
        const QSignalBlocker blocker(channel);
        if (mode > 0)
            channel->setCurrentIndex(mode - 1);
        parameters->setVisible(mode == 0);
        precise->setVisible(mode > 0);
        canvas->refresh();
    });
    connect(channel, &QComboBox::currentIndexChanged, this,
            [tabs](int channel_index) { tabs->setCurrentIndex(channel_index + 1); });
    const auto refresh_preset = [canvas, preset, presets] {
        int selected = 3;
        for (int i = 0; i < 3; ++i)
            if (canvas->points() == presets[i])
                selected = i;
        const QSignalBlocker blocker(preset);
        preset->setCurrentIndex(selected);
    };
    connect(tabs, &QTabBar::currentChanged, this, [refresh_preset] { refresh_preset(); });
    canvas->changed = [changed, refresh_preset] {
        refresh_preset();
        changed();
    };
    refresh_ = [&settings, canvas, values, sliders, preset, presets] {
        int selected = 3;
        for (int i = 0; i < 3; ++i)
            if (canvas->points() == presets[i])
                selected = i;
        preset->setCurrentIndex(selected);
        for (int i = 0; i < 4; ++i) {
            const QSignalBlocker a(values[i]), b(sliders[i]);
            values[i]->setValue(settings.parametric_curve[i]);
            sliders[i]->setValue(qRound(settings.parametric_curve[i]));
        }
        canvas->refresh();
    };
    histogram_ = [canvas](const QImage& source) {
        canvas->bins = {};
        if (!source.isNull()) {
            const auto image = source.convertToFormat(QImage::Format_RGB32);
            const int step =
                std::max(1, int(std::sqrt(double(image.width()) * image.height() / 30000)));
            for (int y = 0; y < image.height(); y += step) {
                const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); x += step) {
                    const auto p = row[x];
                    ++canvas->bins[0][qGray(p)];
                    ++canvas->bins[1][qRed(p)];
                    ++canvas->bins[2][qGreen(p)];
                    ++canvas->bins[3][qBlue(p)];
                }
            }
        }
        canvas->update();
    };
    precise->hide();
    refresh();
}
void CurvePanel::refresh() {
    if (refresh_)
        refresh_();
}
void CurvePanel::histogram(const QImage& image) {
    if (histogram_)
        histogram_(image);
}
} // namespace huelith::desktop
