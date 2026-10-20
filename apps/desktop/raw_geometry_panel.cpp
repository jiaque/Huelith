#include "raw_geometry_panel.h"
#include "color_band_slider.h"
#include "raw_section_header.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
namespace huelith::desktop {
RawGeometryPanel::RawGeometryPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("rawGeometryPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    const auto header =
        raw_section_header(this, tr("几何"), "rawGeometryExpand", "rawGeometryEnable");
    auto* expand = header.expand;
    root->addLayout(header.layout);
    flags_.emplace_back(header.enabled, &editing::RawGeometry::enabled);
    connect(header.enabled, &QToolButton::toggled, this, [this](bool enabled) {
        if (loading_)
            return;
        if (gesture_started)
            gesture_started();
        settings_.enabled = enabled;
        if (changed)
            changed();
        if (gesture_finished)
            gesture_finished();
    });
    auto* body = new QWidget(this);
    auto* form = new QFormLayout(body);
    root->addWidget(body);
    body->hide();
    connect(expand, &QToolButton::toggled, this, [body, expand](bool enabled) {
        body->setVisible(enabled);
        expand->setArrowType(enabled ? Qt::DownArrow : Qt::RightArrow);
    });
    const auto flag = [this, form](const char* name, const QString& text,
                                   bool editing::RawGeometry::* member) {
        auto* check = new QCheckBox(text, this);
        check->setObjectName(QLatin1String(name));
        check->setChecked(settings_.*member);
        flags_.emplace_back(check, member);
        form->addRow(check);
        connect(check, &QCheckBox::toggled, this, [this, member](bool value) {
            if (loading_)
                return;
            if (gesture_started)
                gesture_started();
            settings_.*member = value;
            if (changed)
                changed();
            if (gesture_finished)
                gesture_finished();
        });
    };
    flag("rawGeometryConstrain", tr("约束裁剪"), &editing::RawGeometry::constrain_crop);
    const auto number = [this, form](const char* name, const QString& label, double low,
                                     double high, double editing::RawGeometry::* member) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label, this));
        row->addStretch();
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QLatin1String(name));
        spin->setRange(low, high);
        spin->setDecimals(1);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setFixedWidth(62);
        spin->setKeyboardTracking(false);
        spin->setValue(settings_.*member);
        row->addWidget(spin);
        form->addRow(row);
        auto* slider = new ColorBandSlider(-1, 0, this);
        slider->setObjectName(QString::fromLatin1(name) + "Slider");
        slider->setRange(qRound(low * 10), qRound(high * 10));
        slider->setValue(qRound(settings_.*member * 10));
        slider->set_default(slider->value());
        form->addRow(slider);
        numbers_.emplace_back(spin, member);
        connect(slider, &QSlider::sliderPressed, this, [this] {
            if (gesture_started)
                gesture_started();
        });
        connect(slider, &QSlider::sliderReleased, this, [this] {
            if (gesture_finished)
                gesture_finished();
        });
        connect(slider, &QSlider::valueChanged, spin,
                [spin](int value) { spin->setValue(value / 10.); });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, member](double value) {
            const QSignalBlocker blocker(slider);
            slider->setValue(qRound(value * 10));
            if (loading_)
                return;
            settings_.*member = value;
            if (changed)
                changed();
        });
    };
    number("rawGeometryVertical", tr("垂直"), -100, 100, &editing::RawGeometry::vertical);
    number("rawGeometryHorizontal", tr("水平"), -100, 100, &editing::RawGeometry::horizontal);
    number("rawGeometryRotation", tr("旋转"), -180, 180, &editing::RawGeometry::rotation);
    number("rawGeometryAspect", tr("长宽比"), -100, 100, &editing::RawGeometry::aspect);
    number("rawGeometryScale", tr("缩放"), 10, 400, &editing::RawGeometry::scale);
    number("rawGeometryOffsetX", tr("偏移 X"), -100, 100, &editing::RawGeometry::offset_x);
    number("rawGeometryOffsetY", tr("偏移 Y"), -100, 100, &editing::RawGeometry::offset_y);
    flag("rawGeometryFlipHorizontal", tr("水平翻转"), &editing::RawGeometry::flip_horizontal);
    flag("rawGeometryFlipVertical", tr("垂直翻转"), &editing::RawGeometry::flip_vertical);
    auto* reset = new QPushButton(tr("重置几何"), this);
    reset->setObjectName("rawGeometryReset");
    reset->setAutoDefault(false);
    form->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (gesture_started)
            gesture_started();
        set_settings({});
        if (changed)
            changed();
        if (gesture_finished)
            gesture_finished();
    });
}
void RawGeometryPanel::set_settings(editing::RawGeometry settings) {
    loading_ = true;
    settings_ = settings;
    for (const auto& [spin, member] : numbers_)
        spin->setValue(settings_.*member);
    for (const auto& [check, member] : flags_)
        check->setChecked(settings_.*member);
    loading_ = false;
}
} // namespace huelith::desktop
