// Local Python automation adapter. Assertions and test scenarios live in Python;
// this DLL calls the same C++ targets linked into Huelith, without a test EXE.
#include "canvas_view.h"
#include "huelith/editing/project.h"
#include "main_window.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QWheelEvent>
#include <memory>
#include <optional>

namespace {
using namespace huelith::editing;
struct Driver {
    int argc{1};
    char name[24]{"HuelithPythonValidation"};
    char* argv[2]{name, nullptr};
    std::unique_ptr<QApplication> application;
    std::unique_ptr<Project> project;
    std::optional<Snapshot> snapshot;
    std::unique_ptr<huelith::desktop::MainWindow> window;
};
Driver& driver() {
    static Driver value;
    return value;
}
QUuid uuid(const QJsonObject& o, const char* key) {
    return QUuid(o.value(QLatin1String(key)).toString());
}
Layer& target(State& s, const QJsonObject& o) {
    auto* layer = find_layer(s.layers, uuid(o, "id"));
    if (!layer)
        throw EditError("missing layer");
    return *layer;
}
QJsonObject run(const QJsonObject& o) {
    auto& d = driver();
    const auto op = o.value("op").toString();
    if (op == "init") {
        if (!d.application) {
            d.application = std::make_unique<QApplication>(d.argc, d.argv);
            QCoreApplication::setOrganizationName("HuelithValidation");
            QCoreApplication::setApplicationName("PythonM1");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                               o.value("settings").toString());
            huelith::desktop::apply_dark_theme();
        }
        return {{"ok", true}, {"protocol", 2}};
    }
    if (op == "create") {
        State state;
        state.name = o.value("name").toString("document");
        state.size = {o.value("width").toInt(16), o.value("height").toInt(16)};
        d.project = std::make_unique<Project>(state);
        return {{"ok", true}};
    }
    if (op == "window") {
        d.window = std::make_unique<huelith::desktop::MainWindow>();
        d.window->show();
        QApplication::processEvents();
        return {{"hwnd", QString::number(d.window->winId())}};
    }
    if (op == "window_open") {
        d.window->open_document(o.value("path").toString(), o.value("place").toBool());
        QApplication::processEvents();
        return {{"count", d.window->document_count()},
                {"layers", int(d.window->active_canvas()->project().state().layers.size())}};
    }
    if (op == "window_new") {
        huelith::CanvasSpec spec;
        spec.width = o.value("width").toInt(400);
        spec.height = o.value("height").toInt(300);
        spec.name = "Python GUI";
        const bool success = d.window->create_document(spec);
        QApplication::processEvents();
        return {{"ok", success}, {"count", d.window->document_count()}};
    }
    if (op == "pump") {
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_filename") {
        d.window->active_canvas()->filename = o.value("path").toString();
        return {{"ok", true}};
    }
    if (op == "window_tab") {
        d.window->findChild<QTabWidget*>("documents")->setCurrentIndex(o.value("index").toInt());
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_slider") {
        auto* slider = d.window->findChild<QSlider*>("opacitySlider");
        slider->setSliderDown(true);
        for (const auto value : {90, 70, 50}) {
            slider->setValue(value);
            QMetaObject::invokeMethod(slider, "sliderMoved", Qt::DirectConnection,
                                      Q_ARG(int, value));
        }
        slider->setSliderDown(false);
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_action") {
        if (o.contains("answer")) {
            QTimer::singleShot(0, [o] {
                auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                if (dialog) {
                    dialog->setTextValue(o.value("answer").toString());
                    dialog->accept();
                } else if (auto* box =
                               qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    box->done(QMessageBox::Yes);
                else if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    for (auto* widget : modal->findChildren<QWidget*>())
                        if (auto* nested = dynamic_cast<huelith::desktop::MainWindow*>(widget)) {
                            for (auto* action : nested->findChildren<QAction*>())
                                if (action->text() == QStringLiteral("像素")) {
                                    action->trigger();
                                    break;
                                }
                            break;
                        }
                    if (o.value("answer").toString() == "cancel")
                        modal->reject();
                    else
                        modal->accept();
                }
            });
        }
        for (auto* action : d.window->findChildren<QAction*>()) {
            if (action->text() == o.value("text").toString()) {
                action->trigger();
                QApplication::processEvents();
                return {{"ok", true}};
            }
        }
        throw EditError("action missing");
    }
    if (op == "window_select") {
        auto* tree = d.window->findChild<QTreeWidget*>("layerTree");
        auto* item = tree->topLevelItem(o.value("index").toInt());
        if (!item)
            throw EditError("row missing");
        const auto rect = tree->visualItemRect(item);
        const QPointF point(rect.left() + 110, rect.center().y());
        const auto modifiers = o.value("control").toBool() ? Qt::ControlModifier : Qt::NoModifier;
        QMouseEvent press(QEvent::MouseButtonPress, point,
                          tree->viewport()->mapToGlobal(point.toPoint()), Qt::LeftButton,
                          Qt::LeftButton, modifiers);
        QApplication::sendEvent(tree->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point,
                            tree->viewport()->mapToGlobal(point.toPoint()), Qt::LeftButton,
                            Qt::NoButton, modifiers);
        QApplication::sendEvent(tree->viewport(), &release);
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_state") {
        auto* c = d.window->active_canvas();
        QJsonObject result{
            {"count", d.window->document_count()},
            {"treeSelected",
             d.window->findChild<QTreeWidget*>("layerTree")->selectedItems().size()},
            {"treeRows", d.window->findChild<QTreeWidget*>("layerTree")->topLevelItemCount()}};
        if (c) {
            result.insert("layers", int(c->project().state().layers.size()));
            result.insert("dirty", c->project().dirty());
            result.insert("revision", qint64(c->project().revision()));
            result.insert("selected", !c->project().state().selection.isNull());
            const auto* layer = find_layer(c->project().state().layers, c->active_layer);
            result.insert("children", layer ? int(layer->children.size()) : -1);
        }
        return result;
    }
    if (op == "window_pixel") {
        const auto image = display_image(render(d.window->active_canvas()->project().state()));
        const auto pixel = image.pixelColor(o.value("x").toInt(), o.value("y").toInt());
        return {{"alpha", pixel.alpha()}};
    }
    if (op == "window_draw") {
        auto* c = d.window->active_canvas();
        c->set_tool(static_cast<huelith::desktop::CanvasTool>(o.value("tool").toInt(2)));
        const QPointF a = c->view_transform().map(QPointF(20, 20)),
                      b = c->view_transform().map(QPointF(120, 100));
        QMouseEvent press(QEvent::MouseButtonPress, a, c->mapToGlobal(a.toPoint()), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(c, &press);
        QMouseEvent move(QEvent::MouseMove, b, c->mapToGlobal(b.toPoint()), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(c, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, b, c->mapToGlobal(b.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(c, &release);
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_clipboard") {
        QApplication::clipboard()->setImage(QImage(o.value("path").toString()));
        return {{"ok", true}};
    }
    if (op == "window_key") {
        auto* c = d.window->active_canvas();
        c->setFocus();
        const auto keys = QKeySequence(o.value("key").toString());
        const auto combination = keys[0];
        QKeyEvent press(o.value("release").toBool() ? QEvent::KeyRelease : QEvent::KeyPress,
                        combination.key(), combination.keyboardModifiers());
        QApplication::sendEvent(c, &press);
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_gesture") {
        auto* c = d.window->active_canvas();
        QPointF a, b;
        if (o.contains("handle")) {
            const auto points = c->transform_handles();
            a = points[std::size_t(o.value("handle").toInt(4))];
            const double scale = o.value("scale").toDouble(.5);
            b = points[0] + QPointF((a.x() - points[0].x()) * o.value("scaleX").toDouble(scale),
                                    (a.y() - points[0].y()) * o.value("scaleY").toDouble(scale));
            if (o.contains("rotate")) {
                const auto center = (points[0] + points[4]) / 2.;
                const auto radial = a - center;
                a += radial / QLineF(QPointF(), radial).length() * 14.;
                QTransform rotation;
                rotation.rotate(o.value("rotate").toDouble());
                b = center + rotation.map(a - center);
            }

        } else {
            a = c->view_transform().map(
                QPointF(o.value("x").toDouble(20), o.value("y").toDouble(20)));
            b = c->view_transform().map(
                QPointF(o.value("endX").toDouble(120), o.value("endY").toDouble(100)));
        }
        const auto modifiers = Qt::KeyboardModifiers(o.value("modifiers").toInt());
        QMouseEvent press(QEvent::MouseButtonPress, a, c->mapToGlobal(a.toPoint()), Qt::LeftButton,
                          Qt::LeftButton, modifiers);
        QApplication::sendEvent(c, &press);
        QMouseEvent move(QEvent::MouseMove, b, c->mapToGlobal(b.toPoint()), Qt::NoButton,
                         Qt::LeftButton, modifiers);
        QApplication::sendEvent(c, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, b, c->mapToGlobal(b.toPoint()),
                            Qt::LeftButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(c, &release);
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_view") {
        auto* c = d.window->active_canvas();
        c->rotation = o.value("rotation").toDouble();
        c->set_zoom(o.value("zoom").toDouble(1));
        return {{"ok", true}};
    }
    if (op == "window_wheel") {
        auto* c = d.window->active_canvas();
        const QPointF point(c->width() / 2., c->height() / 2.);
        QWheelEvent event(point, c->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::KeyboardModifiers(o.value("modifiers").toInt()),
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(c, &event);
        return {{"ok", true}};
    }
    if (op == "window_interaction_state") {
        auto* c = d.window->active_canvas();
        const auto* layer = find_layer(c->project().state().layers, c->active_layer);
        const auto matrix = c->current_layer_transform();
        QJsonArray handles;
        for (const auto& point : c->transform_handles())
            handles.append(QJsonArray{point.x(), point.y()});
        QJsonArray options, actions;
        for (auto* action : d.window->findChild<QToolBar*>("viewToolbar")->actions())
            if (action->isVisible())
                options.append(action->objectName());
        for (auto* action : d.window->findChildren<QAction*>())
            if (!action->shortcut().isEmpty())
                actions.append(QJsonObject{{"text", action->text()},
                                           {"shortcut", action->shortcut().toString()}});
        int selected = 0;
        for (auto* action : d.window->findChild<QToolBar*>("editingTools")->actions())
            if (action->isChecked())
                ++selected;
        int rangeCount = 0;
        const auto& range = c->project().state().selection;
        if (!range.isNull())
            for (int y = 0; y < range.height(); ++y)
                for (int x = 0; x < range.width(); ++x)
                    if (range.constScanLine(y)[x])
                        ++rangeCount;
        const auto origin = c->view_transform().map(QPointF());
        return {{"tool", int(c->tool())},
                {"pending", c->transforming()},
                {"sx", matrix.m11()},
                {"sy", matrix.m22()},
                {"tx", matrix.dx()},
                {"ty", matrix.dy()},
                {"width", layer ? layer->pixels.width() : 0},
                {"height", layer ? layer->pixels.height() : 0},
                {"revision", qint64(c->project().revision())},
                {"handles", handles},
                {"options", options},
                {"shortcuts", actions},
                {"checkedTools", selected},
                {"rangeCount", rangeCount},
                {"radius", c->brush_radius},
                {"zoom", c->zoom()},
                {"foreground", c->foreground.name()},
                {"m12", matrix.m12()},
                {"viewportWidth", c->width()},
                {"viewportHeight", c->height()},
                {"viewX", origin.x()},
                {"viewY", origin.y()}};
    }
    if (op == "window_capture") {
        QApplication::processEvents();
        return {{"ok", d.window->grab().save(o.value("path").toString())}};
    }
    if (op == "shutdown") {
        d.window.reset();
        d.project.reset();
        d.snapshot.reset();
        QApplication::clipboard()->clear();
        QApplication::processEvents();
        d.application.reset();
        return {{"ok", true}};
    }
    if (!d.project)
        throw EditError("create project first");
    auto& p = *d.project;
    if (op == "add") {
        Layer layer;
        layer.name = o.value("name").toString("layer");
        layer.kind = static_cast<LayerKind>(o.value("kind").toInt());
        if (o.contains("color")) {
            QImage image(p.state().size, QImage::Format_RGBA8888);
            image.fill(QColor(o.value("color").toString()));
            layer.pixels = to_working(image);
        }
        if (o.contains("path")) {
            layer.pixels = import_image(o.value("path").toString());
            layer.linked_path = o.value("path").toString();
        }
        layer.fill = QColor(o.value("fill").toString("white"));
        const auto id = layer.id.toString();
        p.edit("add", [&](State& s) {
            const auto parent = uuid(o, "parent");
            if (parent.isNull())
                s.layers.push_back(layer);
            else {
                auto* group = find_layer(s.layers, parent);
                if (!group)
                    throw EditError("parent missing");
                group->children.push_back(layer);
            }
        });
        return {{"id", id}};
    }
    if (op == "set")
        p.edit("properties", [&](State& s) {
            auto& l = target(s, o);
            if (o.contains("visible"))
                l.visible = o.value("visible").toBool();
            if (o.contains("clipped"))
                l.clipped = o.value("clipped").toBool();
            if (o.contains("isolated"))
                l.isolated = o.value("isolated").toBool();
            if (o.contains("blend"))
                l.blend = static_cast<Blend>(o.value("blend").toInt());
            if (o.contains("opacity"))
                l.opacity = float(o.value("opacity").toDouble());
            if (o.contains("exposure"))
                l.exposure = o.value("exposure").toDouble();
            if (o.contains("locked"))
                l.locked = o.value("locked").toBool();
            if (o.contains("name"))
                l.name = o.value("name").toString();
        });
    else if (op == "move")
        p.edit("move", [&](State& s) {
            move_layer(s, uuid(o, "id"), uuid(o, "parent"), o.value("position").toInt(0));
        });
    else if (op == "remove")
        p.edit("remove", [&](State& s) { remove_layer(s, uuid(o, "id")); });
    else if (op == "range")
        p.edit("selection", [&](State& s) {
            QPainterPath path;
            path.addRect(o.value("x").toDouble(), o.value("y").toDouble(), o.value("w").toDouble(8),
                         o.value("h").toDouble(8));
            combine_range(s, path_range(s.size, path),
                          static_cast<RangeOp>(o.value("mode").toInt()));
        });
    else if (op == "transform")
        p.edit("transform", [&](State& s) {
            auto& layer = target(s, o);
            QTransform transform;
            transform.translate(o.value("x").toDouble(), o.value("y").toDouble());
            transform.scale(o.value("scale").toDouble(1), o.value("scale").toDouble(1));
            if (o.value("mask").toBool())
                layer.mask_transform = layer.mask_transform * transform;
            else {
                layer.transform = layer.transform * transform;
                if (layer.mask_linked)
                    layer.mask_transform = layer.mask_transform * transform;
            }
        });
    else if (op == "persist_ranges")
        p.edit("ranges", [](State& s) {
            QPainterPath path;
            path.moveTo(0, 0);
            path.cubicTo(2, 8, 8, 2, 12, 12);
            path.closeSubpath();
            s.paths.insert("curve", path);
            s.saved_selections.insert("range", path_range(s.size, path));
            s.snapshots.insert("snapshot", render(s));
            s.horizontal_guides.push_back(5);
        });
    else if (op == "deselect")
        p.edit("deselect", [](State& s) { s.selection = {}; });
    else if (op == "mask")
        p.edit("mask", [&](State& s) {
            auto& l = target(s, o);
            if (s.selection.isNull()) {
                l.mask = QImage(s.size, QImage::Format_Grayscale8);
                l.mask.fill(255);
            } else
                l.mask = s.selection;
        });
    else if (op == "feather")
        p.edit("feather", [&](State& s) { modify_range(s, o.value("radius").toInt(2), 0); });
    else if (op == "undo")
        return {{"ok", p.undo()}};
    else if (op == "redo")
        return {{"ok", p.redo()}};
    else if (op == "snapshot") {
        d.snapshot = p.snapshot();
        return {{"ok", true}};
    } else if (op == "save") {
        const auto snap = o.value("snapshot").toBool() && d.snapshot ? *d.snapshot : p.snapshot();
        save_project(snap, o.value("path").toString(), o.value("fail").toBool());
        p.mark_saved(snap.revision);
    } else if (op == "load") {
        d.project = std::make_unique<Project>(load_project(o.value("path").toString()));
        d.project->mark_saved(d.project->revision());
        return {{"ok", true}};
    } else if (op == "export")
        export_image(p.state(), o.value("path").toString());
    else if (op == "pixel") {
        const auto image = render(p.state());
        const int x = o.value("x").toInt(), y = o.value("y").toInt();
        if (!image.rect().contains(x, y))
            throw EditError("pixel out of bounds");
        const auto* pixel = reinterpret_cast<const float*>(image.constScanLine(y)) + 4 * x;
        const auto displayed = display_image(image).pixelColor(x, y);
        return {{"linear", QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]}},
                {"rgba", QJsonArray{displayed.red(), displayed.green(), displayed.blue(),
                                    displayed.alpha()}}};
    } else if (op == "state") {
        QJsonArray layers;
        for (const auto& l : p.state().layers)
            layers.append(QJsonObject{{"id", l.id.toString()},
                                      {"name", l.name},
                                      {"children", int(l.children.size())},
                                      {"masked", !l.mask.isNull()}});
        return {{"dirty", p.dirty()},
                {"revision", qint64(p.revision())},
                {"canUndo", p.can_undo()},
                {"canRedo", p.can_redo()},
                {"layers", layers},
                {"paths", p.state().paths.size()},
                {"ranges", p.state().saved_selections.size()},
                {"snapshots", p.state().snapshots.size()},
                {"selection", !p.state().selection.isNull()},
                {"selectionPixel",
                 p.state().selection.isNull() ? -1 : int(p.state().selection.constScanLine(0)[0])}};
    }
    return {{"ok", true}};
}
} // namespace
#ifdef _WIN32
#define HUELITH_EXPORT __declspec(dllexport)
#else
#define HUELITH_EXPORT
#endif
extern "C" HUELITH_EXPORT const char* huelith_python_command(const char* json) noexcept {
    static thread_local QByteArray result;
    try {
        QJsonParseError error;
        const auto input = QJsonDocument::fromJson(json, &error);
        if (error.error != QJsonParseError::NoError || !input.isObject())
            throw huelith::editing::EditError("invalid JSON command");
        result = QJsonDocument(run(input.object())).toJson(QJsonDocument::Compact);
    } catch (const std::exception& error) {
        result = QJsonDocument(QJsonObject{{"error", QString::fromUtf8(error.what())}})
                     .toJson(QJsonDocument::Compact);
    }
    return result.constData();
}
