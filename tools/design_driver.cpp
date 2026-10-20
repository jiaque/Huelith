// Test adapter only: Python owns scenarios and assertions; production owns all behavior.
#include "canvas_view.h"
#include "huelith/editing/layout_template.h"
#include "huelith/editing/project.h"
#include "main_window.h"
#include "text_edit_view.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

using namespace huelith::editing;
QJsonObject design_command(huelith::desktop::MainWindow* window, const QJsonObject& request) {
    if (request.value("op").toString() == "layout_probe") {
        LayoutTemplate options;
        options.size = QSize(request.value("width").toInt(800), request.value("height").toInt(600));
        options.rows = request.value("rows").toInt(2);
        options.columns = request.value("columns").toInt(2);
        options.margin = request.value("margin").toDouble(30);
        options.gap = request.value("gap").toDouble(20);
        options.caption = request.value("caption").toDouble(80);
        auto state = create_layout_template(options);
        QJsonArray frames;
        for (auto& layer : state.layers)
            if (layer.frame) {
                if (request.contains("image"))
                    place_in_frame(state, layer.id, import_image(request.value("image").toString()),
                                   request.value("image").toString());
                const auto r = *layer.frame;
                frames.append(QJsonArray{r.x(), r.y(), r.width(), r.height()});
            }
        if (request.contains("missingFont"))
            for (auto& layer : state.layers)
                if (layer.kind == LayerKind::text)
                    layer.text.html = QString("<p style='font-family:%1'>中文</p>")
                                          .arg(request.value("missingFont").toString());
        const auto first = render(state);
        if (request.contains("file")) {
            save_project({state, 1}, request.value("file").toString());
            state = load_project(request.value("file").toString());
        }
        const auto image = render(state);
        if (request.contains("capture"))
            display_image(image).save(request.value("capture").toString());
        return {{"frames", frames},
                {"same", first == image},
                {"fonts", QJsonArray::fromStringList(missing_fonts(state))},
                {"layers", int(state.layers.size())}};
    }
    if (request.value("op").toString() == "design_probe") {
        State state;
        state.name = "Design probe";
        state.size = QSize(request.value("width").toInt(160), request.value("height").toInt(120));
        Layer layer;
        layer.name = "editable";
        layer.kind = request.contains("text") ? LayerKind::text : LayerKind::shape;
        if (layer.kind == LayerKind::text)
            layer.text = text_from_json(request.value("text").toObject());
        else
            layer.shape = shape_from_json(request.value("shape").toObject());
        if (request.contains("styles"))
            layer.styles = styles_from_json(request.value("styles").toObject());
        state.global_light_angle = layer.styles.global_angle;
        layer.transform.translate(request.value("x").toDouble(20), request.value("y").toDouble(20));
        layer.transform.scale(request.value("scale").toDouble(1),
                              request.value("scale").toDouble(1));
        state.layers.push_back(layer);
        if (request.contains("file")) {
            save_project({state, 1}, request.value("file").toString());
            state = load_project(request.value("file").toString());
        }
        if (request.contains("previewEdge"))
            state = preview_state(state, request.value("previewEdge").toInt());
        const auto image = render(state);
        if (request.contains("capture"))
            display_image(image).save(request.value("capture").toString());
        const auto bounds = layer_bounds(state, layer.id);
        QJsonArray pixels;
        for (const auto& sample : request.value("samples").toArray()) {
            const auto point = sample.toArray();
            const int x = point[0].toInt(), y = point[1].toInt();
            if (!image.rect().contains(x, y))
                throw EditError("sample outside image");
            const auto* pixel = reinterpret_cast<const float*>(image.constScanLine(y)) + x * 4;
            pixels.append(QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]});
        }
        QJsonArray hits;
        for (const auto& sample : request.value("hits").toArray()) {
            const auto p = sample.toArray();
            hits.append(!hit_layer(state, QPointF(p[0].toDouble(), p[1].toDouble()), 0).isNull());
        }
        return {{"text", text_json(state.layers[0].text)},
                {"shape", shape_json(state.layers[0].shape)},
                {"styles", styles_json(state.layers[0].styles)},
                {"bounds", QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()}},
                {"pixels", pixels},
                {"hits", hits},
                {"hash", QString::fromLatin1(
                             QCryptographicHash::hash(
                                 QByteArrayView(reinterpret_cast<const char*>(image.constBits()),
                                                image.sizeInBytes()),
                                 QCryptographicHash::Sha256)
                                 .toHex())}};
    }
    if (!window || !window->active_canvas())
        throw EditError("design window missing");
    auto* canvas = window->active_canvas();
    if (request.value("op").toString() == "window_style") {
        QTimer timer;
        timer.setInterval(25);
        int stage{}, ticks{};
        bool ready{};
        QObject::connect(&timer, &QTimer::timeout, window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (++ticks > 400) {
                dialog->reject();
                timer.stop();
                return;
            }
            if (dialog->objectName() != "layerStyleDialog") {
                dialog->reject();
                return;
            }
            if (stage == 0) {
                auto* list = dialog->findChild<QListWidget*>("styleEffects");
                const int index = request.value("index").toInt(1);
                list->setCurrentRow(index);
                list->item(index)->setCheckState(Qt::Checked);
                for (auto it = request.begin(); it != request.end(); ++it)
                    if (auto* spin = dialog->findChild<QDoubleSpinBox*>(it.key()))
                        spin->setValue(it.value().toDouble());
                ++stage;
                return;
            }
            if (!dialog->property("styleReady").toBool())
                return;
            ready = true;
            if (request.contains("capture"))
                dialog->grab().save(request.value("capture").toString());
            if (request.value("accept").toBool())
                dialog->accept();
            else
                dialog->reject();
            timer.stop();
        });
        timer.start();
        bool found{};
        for (auto* action : window->findChildren<QAction*>())
            if (action->text() == QStringLiteral("图层样式…")) {
                found = true;
                action->trigger();
                break;
            }
        timer.stop();
        if (!found)
            throw EditError("style command missing");
        const auto* layer = find_layer(canvas->project().state().layers, canvas->active_layer);
        return {{"ready", ready},
                {"ticks", ticks},
                {"styles", styles_json(layer->styles)},
                {"history", int(canvas->project().history_position())}};
    }
    huelith::desktop::TextEditView* editor = nullptr;
    for (auto* view : canvas->findChildren<QGraphicsView*>())
        if (view->isVisible())
            editor = dynamic_cast<huelith::desktop::TextEditView*>(view);
    // findChild cannot distinguish a queued-for-deletion hidden editor, so only interact with a
    // visible one.
    if (editor && !editor->isVisible())
        editor = nullptr;
    if (request.contains("textInput")) {
        if (!editor)
            throw EditError("text editor missing");
        QInputMethodEvent event;
        event.setCommitString(request.value("textInput").toString());
        QApplication::sendEvent(editor, &event);
    }
    if (request.contains("resizeText")) {
        if (!editor)
            throw EditError("text editor missing");
        QGraphicsTextItem* item{};
        for (auto* value : editor->scene()->items())
            if (auto* text = dynamic_cast<QGraphicsTextItem*>(value))
                item = text;
        if (!item)
            throw EditError("text item missing");
        const auto corner = item->boundingRect().bottomRight() - QPointF(3, 3);
        const auto start = editor->mapFromScene(item->mapToScene(corner));
        const auto end =
            editor->mapFromScene(item->mapToScene(corner * request.value("resizeText").toDouble()));
        for (const auto phase :
             {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}) {
            const QPointF p = phase == QEvent::MouseButtonPress ? start : end;
            QMouseEvent event(phase, p, editor->viewport()->mapToGlobal(p.toPoint()),
                              phase == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                              phase == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(editor->viewport(), &event);
        }
    }
    if (request.contains("key")) {
        if (!editor)
            throw EditError("text editor missing");
        const auto key = QKeySequence(request.value("key").toString())[0];
        QKeyEvent event(QEvent::KeyPress, key.key(), key.keyboardModifiers());
        QApplication::sendEvent(editor, &event);
    }
    if (request.contains("control")) {
        auto* widget = window->findChild<QWidget*>(request.value("control").toString());
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
            spin->setValue(request.value("value").toDouble());
            QMetaObject::invokeMethod(spin, "editingFinished");
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            const int index = request.value("value").toInt();
            combo->setCurrentIndex(index);
            QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index));
        } else
            throw EditError("design control missing");
    }
    if (request.contains("action")) {
        auto* action = window->findChild<QAction*>(request.value("action").toString());
        if (!action)
            throw EditError("design action missing");
        action->trigger();
    }
    QApplication::processEvents();
    if (request.contains("file"))
        save_project(canvas->project().snapshot(), request.value("file").toString());
    if (request.contains("capture"))
        window->grab().save(request.value("capture").toString());
    if (request.contains("render"))
        display_image(render(canvas->project().state())).save(request.value("render").toString());
    QJsonArray layers;
    for (const auto& layer : canvas->project().state().layers) {
        QTextDocument doc;
        if (layer.kind == LayerKind::text)
            configure_text_document(doc, layer.text);
        layers.append(QJsonObject{{"kind", int(layer.kind)},
                                  {"text", text_json(layer.text)},
                                  {"frame", layer.frame.has_value()},
                                  {"children", int(layer.children.size())},
                                  {"styles", styles_json(layer.styles)},
                                  {"shape", shape_json(layer.shape)},
                                  {"plain", doc.toPlainText()},
                                  {"id", layer.id.toString()}});
    }
    QString live;
    if (canvas->editing_text())
        for (auto* view : canvas->findChildren<QGraphicsView*>())
            if (view->isVisible() && view->scene())
                for (auto* item : view->scene()->items())
                    if (auto* text = dynamic_cast<QGraphicsTextItem*>(item))
                        live = text->toPlainText();
    return {{"layers", layers},
            {"live", live},
            {"editing", canvas->editing_text()},
            {"revision", qint64(canvas->project().revision())},
            {"history", int(canvas->project().history_position())}};
}
