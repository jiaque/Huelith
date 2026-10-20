// Python drives production Qt widgets and owns assertions; no test executable.
#include "huelith/editing/project.h"
#include "selection_refine_dialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStandardItemModel>
#include <memory>

QJsonObject refine_command(const QJsonObject& request) {
    static std::unique_ptr<huelith::desktop::SelectionRefineDialog> dialog;
    if (request.value("close").toBool()) {
        dialog.reset();
        return {};
    }
    if (request.contains("image")) {
        QImage image(request.value("image").toString()), mask(request.value("mask").toString());
        mask = mask.convertToFormat(QImage::Format_Grayscale8);
        dialog = std::make_unique<huelith::desktop::SelectionRefineDialog>(
            image, mask, nullptr, request.value("maskOnly").toBool(),
            QImage(request.value("background").toString()));
        dialog->open();
        dialog->raise();
        dialog->activateWindow();
        QApplication::processEvents();
    }
    if (!dialog)
        throw huelith::editing::EditError("No selection refinement window");
    for (auto it = request.begin(); it != request.end(); ++it) {
        if (auto* decimal = dialog->findChild<QDoubleSpinBox*>(it.key()))
            decimal->setValue(it.value().toDouble());
        else if (auto* combo = dialog->findChild<QComboBox*>(it.key()))
            combo->setCurrentIndex(it.value().toInt());
        else if (auto* check = dialog->findChild<QCheckBox*>(it.key()))
            check->setChecked(it.value().toBool());
    }
    auto* view = dialog->findChild<QGraphicsView*>("refinePreview");
    if (request.contains("stroke")) {
        view->setFocus();
        const auto points = request.value("stroke").toArray();
        const auto modifiers = request.value("alt").toBool() ? Qt::AltModifier : Qt::NoModifier;
        for (int i = 0; i < points.size(); ++i) {
            const auto p = points[i].toArray();
            const auto area = view->sceneRect();
            const QPointF position = view->mapFromScene(
                QPointF(p[0].toDouble() * area.width(), p[1].toDouble() * area.height()));
            const auto type = i == 0                   ? QEvent::MouseButtonPress
                              : i + 1 == points.size() ? QEvent::MouseButtonRelease
                                                       : QEvent::MouseMove;
            QMouseEvent mouse(type, position, view->viewport()->mapToGlobal(position.toPoint()),
                              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                              modifiers);
            QApplication::sendEvent(view->viewport(), &mouse);
            if (request.value("escapeStroke").toBool() && i == points.size() - 2) {
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(view, &escape);
                break;
            }
        }
    }
    if (request.contains("key")) {
        const auto combination = QKeySequence(request.value("key").toString())[0];
        QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
        QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers());
        view->setFocus();
        QApplication::sendEvent(view, &press);
        QApplication::sendEvent(view, &release);
    }
    if (request.contains("click")) {
        auto* button = dialog->findChild<QPushButton*>(request.value("click").toString());
        if (!button)
            throw huelith::editing::EditError("Refinement button missing");
        button->click();
    }
    if (request.contains("capture"))
        dialog->grab().save(request.value("capture").toString());
    if (request.contains("display"))
        for (auto* item : view->scene()->items())
            if (auto* pixmap = dynamic_cast<QGraphicsPixmapItem*>(item))
                pixmap->pixmap().save(request.value("display").toString());
    if (request.value("accept").toBool())
        dialog->accept();
    if (request.value("reject").toBool())
        dialog->reject();
    if (request.contains("save"))
        dialog->preview_mask().save(request.value("save").toString());
    if (request.contains("resultMask"))
        dialog->result_images().mask.save(request.value("resultMask").toString());
    QJsonObject controls;
    for (auto* number : dialog->findChildren<QDoubleSpinBox*>())
        controls.insert(number->objectName(), number->value());
    for (auto* combo : dialog->findChildren<QComboBox*>())
        controls.insert(combo->objectName(), combo->currentIndex());
    for (auto* check : dialog->findChildren<QCheckBox*>())
        controls.insert(check->objectName(), check->isChecked());
    auto* output = dialog->findChild<QComboBox*>("refineOutput");
    QJsonArray enabled;
    for (int i = 0; i < output->count(); ++i)
        enabled.append(qobject_cast<QStandardItemModel*>(output->model())->item(i)->isEnabled());
    return {{"ready", dialog->property("refineReady").toBool()},
            {"failed", dialog->property("refineError").toBool()},
            {"status", dialog->findChild<QLabel*>("refineStatus")->text()},
            {"visible", dialog->isVisible()},
            {"result", dialog->result()},
            {"controls", controls},
            {"outputEnabled", enabled},
            {"zoom", view->transform().m11()},
            {"history", dialog->property("refineHistory").toInt()}};
}
