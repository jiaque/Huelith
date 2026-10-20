#include "canvas_preferences.h"
#include "main_window.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>

namespace huelith::desktop {
namespace {
QString choose_workspace(MainWindow* window, const QString& title, bool creating) {
    QDialog dialog(window);
    dialog.setObjectName("workspaceDialog");
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName("workspaceName");
    name->setMaxLength(80);
    auto* choice = new QComboBox(&dialog);
    choice->setObjectName("workspaceChoice");
    choice->addItems(QSettings().value("workspace/savedLayouts").toMap().keys());
    name->setVisible(creating);
    choice->setVisible(!creating);
    form->addRow(QObject::tr("名称"),
                 creating ? static_cast<QWidget*>(name) : static_cast<QWidget*>(choice));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!(creating ? name->text().trimmed() : choice->currentText()).isEmpty())
            dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return creating ? name->text().trimmed() : choice->currentText();
}
bool restore_workspace(MainWindow* window, const QString& name) {
    const auto saved =
        QSettings().value("workspace/savedLayouts").toMap().value(name).toByteArray();
    const auto previous = window->saveState(1);
    if (saved.isEmpty() || !window->restoreState(saved, 1)) {
        window->restoreState(previous, 1);
        window->statusBar()->showMessage(QObject::tr("工作区布局不可用，已保留当前布局"), 5000);
        return false;
    }
    window->apply_workspace_preferences();
    QSettings().setValue("workspace/currentName", name);
    return true;
}
} // namespace
void add_workspace_actions(QMenu* menu, MainWindow* window) {
    menu->addSeparator();
    auto* save = menu->addAction(QObject::tr("保存工作区为…"));
    QObject::connect(save, &QAction::triggered, window, [window] {
        const auto name = choose_workspace(window, QObject::tr("保存工作区"), true);
        if (name.isEmpty())
            return;
        QSettings settings;
        auto layouts = settings.value("workspace/savedLayouts").toMap();
        layouts.insert(name, window->saveState(1));
        settings.setValue("workspace/savedLayouts", layouts);
        settings.setValue("workspace/currentName", name);
        settings.sync();
    });
    auto* load = menu->addAction(QObject::tr("切换工作区…"));
    QObject::connect(load, &QAction::triggered, window, [window] {
        const auto name = choose_workspace(window, QObject::tr("切换工作区"), false);
        if (!name.isEmpty())
            restore_workspace(window, name);
    });
    auto* reset = menu->addAction(QObject::tr("重置当前工作区"));
    QObject::connect(reset, &QAction::triggered, window, [window] {
        const auto name = QSettings().value("workspace/currentName").toString();
        if (name.isEmpty())
            window->reset_workspace();
        else
            restore_workspace(window, name);
    });
    auto* remove = menu->addAction(QObject::tr("删除工作区…"));
    QObject::connect(remove, &QAction::triggered, window, [window] {
        const auto name = choose_workspace(window, QObject::tr("删除工作区"), false);
        if (name.isEmpty())
            return;
        QSettings settings;
        auto layouts = settings.value("workspace/savedLayouts").toMap();
        layouts.remove(name);
        settings.setValue("workspace/savedLayouts", layouts);
        if (settings.value("workspace/currentName").toString() == name)
            settings.remove("workspace/currentName");
        settings.sync();
    });
    auto* lock = menu->addAction(QObject::tr("锁定工作区"));
    lock->setCheckable(true);
    lock->setChecked(read_canvas_preferences().lock_workspace);
    QObject::connect(lock, &QAction::triggered, window, [window, lock] {
        const bool locked = !read_canvas_preferences().lock_workspace;
        QSettings().setValue("workspace/locked", locked);
        lock->setChecked(locked);
        window->apply_workspace_preferences();
    });
    QObject::connect(menu, &QMenu::aboutToShow, window, [load, remove, reset, lock] {
        const QSettings settings;
        const bool any = !settings.value("workspace/savedLayouts").toMap().isEmpty();
        load->setEnabled(any);
        remove->setEnabled(any);
        reset->setEnabled(!settings.value("workspace/currentName").toString().isEmpty());
        lock->setChecked(read_canvas_preferences().lock_workspace);
    });
}
} // namespace huelith::desktop
