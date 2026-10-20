#include "raw_white_balance_panel.h"
#include "color_band_slider.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
namespace huelith::desktop {
RawWhiteBalancePanel::RawWhiteBalancePanel(QWidget* parent) : QWidget(parent) {
    setObjectName("rawWhiteBalancePanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("白平衡"), this));
    mode_ = new QComboBox(this);
    mode_->setObjectName("rawWhiteBalanceMode");
    const QStringList names{tr("拍摄时设置"), tr("自动"),   tr("日光"),   tr("阴天"), tr("阴影"),
                            tr("钨丝灯"),     tr("荧光灯"), tr("闪光灯"), tr("自定")};
    const QStringList modes{"as-shot",  "auto",        "daylight", "cloudy", "shade",
                            "tungsten", "fluorescent", "flash",    "custom"};
    for (int i = 0; i < names.size(); ++i)
        mode_->addItem(names[i], modes[i]);
    top->addWidget(mode_, 1);
    picker_ = new QToolButton(this);
    picker_->setObjectName("rawWhiteBalancePicker");
    picker_->setAccessibleName(tr("白平衡吸管"));
    QPixmap icon(48, 48);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    QSvgRenderer renderer(
        QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><g fill='none' "
                   "stroke='#c8cdd2' stroke-width='1.6' stroke-linejoin='round'><path d='M14 5l5 "
                   "5M16 3q2-2 4 0t0 4l-3 3-3-3zM15 8L5 18l-2 3 4-1L17 10M5 17l3 3'/></g></svg>"));
    renderer.render(&painter);
    painter.end();
    icon.setDevicePixelRatio(2);
    picker_->setIcon(QIcon(icon));
    picker_->setFixedSize(26, 26);
    picker_->setCheckable(true);
    picker_->setToolTip(tr("白平衡工具 (I)：点击中性灰区域；Esc退出"));
    top->addWidget(picker_);
    layout->addLayout(top);
    const auto row = [&](const QString& name, const QString& title, bool temperature) {
        auto* line = new QHBoxLayout;
        line->addWidget(new QLabel(title, this));
        line->addStretch();
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(name);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setRange(temperature ? 2000 : -150, temperature ? 50000 : 150);
        spin->setDecimals(temperature ? 0 : 1);
        spin->setSingleStep(temperature ? 50 : 1);
        spin->setFixedWidth(72);
        line->addWidget(spin);
        layout->addLayout(line);
        auto* slider = new ColorBandSlider(-1, 0, this);
        slider->setObjectName(name + "Slider");
        slider->setRange(temperature ? 0 : -1500, temperature ? 1000 : 1500);
        slider->set_colors(temperature ? QColor("#6595d1") : QColor("#67a568"),
                           temperature ? QColor("#dfbd62") : QColor("#b879b8"));
        layout->addWidget(slider);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, slider, temperature](double value) {
                    {
                        QSignalBlocker block(slider);
                        slider->setValue(temperature
                                             ? qRound(std::log(value / 2000) / std::log(25.) * 1000)
                                             : qRound(value * 10));
                    }
                    if (temperature)
                        settings_.temperature = value;
                    else
                        settings_.tint = value;
                    settings_.mode = "custom";
                    {
                        QSignalBlocker block(mode_);
                        mode_->setCurrentIndex(8);
                    }
                    if (changed)
                        changed();
                });
        connect(slider, &QSlider::valueChanged, spin, [spin, temperature](int value) {
            spin->setValue(temperature ? 2000 * std::pow(25., value / 1000.) : value / 10.);
        });
        connect(slider, &QSlider::sliderPressed, this, [this] {
            if (gesture_started)
                gesture_started();
        });
        connect(slider, &QSlider::sliderReleased, this, [this] {
            if (gesture_finished)
                gesture_finished();
        });
        return spin;
    };
    temperature_ = row("rawWhiteBalanceTemperature", tr("色温"), true);
    tint_ = row("rawWhiteBalanceTint", tr("色调"), false);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == 1) {
            if (automatic)
                automatic();
            return;
        }
        if (index == 0)
            settings_ = shot_;
        else if (index < 8) {
            constexpr double values[]{5500, 6500, 7500, 2850, 3800, 5500};
            settings_.temperature = values[index - 2];
            settings_.tint = index == 6 ? 20 : 0;
        }
        settings_.mode = mode_->currentData().toString();
        refresh();
        if (changed)
            changed();
    });
    connect(picker_, &QToolButton::toggled, this, [this] {
        if (view_changed)
            view_changed();
    });
    refresh();
}
void RawWhiteBalancePanel::refresh() {
    const QSignalBlocker a(mode_), b(temperature_), c(tint_);
    mode_->setCurrentIndex(mode_->findData(settings_.mode));
    temperature_->setValue(settings_.temperature);
    tint_->setValue(settings_.tint);
    auto* temp =
        static_cast<ColorBandSlider*>(findChild<QSlider*>("rawWhiteBalanceTemperatureSlider"));
    auto* tint = static_cast<ColorBandSlider*>(findChild<QSlider*>("rawWhiteBalanceTintSlider"));
    const QSignalBlocker d(temp), e(tint);
    temp->setValue(qRound(std::log(settings_.temperature / 2000) / std::log(25.) * 1000));
    tint->setValue(qRound(settings_.tint * 10));
    temp->set_default(qRound(std::log(shot_.temperature / 2000) / std::log(25.) * 1000));
    tint->set_default(qRound(shot_.tint * 10));
}
void RawWhiteBalancePanel::set_settings(editing::WhiteBalance value, editing::WhiteBalance shot,
                                        bool available) {
    settings_ = value;
    shot_ = shot;
    if (settings_.mode == "as-shot")
        settings_ = shot_;
    setEnabled(available);
    refresh();
}
bool RawWhiteBalancePanel::sampling() const {
    return picker_->isChecked();
}
void RawWhiteBalancePanel::stop_sampling() {
    picker_->setChecked(false);
}
} // namespace huelith::desktop
