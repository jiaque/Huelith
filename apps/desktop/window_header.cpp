#include "window_header.h"
#include "window_icons.h"
#include "application_icon.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QWindow>

namespace huelith::desktop {
WindowHeader::WindowHeader(QMenuBar* menus, QWidget* host) : QWidget(host) {
    host->setWindowIcon(application_icon());
    setObjectName("windowHeader");
    setFixedHeight(36);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(0);
    auto* title = new QLabel(this);
    title->setObjectName("appTitle");
    title->setAccessibleName(tr("Huelith"));
    title->setToolTip(tr("Huelith"));
    title->setFixedSize(24, 24);
    title->setPixmap(application_icon().pixmap(QSize(24, 24), devicePixelRatioF()));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(title);
    layout->addSpacing(4);
    menus->setParent(this);
    layout->addWidget(menus);
    layout->addStretch();
    const auto add_button = [this, layout](WindowIcon icon, const QString& name,
                                          const QString& accessible) {
        auto* button = new QPushButton(this);
        button->setIcon(window_icon(icon));
        button->setIconSize(QSize(16, 16));
        button->setObjectName(name);
        button->setAccessibleName(accessible);
        button->setToolTip(accessible);
        button->setFixedSize(46, 36);
        layout->addWidget(button);
        return button;
    };
    auto* minimize = add_button(WindowIcon::minimize, "minimizeWindow", tr("最小化"));
    maximize_ = add_button(WindowIcon::maximize, "maximizeWindow", tr("最大化"));
    auto* close = add_button(WindowIcon::close, "closeWindow", tr("关闭窗口"));
    connect(minimize, &QPushButton::clicked, host, &QWidget::showMinimized);
    connect(maximize_, &QPushButton::clicked, this, &WindowHeader::toggle_maximized);
    connect(close, &QPushButton::clicked, host, &QWidget::close);
    host->installEventFilter(this);
}

void WindowHeader::toggle_maximized() {
    if (window()->isMaximized()) {
        window()->showNormal();
    } else {
        window()->showMaximized();
    }
}

void WindowHeader::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WindowHeader::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        toggle_maximized();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

bool WindowHeader::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window() && event->type() == QEvent::WindowStateChange) {
        const bool maximized = window()->isMaximized();
        maximize_->setIcon(window_icon(maximized ? WindowIcon::restore : WindowIcon::maximize));
        maximize_->setAccessibleName(maximized ? tr("还原") : tr("最大化"));
        maximize_->setToolTip(maximize_->accessibleName());
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace huelith::desktop
