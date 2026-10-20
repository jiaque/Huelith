#include "canvas_view.h"
#include "main_window.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QToolButton>

QJsonObject object_command(huelith::desktop::MainWindow* window, const QJsonObject& request) {
    auto* canvas = window->active_canvas();
    if (!canvas)
        throw huelith::editing::EditError("No canvas");
    if (request.value("activate").toBool())
        window->findChild<QToolButton*>("toolButton_28")->click();
    if (request.contains("mode"))
        window->findChild<QComboBox*>("objectMode")->setCurrentIndex(request.value("mode").toInt());
    if (request.contains("all"))
        window->findChild<QCheckBox*>("objectAllLayers")->setChecked(request.value("all").toBool());
    if (request.contains("showAll"))
        window->findChild<QCheckBox*>("objectShowAll")
            ->setChecked(request.value("showAll").toBool());
    if (request.contains("hover")) {
        const auto p = request.value("hover").toArray();
        const auto point = canvas->view_transform().map(QPointF(p[0].toDouble(), p[1].toDouble()));
        QMouseEvent event(QEvent::MouseMove, point, canvas->mapToGlobal(point.toPoint()),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &event);
    }
    if (request.contains("gesture")) {
        const auto points = request.value("gesture").toArray();
        const auto mods = Qt::KeyboardModifiers(request.value("modifiers").toInt());
        for (int i = 0; i < points.size(); ++i) {
            const auto p = points[i].toArray();
            const auto point =
                canvas->view_transform().map(QPointF(p[0].toDouble(), p[1].toDouble()));
            const auto type = i == 0                   ? QEvent::MouseButtonPress
                              : i + 1 == points.size() ? QEvent::MouseButtonRelease
                                                       : QEvent::MouseMove;
            QMouseEvent event(type, point, canvas->mapToGlobal(point.toPoint()),
                              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                              mods);
            QApplication::sendEvent(canvas, &event);
            if (i == 1 && request.value("escape").toBool()) {
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(canvas, &escape);
            }
        }
    }
    if (request.contains("save"))
        canvas->project().state().selection.save(request.value("save").toString());
    if (request.contains("capture"))
        window->grab().save(request.value("capture").toString());
    return {{"ready", canvas->property("objectReady").toBool()},
            {"failed", canvas->property("objectError").toBool()},
            {"count", canvas->property("objectCount").toInt()},
            {"hover", canvas->property("objectHover").toInt()},
            {"status", canvas->property("objectStatus").toString()},
            {"tool", int(canvas->tool())}};
}
