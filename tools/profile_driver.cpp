#include "huelith/editing/project.h"
#include "huelith/editing/raw_profile.h"
#include "huelith/editing/raw_sidecar.h"
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
QJsonObject raw_profile_probe(const QJsonObject& request) {
    using namespace huelith::editing;
    if (request.value("list").toBool())
        return {{"profiles", builtin_raw_profiles()}};
    const auto pixels = request.value("pixels").toArray();
    if (pixels.isEmpty() || pixels.size() > 400000 || pixels.size() % 4)
        throw EditError("Invalid profile pixels");
    QImage source(int(pixels.size() / 4), 1, QImage::Format_RGBA32FPx4_Premultiplied);
    source.setColorSpace(working_space());
    auto* p = reinterpret_cast<float*>(source.bits());
    for (int i = 0; i < pixels.size(); ++i)
        p[i] = float(pixels[i].toDouble());
    const std::atomic_bool cancel{request.value("cancelled").toBool()};
    const auto result = apply_raw_profile(source, request.value("recipe").toObject(), &cancel);
    QJsonArray values;
    const auto* output = reinterpret_cast<const float*>(result.constBits());
    for (int i = 0; i < pixels.size(); ++i)
        values.append(output[i]);
    return {{"pixels", values},
            {"automatic", tone_json(automatic_raw_tone(
                              source, tone_from_json(request.value("tone").toObject())))}};
}
QJsonObject raw_profile_ui(QDialog* dialog, const QJsonObject& request) {
    auto* list = dialog->findChild<QListWidget*>("rawProfileList");
    if (request.contains("id"))
        for (int i = 0; i < list->count(); ++i) {
            auto* item = list->item(i);
            if (item->data(Qt::UserRole).toJsonObject().value("id") != request.value("id"))
                continue;
            list->scrollToItem(item);
            QApplication::processEvents();
            const QPointF p = list->visualItemRect(item).center();
            for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                QMouseEvent e(type, p, list->viewport()->mapToGlobal(p.toPoint()), Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                              Qt::NoModifier);
                QApplication::sendEvent(list->viewport(), &e);
            }
            break;
        }
    if (request.contains("import")) {
        const auto path = request.value("import").toString();
        QTimer::singleShot(30, [path] {
            if (auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                file->selectFile(path);
                QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
            }
        });
        dialog->findChild<QPushButton*>("rawProfileImport")->click();
    }
    QJsonArray items;
    for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i);
        items.append(QJsonObject{{"id", item->data(Qt::UserRole).toJsonObject().value("id")},
                                 {"text", item->text()},
                                 {"thumbnail", !item->icon().isNull()}});
    }
    return {{"items", items},
            {"busy", dialog->property("autoToneBusy").toBool()},
            {"favorites",
             QJsonArray::fromStringList(QSettings().value("raw/profileFavorites").toStringList())}};
}
QJsonObject raw_sidecar_probe(const QJsonObject& request) {
    using namespace huelith::editing;
    std::vector<Layer> layers;
    for (const auto& value : request.value("sources").toArray()) {
        const auto data = value.toObject();
        Layer layer;
        layer.linked_path = data.value("path").toString();
        QFile file(layer.linked_path);
        if (!file.open(QIODevice::ReadOnly))
            throw EditError("Source file unavailable");
        layer.raw_source =
            data.contains("bytes") ? data.value("bytes").toString().toUtf8() : file.readAll();
        layer.raw_recipe = data.value("recipe").toObject();
        layers.push_back(std::move(layer));
    }
    const std::atomic_bool cancelled{request.value("cancelled").toBool()};
    if (request.value("save").toBool())
        save_raw_sidecars(layers, &cancelled);
    QJsonArray recipes;
    for (const auto& layer : layers)
        recipes.append(load_raw_sidecar(layer));
    return {{"recipes", recipes}};
}
