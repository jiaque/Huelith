// Local Python automation adapter. Assertions and test scenarios live in Python;
// this DLL calls the same C++ targets linked into Huelith, without a test EXE.
#include "canvas_view.h"
#include "huelith/editing/project.h"
#include "main_window.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
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
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
    if (op == "window_scene") {
        State state;
        state.size = {o.value("width").toInt(400), o.value("height").toInt(300)};
        state.name = "M1 Interaction";
        std::function<Layer(QJsonObject)> make = [&](QJsonObject item) {
            Layer layer;
            layer.name = item.value("name").toString("layer");
            layer.kind = static_cast<LayerKind>(item.value("kind").toInt());
            layer.visible = item.value("visible").toBool(true);
            layer.locked = item.value("locked").toBool();
            layer.clipped = item.value("clipped").toBool();
            layer.opacity = float(item.value("opacity").toDouble(1));
            if (item.contains("path"))
                layer.pixels = import_image(item.value("path").toString());
            else if (item.contains("color")) {
                QImage image(item.value("width").toInt(state.size.width()),
                             item.value("height").toInt(state.size.height()),
                             QImage::Format_RGBA8888);
                image.fill(QColor(item.value("color").toString()));
                layer.pixels = to_working(image);
            }
            layer.transform.translate(item.value("x").toDouble(), item.value("y").toDouble());
            layer.transform.scale(item.value("scale").toDouble(1), item.value("scale").toDouble(1));
            if (item.contains("mask")) {
                layer.mask = QImage(state.size, QImage::Format_Grayscale8);
                layer.mask.fill(item.value("mask").toInt());
            }
            layer.mask_linked = item.value("linked").toBool(true);
            for (const auto& child : item.value("children").toArray())
                layer.children.push_back(make(child.toObject()));
            return layer;
        };
        for (const auto& item : o.value("layers").toArray())
            state.layers.push_back(make(item.toObject()));
        for (const auto& value : o.value("horizontal").toArray())
            state.horizontal_guides.push_back(value.toDouble());
        for (const auto& value : o.value("vertical").toArray())
            state.vertical_guides.push_back(value.toDouble());
        if (!d.window->active_canvas()) {
            huelith::CanvasSpec spec;
            spec.width = state.size.width();
            spec.height = state.size.height();
            if (!d.window->create_document(spec))
                throw EditError("create failed");
        }
        d.window->active_canvas()->cancel_transform();
        d.window->active_canvas()->selected_layers.clear();
        d.window->active_canvas()->load_state(state, true);
        d.window->active_canvas()->warp_mode = false;
        d.window->active_canvas()->set_tool(huelith::desktop::CanvasTool::move);
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_control") {
        auto* widget = d.window->findChild<QWidget*>(o.value("name").toString());
        if (!widget)
            throw EditError("control missing");
        if (auto* box = qobject_cast<QCheckBox*>(widget))
            box->setChecked(o.value("value").toBool());
        else if (auto* combo = qobject_cast<QComboBox*>(widget))
            combo->setCurrentIndex(o.value("value").toInt());
        else if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
            spin->setValue(o.value("value").toDouble());
            QMetaObject::invokeMethod(spin, "editingFinished");
        } else if (auto* list = qobject_cast<QListWidget*>(widget)) {
            auto* item = list->item(o.value("value").toInt());
            if (!item)
                throw EditError("list item missing");
            QMetaObject::invokeMethod(list, "itemClicked", Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem*, item));
        } else
            throw EditError("unsupported control");
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_mouse") {
        auto* c = d.window->active_canvas();
        QPointF point(o.value("x").toDouble(), o.value("y").toDouble());
        if (o.value("space").toString() != "widget")
            point = c->view_transform().map(point);
        if (o.contains("handle"))
            point = c->transform_handles()[std::size_t(o.value("handle").toInt())];
        if (o.contains("warp"))
            point = c->warp_handles()[std::size_t(o.value("warp").toInt())];
        point += QPointF(o.value("dx").toDouble(), o.value("dy").toDouble());
        const auto phase = o.value("phase").toString();
        const auto type = phase == "press"     ? QEvent::MouseButtonPress
                          : phase == "release" ? QEvent::MouseButtonRelease
                          : phase == "double"  ? QEvent::MouseButtonDblClick
                                               : QEvent::MouseMove;
        QMouseEvent event(type, point, c->mapToGlobal(point.toPoint()),
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::KeyboardModifiers(o.value("modifiers").toInt()));
        QApplication::sendEvent(c, &event);
        QApplication::processEvents();
        return {{"x", point.x()}, {"y", point.y()}};
    }
    if (op == "window_tablet") {
        auto* c = d.window->active_canvas();
        const auto point =
            c->view_transform().map(QPointF(o.value("x").toDouble(), o.value("y").toDouble()));
        const auto phase = o.value("phase").toString();
        const auto type = phase == "press"     ? QEvent::TabletPress
                          : phase == "release" ? QEvent::TabletRelease
                                               : QEvent::TabletMove;
        QTabletEvent event(type, QPointingDevice::primaryPointingDevice(), point,
                           c->mapToGlobal(point.toPoint()), o.value("pressure").toDouble(1), 0, 0,
                           0, 0, 0, Qt::NoModifier, Qt::LeftButton,
                           type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
        QApplication::sendEvent(c, &event);
        QApplication::processEvents();
        return {{"ok", true}};
    }
    if (op == "window_recovery_file")
        return {{"path", d.window->active_canvas()->recovery_file}};
    if (op == "window_layout") {
        QJsonObject result;
        for (auto* dock : d.window->findChildren<QDockWidget*>()) {
            if (o.value("hide").toBool())
                dock->hide();
            result.insert(dock->objectName(), dock->isVisible());
        }
        return result;
    }
    if (op == "clipboard_probe") {
        return {{"width", QApplication::clipboard()->image().width()},
                {"formats",
                 QJsonArray::fromStringList(QApplication::clipboard()->mimeData()->formats())}};
    }
    if (op == "window_probe") {
        auto* c = d.window->active_canvas();
        const auto& state = c->display_state();
        QJsonArray layers, guides_h, guides_v;
        std::function<void(const std::vector<Layer>&)> visit = [&](const auto& values) {
            for (const auto& layer : values) {
                const auto matrix = layer.transform * parent_matrix(state, layer.id);
                layers.append(QJsonObject{{"name", layer.name},
                                          {"parent",
                                           [&]() {
                                               const auto p = parent_matrix(state, layer.id);
                                               return p.dx();
                                           }()},
                                          {"id", layer.id.toString()},
                                          {"tx", matrix.dx()},
                                          {"ty", matrix.dy()},
                                          {"sx", matrix.m11()},
                                          {"sy", matrix.m22()},
                                          {"m13", matrix.m13()},
                                          {"m23", matrix.m23()},
                                          {"maskTx", layer.mask_transform.dx()},
                                          {"maskTy", layer.mask_transform.dy()},
                                          {"maskSx", layer.mask_transform.m11()},
                                          {"masked", !layer.mask.isNull()},
                                          {"maskEnabled", layer.mask_enabled},
                                          {"visible", layer.visible},
                                          {"warped", layer.warped},
                                          {"warpCenterY", layer.warp_grid[4].y()}});
                visit(layer.children);
            }
        };
        visit(state.layers);
        for (double value : state.horizontal_guides)
            guides_h.append(value);
        for (double value : state.vertical_guides)
            guides_v.append(value);
        QJsonArray elements;
        for (int i = 0; i < c->path.elementCount(); ++i) {
            const auto e = c->path.elementAt(i);
            elements.append(QJsonArray{e.x, e.y, int(e.type)});
        }
        QJsonObject result{
            {"pathPoints", elements},
            {"guidesVisible", c->show_guides},
            {"guidesLocked", c->lock_guides},
            {"grid", c->show_grid},
            {"rulers", c->show_rulers},
            {"snap", c->snap},
            {"layers", layers},
            {"active", c->active_layer.toString()},
            {"selected", int(c->selected_layers.size())},
            {"pending", c->transforming()},
            {"phase", c->ants_phase()},
            {"horizontal", guides_h},
            {"vertical", guides_v},
            {"pathElements", c->path.elementCount()},
            {"pathClosed",
             !c->path.isEmpty() && c->path.currentPosition() ==
                                       QPointF(c->path.elementAt(0).x, c->path.elementAt(0).y)},
            {"history", int(c->project().history_position())},
            {"maskTarget", c->mask_target},
            {"maskView", c->mask_view}};
        if (o.contains("x")) {
            const int x = o.value("x").toInt(), y = o.value("y").toInt();
            const auto image = display_image(render(state));
            if (image.rect().contains(x, y)) {
                const auto pixel = image.pixelColor(x, y);
                result.insert("rgba",
                              QJsonArray{pixel.red(), pixel.green(), pixel.blue(), pixel.alpha()});
                result.insert("range", state.selection.isNull()
                                           ? -1
                                           : int(state.selection.constScanLine(y)[x]));
            }
        }
        return result;
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
        d.window->setProperty("conflictDetected", false);
        if (o.contains("dialog")) {
            auto* timer = new QTimer(d.window.get());
            timer->setInterval(20);
            auto phase = std::make_shared<int>(0);
            auto ticks = std::make_shared<int>(0);
            const auto settings = o.value("dialog").toObject();
            QObject::connect(
                timer, &QTimer::timeout, d.window.get(),
                [timer, phase, settings, ticks, window = d.window.get()] {
                    if (++*ticks > 500) {
                        if (auto* active =
                                qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                            active->reject();
                        timer->stop();
                        timer->deleteLater();
                        return;
                    }
                    auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    if (!modal)
                        return;
                    if (auto* file = qobject_cast<QFileDialog*>(modal)) {
                        file->selectFile(settings.value("path").toString());
                        static_cast<QDialog*>(file)->accept();
                        timer->stop();
                        timer->deleteLater();
                        return;
                    }
                    if (*phase == 0) {
                        for (auto it = settings.begin(); it != settings.end(); ++it) {
                            if (auto* spin = modal->findChild<QSpinBox*>(it.key()))
                                spin->setValue(it.value().toInt());
                            else if (auto* combo = modal->findChild<QComboBox*>(it.key()))
                                combo->setCurrentIndex(it.value().toInt());
                            else if (auto* check = modal->findChild<QCheckBox*>(it.key()))
                                check->setChecked(it.value().toBool());
                        }
                        if (settings.contains("capture"))
                            modal->grab().save(settings.value("capture").toString());
                        if (settings.contains("conflict")) {
                            auto* table = modal->findChild<QTableWidget*>("shortcutTable");
                            if (table && table->rowCount() > 1) {
                                static_cast<QKeySequenceEdit*>(table->cellWidget(0, 1))
                                    ->setKeySequence(QKeySequence("Ctrl+Q"));
                                static_cast<QKeySequenceEdit*>(table->cellWidget(1, 1))
                                    ->setKeySequence(QKeySequence("Ctrl+Q"));
                                auto* box = modal->findChild<QDialogButtonBox*>();
                                box->button(QDialogButtonBox::Ok)->click();
                                window->setProperty(
                                    "conflictDetected",
                                    !modal->findChild<QLabel*>("shortcutError")->text().isEmpty());
                            }
                        }
                        if (settings.contains("binding")) {
                            const auto binding = settings.value("binding").toObject();
                            auto* table = modal->findChild<QTableWidget*>("shortcutTable");
                            if (table)
                                for (int i = 0; i < table->rowCount(); ++i)
                                    if (table->item(i, 0)->text() ==
                                        binding.value("name").toString())
                                        static_cast<QKeySequenceEdit*>(table->cellWidget(i, 1))
                                            ->setKeySequence(
                                                QKeySequence(binding.value("key").toString()));
                        }
                        if (settings.contains("capture"))
                            modal->grab().save(settings.value("capture").toString());
                        ++*phase;
                        if (settings.value("accept").toBool()) {
                            if (auto* box = modal->findChild<QDialogButtonBox*>()) {
                                auto* button = box->button(QDialogButtonBox::Ok);
                                if (!button)
                                    button = box->button(QDialogButtonBox::Save);
                                if (!button)
                                    button = box->button(QDialogButtonBox::Open);
                                if (button)
                                    button->click();
                                else
                                    modal->accept();
                            } else
                                modal->accept();
                        } else
                            modal->reject();
                        if (!settings.contains("path")) {
                            timer->stop();
                            timer->deleteLater();
                        }
                    }
                });
            timer->start();
        }

        if (o.contains("answer")) {
            QTimer::singleShot(0, [o] {
                auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                if (dialog) {
                    dialog->setTextValue(o.value("answer").toString());
                    dialog->accept();
                } else if (auto* box =
                               qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    box->done(o.value("answer").toString() == "cancel"    ? QMessageBox::Cancel
                              : o.value("answer").toString() == "discard" ? QMessageBox::Discard
                              : o.value("answer").toString() == "save"    ? QMessageBox::Save
                                                                          : QMessageBox::Yes);
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
                return {{"ok", true},
                        {"conflictDetected", d.window->property("conflictDetected").toBool()}};
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
        const int column = o.value("column").toInt();
        const QPointF point(column == 2 ? tree->columnViewportPosition(2) + 20
                                        : rect.left() + o.value("offset").toInt(110),
                            rect.center().y());
        const auto modifiers = Qt::KeyboardModifiers(
            o.value("modifiers").toInt(o.value("control").toBool() ? int(Qt::ControlModifier) : 0));
        QMouseEvent press(QEvent::MouseButtonPress, point,
                          tree->viewport()->mapToGlobal(point.toPoint()), Qt::LeftButton,
                          Qt::LeftButton, modifiers);
        QApplication::sendEvent(tree->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point,
                            tree->viewport()->mapToGlobal(point.toPoint()), Qt::LeftButton,
                            Qt::NoButton, modifiers);
        QApplication::sendEvent(tree->viewport(), &release);
        if (o.value("double").toBool()) {
            QMouseEvent click(QEvent::MouseButtonDblClick, point,
                              tree->viewport()->mapToGlobal(point.toPoint()), Qt::LeftButton,
                              Qt::LeftButton, modifiers);
            QApplication::sendEvent(tree->viewport(), &click);
            QApplication::processEvents();
            if (o.contains("rename"))
                if (auto* editor = tree->findChild<QLineEdit*>()) {
                    editor->setText(o.value("rename").toString());
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QApplication::sendEvent(editor, &enter);
                }
        }
        if (o.contains("drop")) {
            const auto* destination = tree->topLevelItem(o.value("drop").toInt());
            const auto where = tree->visualItemRect(destination).center();
            std::unique_ptr<QMimeData> mime(
                tree->model()->mimeData({tree->model()->index(o.value("index").toInt(), 0)}));
            QDragEnterEvent enter(where, Qt::MoveAction, mime.get(), Qt::LeftButton,
                                  Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &enter);
            QDropEvent drop(where, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &drop);
        }
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
        d.window->activateWindow();
        QApplication::processEvents();
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
