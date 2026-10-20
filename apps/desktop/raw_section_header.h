#pragma once
#include <QHBoxLayout>
#include <QPainter>
#include <QSvgRenderer>
#include <QToolButton>

namespace huelith::desktop {
struct RawSectionHeader {
    QHBoxLayout* layout;
    QToolButton* expand;
    QToolButton* enabled;
};
inline RawSectionHeader raw_section_header(QWidget* parent, const QString& label,
                                           const QString& name, const QString& enabled_name,
                                           bool open = false, bool active = true) {
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* expand = new QToolButton(parent);
    expand->setObjectName(name);
    expand->setText(label);
    expand->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    expand->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    expand->setMinimumHeight(34);
    expand->setCheckable(true);
    expand->setChecked(open);
    expand->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    layout->addWidget(expand, 1);
    auto* enabled = new QToolButton(parent);
    enabled->setObjectName(enabled_name);
    enabled->setCheckable(true);
    enabled->setChecked(active);
    enabled->setFixedSize(30, 34);
    enabled->setToolTip(QObject::tr("启用/停用本组调整，保留参数"));
    enabled->setAccessibleName(label + QObject::tr("效果开关"));
    QPixmap icon(36, 36);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    QSvgRenderer eye(QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><g "
                                "fill='none' stroke='#c6c6c6' stroke-width='1.5'><path d='M2 12Q12 "
                                "1 22 12Q12 23 2 12Z'/><circle cx='12' cy='12' r='3'/></g></svg>"));
    eye.render(&painter);
    painter.end();
    icon.setDevicePixelRatio(2);
    enabled->setIcon(QIcon(icon));
    enabled->setStyleSheet("QToolButton {border:0;} QToolButton:checked {background:#34383b;} "
                           "QToolButton:!checked {background:#202427;}");
    layout->addWidget(enabled);
    return {layout, expand, enabled};
}
} // namespace huelith::desktop
