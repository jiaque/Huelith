#pragma once
#include <QDialog>
#include "application_icon.h"
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
    dialog.show();
    loop.exec();
    dialog.hide();
    return dialog.result();
}
}
