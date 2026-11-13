#pragma once
#include "application_icon.h"
#include "workspace_layout.h"
#include <QDialog>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>

namespace huelith::desktop {
// Keep the existing stack-owned controls alive without QDialog::exec's modality.
// Windows receives an unowned top-level window, hence an independent taskbar entry.
inline int run_work_window(QDialog& dialog, QWidget* owner) {
    const QPointer<QWidget> lifetime(owner);
    dialog.setParent(nullptr, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint |
                     (dialog.windowFlags() & Qt::FramelessWindowHint));
    dialog.setWindowIcon(application_icon());
    dialog.setWindowModality(Qt::NonModal);
    dialog.setAttribute(Qt::WA_QuitOnClose,false);
    dialog.setProperty("independentWorkWindow",true);
    QEventLoop loop;
    QObject::connect(&dialog,&QDialog::finished,&loop,&QEventLoop::quit);
    QTimer watch;
    QObject::connect(&watch,&QTimer::timeout,&dialog,[&] {
        if(!lifetime || !lifetime->isVisible()) dialog.reject();
    });
    watch.start(100);
    const QString layoutKey = "workspace/windows/" +
                              QString::fromLatin1(dialog.metaObject()->className()) + "/" +
                              dialog.objectName();
    restore_workspace_window(&dialog, layoutKey, owner);
    dialog.show();
    loop.exec();
    QSettings().setValue(layoutKey + "/geometry", dialog.saveGeometry());
    dialog.hide();
    return dialog.result();
}
}
