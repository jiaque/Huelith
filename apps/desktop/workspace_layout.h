#pragma once
#include <QAbstractSpinBox>
#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <algorithm>

namespace huelith::desktop {
class WorkspacePanelToggle final : public QObject {
  public:
    WorkspacePanelToggle(QWidget* window, const QList<QWidget*>& panels, QToolButton* button)
        : QObject(window), button_(button), window_(window) {
        for (auto* panel : panels)
            panels_.append(panel);
        qApp->installEventFilter(this);
        connect(button, &QToolButton::clicked, this, [this] { toggle(); });
    }

  protected:
    bool eventFilter(QObject* receiver, QEvent* event) override {
        if (event->type() != QEvent::KeyPress)
            return false;
        auto* widget = qobject_cast<QWidget*>(receiver);
        if (!widget || widget->window() != window_)
            return false;
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() != Qt::Key_Tab && key->key() != Qt::Key_Backtab)
            return false;
        auto* focus = QApplication::focusWidget();
        if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus) ||
            qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus))
            return false;
        if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            return false;
        toggle(!key->modifiers().testFlag(Qt::ShiftModifier));
        return true;
    }

  private:
    void toggle(bool tools = false) {
        if (!hidden_) {
            visible_.clear();
            tools_.clear();
            if (tools)
                for (auto* toolbar : window_->findChildren<QToolBar*>())
                    if (toolbar->isVisible()) {
                        tools_.append(toolbar);
                        toolbar->hide();
                    }
            for (const auto& panel : panels_) {
                visible_.append(panel && panel->isVisible());
                if (panel)
                    panel->hide();
            }
        } else {
            for (const auto& toolbar : tools_)
                if (toolbar)
                    toolbar->show();
            for (int i = 0; i < panels_.size(); ++i)
                if (panels_[i])
                    panels_[i]->setVisible(visible_.value(i));
        }
        hidden_ = !hidden_;
        button_->setText(hidden_ ? tr("展开侧栏") : tr("收起侧栏"));
    }
    QList<QPointer<QWidget>> panels_;
    QList<QPointer<QToolBar>> tools_;
    QList<bool> visible_;
    QToolButton* button_;
    QWidget* window_;
    bool hidden_{};
};

inline QToolButton* workspace_panel_toggle(QWidget* window, const QList<QWidget*>& panels) {
    auto* button = new QToolButton(window);
    button->setObjectName("workspacePanelToggle");
    button->setText(QObject::tr("收起侧栏"));
    button->setToolTip(QObject::tr("显示或隐藏侧栏（Tab）；编辑文字时仍使用 Tab 切换输入框"));
    new WorkspacePanelToggle(window, panels, button);
    return button;
}
inline void restore_workspace_window(QWidget* window, const QString& key,
                                     QWidget* owner = nullptr) {
    QSettings settings;
    const auto saved = settings.value("workspace/restoreLast", true).toBool()
                           ? settings.value(key + "/geometry").toByteArray()
                           : QByteArray{};
    const bool restored = !saved.isEmpty() && window->restoreGeometry(saved);
    QScreen* screen = owner ? owner->screen() : window->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    if (restored) {
        for (auto* candidate : QGuiApplication::screens())
            if (candidate->availableGeometry().contains(window->geometry().center())) {
                screen = candidate;
                break;
            }
    }
    const QRect area = screen->availableGeometry().adjusted(8, 8, -8, -8);
    window->setMinimumSize(window->minimumSize().boundedTo(area.size()));
    window->resize(window->size().boundedTo(area.size()));
    QPoint position = restored ? window->pos() : area.center() - window->rect().center();
    position.setX(std::clamp(position.x(), area.left(),
                             std::max(area.left(), area.right() - window->width() + 1)));
    position.setY(std::clamp(position.y(), area.top(),
                             std::max(area.top(), area.bottom() - window->height() + 1)));
    window->move(position);
}

// Persist the user's chosen inspector width, not a width temporarily constrained by a small screen.
inline void configure_inspector_splitter(QSplitter* splitter, const QString& key,
                                         int defaultWidth) {
    splitter->setChildrenCollapsible(false);
    splitter->widget(0)->setMinimumWidth(200);
    splitter->widget(1)->setMaximumWidth(900);
    splitter->setHandleWidth(6);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    QObject::connect(splitter, &QSplitter::splitterMoved, splitter,
                     [splitter, key] { QSettings().setValue(key, splitter->sizes().value(1)); });
    QTimer::singleShot(0, splitter, [splitter, key, defaultWidth] {
        const int wanted = QSettings().value(key, defaultWidth).toInt();
        const int available = splitter->width() - splitter->handleWidth();
        splitter->setSizes({std::max(200, available - wanted), wanted});
    });
}
} // namespace huelith::desktop
