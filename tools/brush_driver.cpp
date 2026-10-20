// Python supplies test cases and assertions; this adapter invokes production brush code and Qt UI.
#include "canvas_view.h"
#include "huelith/editing/brush.h"
#include "huelith/editing/content_fill.h"
#include "huelith/editing/healing.h"
#include "huelith/editing/liquify.h"
#include "main_window.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QFileDialog>
#include <QTabletEvent>
#include <cstring>

using namespace huelith::editing;
QJsonObject composite_command(const QJsonObject& request);
QJsonObject brush_command(huelith::desktop::MainWindow* window, const QJsonObject& request) {
    if (request.value("composite").toBool() || request.contains("blendTerms"))
        return composite_command(request);
    if (request.value("retouch").toBool() || request.value("gaussian").toBool()) {
        const int w = request.value("width").toInt(), h = request.value("height").toInt();
        const auto bytes = QByteArray::fromBase64(request.value("data").toString().toLatin1());
        const auto coverage = QByteArray::fromBase64(request.value("mask").toString().toLatin1());
        if (w < 1 || h < 1 || qint64(w) * h > 4000000 || bytes.size() != qint64(w) * h * 16 ||
            coverage.size() != qint64(w) * h)
            throw EditError("invalid retouch fixture");
        State state;
        state.name = "retouch";
        state.size = {w, h};
        Layer layer;
        layer.name = "pixels";
        layer.pixels = QImage(w, h, QImage::Format_RGBA32FPx4_Premultiplied);
        layer.pixels.setColorSpace(working_space());
        std::memcpy(layer.pixels.bits(), bytes.constData(), std::size_t(bytes.size()));
        QImage mask(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y)
            std::memcpy(mask.scanLine(y), coverage.constData() + qint64(y) * w, std::size_t(w));
        if (request.contains("selection")) {
            const auto selection =
                QByteArray::fromBase64(request.value("selection").toString().toLatin1());
            if (selection.size() != qint64(w) * h)
                throw EditError("invalid selection fixture");
            state.selection = QImage(w, h, QImage::Format_Grayscale8);
            for (int y = 0; y < h; ++y)
                std::memcpy(state.selection.scanLine(y), selection.constData() + qint64(y) * w,
                            std::size_t(w));
        }
        state.layers.push_back(layer);
        if (request.value("gaussian").toBool()) {
            Effect effect;
            effect.blur_radius = request.value("radius").toDouble(5);
            effect.amount = request.value("amount").toDouble(1);
            effect.enabled = request.value("enabled").toBool(true);
            if (request.value("masked").toBool())
                effect.mask = mask;
            state.layers[0].effects = {effect};
            std::atomic_bool cancelled{request.value("cancelled").toBool()};
            QElapsedTimer timer;
            timer.start();
            const auto output = apply_effects(layer.pixels, {effect}, &cancelled);
            const double elapsed = timer.nsecsElapsed() / 1e6;
            bool roundtrip = true;
            if (request.contains("file")) {
                save_project({state, 1}, request.value("file").toString());
                const auto loaded = load_project(request.value("file").toString());
                roundtrip = render(loaded) == output && loaded.layers[0].pixels == layer.pixels;
            }
            const auto proxy = preview_state(state, std::max(1, w / 2));
            return {{"data", QString::fromLatin1(
                                 QByteArray(reinterpret_cast<const char*>(output.constBits()),
                                            output.sizeInBytes())
                                     .toBase64())},
                    {"roundtrip", roundtrip},
                    {"elapsedMs", elapsed},
                    {"proxyRadius", proxy.layers[0].effects[0].blur_radius}};
        }
        RetouchOptions options;
        options.operation = Retouch(request.value("operation").toInt(5));
        options.tone_range = request.value("range").toInt(1);
        options.protect_tones = request.value("protect").toBool(true);
        options.sponge_saturate = request.value("increase").toBool();
        options.sponge_vibrance = request.value("vibrance").toBool(true);
        options.strength = request.value("strength").toDouble(.5);
        const auto offset=request.value("offset").toArray(),anchor=request.value("anchor").toArray();
        if(offset.size()==2)options.source_offset={offset[0].toDouble(),offset[1].toDouble()};
        if(anchor.size()==2)options.source_anchor={anchor[0].toDouble(),anchor[1].toDouble()};
        options.source_scale_x=request.value("scaleX").toDouble(1);
        options.source_scale_y=request.value("scaleY").toDouble(1);
        options.source_rotation=request.value("rotation").toDouble();
        options.source_flip_x=request.value("flipX").toBool();
        options.source_flip_y=request.value("flipY").toBool();
        retouch(state, layer.id, mask, options);
        const auto& output = state.layers[0].pixels;
        return {{"data",
                 QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(output.constBits()),
                                                output.sizeInBytes())
                                         .toBase64())}};
    }
    if (request.value("liquify").toBool()) {
        const int w = request.value("width").toInt(), h = request.value("height").toInt();
        const auto bytes = QByteArray::fromBase64(request.value("data").toString().toLatin1());
        if (w < 1 || h < 1 || qint64(w) * h > 4000000 || bytes.size() != qint64(w) * h * 16)
            throw EditError("invalid liquify input");
        QImage source(w, h, QImage::Format_RGBA32FPx4_Premultiplied);
        source.setColorSpace(working_space());
        std::memcpy(source.bits(), bytes.constData(), std::size_t(bytes.size()));
        auto mesh = create_liquify_mesh(source.size());
        QElapsedTimer timer;
        timer.start();
        for (const auto value : request.value("dabs").toArray()) {
            const auto dab = value.toObject();
            if (dab.contains("freeze")) {
                freeze_liquify(mesh, dab.value("freeze").toInt());
                continue;
            }
            if (dab.contains("reconstruct")) {
                reconstruct_liquify(mesh, dab.value("reconstruct").toDouble());
                continue;
            }
            LiquifyBrush brush;
            brush.tool = LiquifyTool(dab.value("tool").toInt());
            brush.reverse = dab.value("reverse").toBool();
            brush.diameter = dab.value("size").toDouble(60);
            brush.density = dab.value("density").toDouble(.5);
            brush.pressure = dab.value("pressure").toDouble(1);
            const auto from = dab.value("from").toArray(), to = dab.value("to").toArray();
            liquify_dab(mesh, source.size(), {from[0].toDouble(), from[1].toDouble()},
                        {to[0].toDouble(), to[1].toDouble()}, brush,
                        dab.value("amount").toDouble(1));
        }
        Effect effect;
        effect.liquify = mesh;
        effect.amount = request.value("amount").toDouble(1);
        effect.enabled = request.value("enabled").toBool(true);
        std::atomic_bool cancelled{request.value("cancelled").toBool()};
        auto output = apply_effects(source, {effect}, &cancelled);
        bool roundtrip = liquify_from_json(liquify_json(mesh)) == mesh;
        if (request.contains("file")) {
            State state;
            state.name = "liquify";
            state.size = source.size();
            Layer layer;
            layer.name = "source";
            layer.pixels = source;
            layer.effects = {effect};
            state.layers.push_back(layer);
            save_project({state, 1}, request.value("file").toString());
            const auto loaded = load_project(request.value("file").toString());
            roundtrip = loaded.layers[0].effects[0].liquify == mesh && render(loaded) == output;
        }
        const auto encode = [](const QImage& image) {
            return QString::fromLatin1(
                QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes())
                    .toBase64());
        };
        return {{"data", encode(output)},    {"mesh", encode(mesh)},
                {"meshWidth", mesh.width()}, {"meshHeight", mesh.height()},
                {"roundtrip", roundtrip},    {"elapsedMs", timer.nsecsElapsed() / 1e6}};
    }
    if (request.value("patch").toBool()) {
        const int w = request.value("width").toInt(), h = request.value("height").toInt();
        const auto data = QByteArray::fromBase64(request.value("data").toString().toLatin1());
        const auto coverage = QByteArray::fromBase64(request.value("mask").toString().toLatin1());
        if (w < 1 || h < 1 || qint64(w) * h > 4000000 || data.size() != qint64(w) * h * 16 ||
            coverage.size() != qint64(w) * h)
            throw EditError("Invalid patch fixture");
        QImage input(w, h, QImage::Format_RGBA32FPx4_Premultiplied);
        input.setColorSpace(working_space());
        QImage mask(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y) {
            std::memcpy(input.scanLine(y), data.constData() + qint64(y) * w * 16,
                        std::size_t(w) * 16);
            std::memcpy(mask.scanLine(y), coverage.constData() + qint64(y) * w, std::size_t(w));
        }
        QPointF offset(request.value("dx").toDouble(), request.value("dy").toDouble());
        std::atomic_bool cancelled{request.value("cancelled").toBool()};
        QElapsedTimer timer;
        timer.start();
        if (request.value("automatic").toBool())
            offset = automatic_patch_offset(input, mask, &cancelled);
        QImage output;
        if (request.value("contentFill").toBool()) {
            QImage allowed;
            if (request.contains("allowed")) {
                const auto bytes =
                    QByteArray::fromBase64(request.value("allowed").toString().toLatin1());
                if (bytes.size() != qint64(w) * h)
                    throw EditError("bad allowed fixture");
                allowed = QImage(w, h, QImage::Format_Grayscale8);
                for (int y = 0; y < h; ++y)
                    std::memcpy(allowed.scanLine(y), bytes.constData() + qint64(y) * w,
                                std::size_t(w));
            }
            ContentFillOptions options;
            options.patch_radius = request.value("radius").toInt(4);
            options.candidates = request.value("candidates").toInt(192);
            options.color_adaptation = request.value("adaptation").toDouble(.5);
            options.rotation_adaptation = request.value("rotation").toInt();
            options.scale = request.value("scale").toBool();
            options.mirror = request.value("mirror").toBool();
            options.seed = unsigned(request.value("seed").toInt(173));
            output = content_fill(input, mask, allowed, options, &cancelled);
            if (request.value("apply").toBool()) {
                State state;
                state.name = "fill";
                state.size = input.size();
                Layer layer;
                layer.name = "pixels";
                layer.pixels = input;
                state.layers.push_back(layer);
                paint_replacement(state, layer.id, output, mask);
                output = render(state);
            }
        } else if (request.value("apply").toBool()) {
            State state;
            state.name = "Patch probe";
            state.size = input.size();
            state.selection = mask;
            Layer layer;
            layer.name = "source";
            layer.pixels = input;
            state.layers.push_back(layer);
            QUuid target = layer.id;
            if (request.value("repair").toBool()) {
                Layer repair;
                repair.name = "repair";
                repair.kind = LayerKind::repair;
                state.layers.push_back(repair);
                target = repair.id;
            }
            patch_selection(state, target, offset, request.value("destination").toBool(),
                            SampleLayers::current_below, request.value("adaptation").toDouble(1));
            if (request.contains("file")) {
                save_project({state, 1}, request.value("file").toString());
                state = load_project(request.value("file").toString());
            }
            output = render(state);
        } else
            output = patch_replacement(input, mask, offset, request.value("adaptation").toDouble(1),
                                       &cancelled);
        const double elapsed = timer.nsecsElapsed() / 1e6;
        if (request.contains("capture"))
            display_image(output).save(request.value("capture").toString());
        return {{"data",
                 QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(output.constBits()),
                                                output.sizeInBytes())
                                         .toBase64())},
                {"dx", offset.x()},
                {"dy", offset.y()},
                {"elapsedMs", elapsed}};
    }
    if (request.value("op").toString() == "brush_probe") {
        const auto settings = brush_from_json(request.value("settings").toObject());
        QImage image(request.value("width").toInt(512), request.value("height").toInt(256),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        BrushStroke stroke;
        stroke.reset(settings, std::uint32_t(request.value("seed").toInt(1)),
                     request.value("zoom").toDouble(1));
        QElapsedTimer timer;
        timer.start();
        for (const auto& item : request.value("points").toArray()) {
            const auto point = item.toArray();
            stroke.add(image, {point[0].toDouble(), point[1].toDouble()},
                       point.size() > 2 ? point[2].toDouble() : 1);
        }
        for (const auto value : request.value("idle").toArray())
            stroke.idle(image, value.toDouble());
        if (request.value("finish").toBool())
            stroke.finish(image);
        const double elapsed = timer.nsecsElapsed() / 1e6;
        if (request.contains("capture"))
            image.save(request.value("capture").toString());
        QJsonArray values;
        for (const auto& item : request.value("samples").toArray()) {
            const auto point = item.toArray();
            values.append(qAlpha(image.pixel(point[0].toInt(), point[1].toInt())));
        }
        QJsonArray builtins;
        for (const auto& [name, preset] : builtin_brushes())
            builtins.append(QJsonObject{{"name", name}, {"settings", brush_json(preset)}});
        return {{"settings", brush_json(settings)},
                {"presets", builtins},
                {"stamps", stroke.stamps()},
                {"position", QJsonArray{stroke.position().x(), stroke.position().y()}},
                {"elapsedMs", elapsed},
                {"samples", values},
                {"hash", QString::fromLatin1(
                             QCryptographicHash::hash(
                                 QByteArrayView(reinterpret_cast<const char*>(image.constBits()),
                                                image.sizeInBytes()),
                                 QCryptographicHash::Sha256)
                                 .toHex())}};
    }
    auto* canvas = window->active_canvas();
    if (request.value("previewState").toBool()) {
        if (request.contains("pixels"))
            canvas->rendered_preview().save(request.value("pixels").toString());
        QJsonObject result{
            {"pending", canvas->preview_pending()},
            {"error", canvas->property("previewError").toString()},
            {"proxyGeneration", canvas->property("previewProxyGeneration").toLongLong()},
            {"generation", canvas->property("previewGeneration").toLongLong()}};
        if (request.value("compare").toBool()) {
            const auto expected = display_image(render(canvas->project().state()));
            const auto& actual = canvas->rendered_preview();
            result["equal"] = expected == actual;
            result["width"] = actual.width();
            result["height"] = actual.height();
        }
        return result;
    }
    QJsonObject content_status;
    if (request.value("contentDialog").toBool()) {
        QTimer poll;
        poll.setInterval(20);
        int ticks = 0, stage = 0;
        bool ready = false;
        QString status;
        QJsonObject sampling_info;
        QJsonArray sampling_hashes;
        QObject::connect(&poll, &QTimer::timeout, window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (dialog->objectName() != "contentFillDialog" || ++ticks > 3000) {
                dialog->reject();
                poll.stop();
                return;
            }
            status = dialog->findChild<QLabel*>("fillStatus")->text();
            if (stage == 0) {
                // Native dialogs can enter their event loop before the first exposure paint.
                // Mouse coordinates must come from the laid-out image, never an empty QRectF.
                if (request.contains("samplingPoints") &&
                    dialog->findChild<QWidget*>("fillSamplingView")
                        ->property("imageRect")
                        .toRectF()
                        .isEmpty())
                    return;
                for (auto it = request.begin(); it != request.end(); ++it) {
                    if (auto* combo = dialog->findChild<QComboBox*>(it.key()))
                        combo->setCurrentIndex(it.value().toInt());
                    if (auto* check = dialog->findChild<QCheckBox*>(it.key()))
                        check->setChecked(it.value().toBool());
                    if (auto* spin = dialog->findChild<QSpinBox*>(it.key()))
                        spin->setValue(it.value().toInt());
                }
                if (request.contains("samplingPoints")) {
                    auto* view = dialog->findChild<QWidget*>("fillSamplingView");
                    view->repaint();
                    const auto area = view->property("imageRect").toRectF();
                    sampling_info = {
                        {"subtract", dialog->findChild<QCheckBox*>("fillSubtract")->isChecked()},
                        {"rect", QJsonArray{area.x(), area.y(), area.width(), area.height()}}};
                    sampling_hashes.append(dialog->property("samplingHash").toString());
                    const auto points = request.value("samplingPoints").toArray();
                    for (qsizetype i = 0; i < points.size(); ++i) {
                        const auto point = points[i].toArray();
                        const QPointF p(area.x() + point[0].toDouble() * area.width(),
                                        area.y() + point[1].toDouble() * area.height());
                        QMouseEvent event(i == 0 ? QEvent::MouseButtonPress : QEvent::MouseMove, p,
                                          view->mapToGlobal(p.toPoint()),
                                          i == 0 ? Qt::LeftButton : Qt::NoButton, Qt::LeftButton,
                                          Qt::KeyboardModifiers());
                        QApplication::sendEvent(view, &event);
                        if (i + 1 == points.size()) {
                            QMouseEvent release(QEvent::MouseButtonRelease, p,
                                                view->mapToGlobal(p.toPoint()), Qt::LeftButton,
                                                Qt::NoButton, Qt::KeyboardModifiers());
                            QApplication::sendEvent(view, &release);
                        }
                    }
                    sampling_hashes.append(dialog->property("samplingHash").toString());
                    if (request.value("undoSampling").toBool()) {
                        view->setFocus();
                        QKeyEvent undo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                        QApplication::sendEvent(view, &undo);
                        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Z, Qt::ControlModifier);
                        QApplication::sendEvent(view, &release);
                        sampling_hashes.append(dialog->property("samplingHash").toString());
                    }
                }
                ++stage;
                return;
            }
            if (request.value("cancelEarly").toBool()) {
                dialog->reject();
                poll.stop();
                return;
            }
            if (dialog->property("fillError").toBool() && ticks > 10) {
                if (request.contains("capture"))
                    dialog->grab().save(request.value("capture").toString());
                dialog->reject();
                poll.stop();
                return;
            }
            if (!dialog->property("fillReady").toBool())
                return;
            ready = true;
            if (request.contains("capture"))
                dialog->grab().save(request.value("capture").toString());
            if (stage == 1) {
                ++stage;
                if (request.value("accept").toBool())
                    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                else {
                    dialog->reject();
                    poll.stop();
                }
            }
        });
        poll.start();
        bool found = false;
        for (auto* action : window->findChildren<QAction*>())
            if (action->text() == QStringLiteral("内容识别填充…")) {
                found = true;
                action->trigger();
                break;
            }
        poll.stop();
        if (!found)
            throw EditError("content fill action missing");
        content_status = {{"ready", ready},
                          {"ticks", ticks},
                          {"status", status},
                          {"samplingHashes", sampling_hashes}};
        content_status.insert("samplingInfo", sampling_info);
    }
    if (request.value("liquifyDialog").toBool()) {
        QTimer poll;
        poll.setInterval(25);
        int stage = 0, ticks = 0;
        bool ready = false;
        const auto actions = request.value("actions").toArray();
        QObject::connect(&poll, &QTimer::timeout, window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (++ticks > 1500 || dialog->objectName() != "liquifyDialog") {
                dialog->reject();
                poll.stop();
                return;
            }
            auto* view = dialog->findChild<QWidget*>("liquifyView");
            if (view->property("imageRect").toRectF().isEmpty() ||
                !view->property("ready").toBool())
                return;
            if (stage < actions.size()) {
                const auto action = actions[stage++].toObject();
                if (action.contains("file")) QTimer::singleShot(0, [action] {
                    if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                        file->selectFile(action.value("file").toString()); static_cast<QDialog*>(file)->accept();
                    }
                });
                if (action.contains("button"))
                    dialog->findChild<QAbstractButton*>(action.value("button").toString())->click();
                for (auto it = action.begin(); it != action.end(); ++it) {
                    if (auto* spin = dialog->findChild<QDoubleSpinBox*>(it.key()))
                        spin->setValue(it.value().toDouble());
                    if (auto* check = dialog->findChild<QCheckBox*>(it.key()))
                        check->setChecked(it.value().toBool());
                    if (auto* combo = dialog->findChild<QComboBox*>(it.key()))
                        combo->setCurrentIndex(it.value().toInt());
                }
                if (action.contains("key")) {
                    const int k = action.value("key").toInt();
                    const auto mods = Qt::KeyboardModifiers(action.value("modifiers").toInt());
                    QKeyEvent press(QEvent::KeyPress, k, mods),
                        release(QEvent::KeyRelease, k, mods);
                    QApplication::sendEvent(view, &press);
                    QApplication::sendEvent(view, &release);
                }
                if (action.contains("from")) {
                    const QRectF rect = view->property("imageRect").toRectF();
                    const auto from = action.value("from").toArray(),
                               to = action.value("to").toArray();
                    const QPointF a(rect.left() + from[0].toDouble() * rect.width(),
                                    rect.top() + from[1].toDouble() * rect.height()),
                        b(rect.left() + to[0].toDouble() * rect.width(),
                          rect.top() + to[1].toDouble() * rect.height());
                    const auto send_tablet=[&](QEvent::Type type,QPointF point) {
                        QTabletEvent event(type,QPointingDevice::primaryPointingDevice(),point,view->mapToGlobal(point),
                            action.value("pressure").toDouble(1),0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,
                            type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);
                        QApplication::sendEvent(view,&event);
                    };
                    const bool tablet=action.contains("pressure");
                    QMouseEvent press(QEvent::MouseButtonPress, a, view->mapToGlobal(a),
                                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    if(tablet)send_tablet(QEvent::TabletPress,a);else QApplication::sendEvent(view, &press);
                    for (int i = 1; i <= 12; ++i) {
                        const auto p = a + (b - a) * (i / 12.);
                        QMouseEvent move(QEvent::MouseMove, p, view->mapToGlobal(p), Qt::NoButton,
                                         Qt::LeftButton, Qt::NoModifier);
                        if(tablet)send_tablet(QEvent::TabletMove,p);else QApplication::sendEvent(view, &move);
                    }
                    QMouseEvent release(QEvent::MouseButtonRelease, b, view->mapToGlobal(b),
                                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    if(tablet)send_tablet(QEvent::TabletRelease,b);else QApplication::sendEvent(view, &release);
                }
                return;
            }
            ready = true;
            if (request.contains("capture"))
                dialog->grab().save(request.value("capture").toString());
            poll.stop();
            if (request.value("accept").toBool())
                dialog->accept();
            else
                dialog->reject();
        });
        poll.start();
        bool found = false;
        for (auto* action : window->findChildren<QAction*>())
            if (action->text() == QStringLiteral("液化…")) {
                found = true;
                action->trigger();
                break;
            }
        poll.stop();
        if (!found)
            throw EditError("liquify action missing");
        content_status = {{"ready", ready}, {"ticks", ticks}};
    }
    if (request.contains("patchDestination"))
        window->findChild<QRadioButton*>("patchDestination")
            ->setChecked(request.value("patchDestination").toBool());
    if (request.contains("settings"))
        canvas->set_brush_settings(brush_from_json(request.value("settings").toObject()));
    int count{};
    bool ready{};
    if (request.value("dialog").toBool()) {
        QTimer timer;
        timer.setInterval(20);
        int ticks{};
        QObject::connect(&timer, &QTimer::timeout, window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (dialog->objectName() != "brushSettingsDialog" || ++ticks > 200) {
                dialog->reject();
                timer.stop();
                return;
            }
            auto* presets = dialog->findChild<QListWidget*>("brushPresetList");
            count = presets->count();
            if (request.contains("preset"))
                presets->setCurrentRow(request.value("preset").toInt());
            if (request.contains("category"))
                dialog->findChild<QListWidget*>("brushCategories")
                    ->setCurrentRow(request.value("category").toInt());
            for (auto it = request.begin(); it != request.end(); ++it) {
                if (auto* spin = dialog->findChild<QDoubleSpinBox*>(it.key()))
                    spin->setValue(it.value().toDouble());
                if (auto* check = dialog->findChild<QCheckBox*>(it.key()))
                    check->setChecked(it.value().toBool());
                if (auto* combo = dialog->findChild<QComboBox*>(it.key()))
                    combo->setCurrentIndex(it.value().toInt());
            }
            if (request.contains("saveName")) {
                dialog->findChild<QLineEdit*>("brushPresetName")
                    ->setText(request.value("saveName").toString());
                dialog->findChild<QPushButton*>("brushSavePreset")->click();
            }
            if (request.value("remove").toBool())
                dialog->findChild<QPushButton*>("brushDeletePreset")->click();
            if (request.contains("capture"))
                dialog->grab().save(request.value("capture").toString());
            ready = true;
            timer.stop();
            if (request.value("accept").toBool())
                dialog->accept();
            else
                dialog->reject();
        });
        timer.start();
        bool found{};
        for (auto* action : window->findChildren<QAction*>())
            if (action->text() == QStringLiteral("画笔设置…")) {
                found = true;
                action->trigger();
                break;
            }
        timer.stop();
        if (!found)
            throw EditError("brush dialog action missing");
    }
    const auto rendered = render(canvas->project().state());
    if (request.contains("pixels"))
        display_image(rendered).save(request.value("pixels").toString());
    return {
        {"settings", brush_json(canvas->brush_settings())},
        {"crop", QJsonArray{canvas->crop_rectangle().x(), canvas->crop_rectangle().y(),
                            canvas->crop_rectangle().width(), canvas->crop_rectangle().height()}},
        {"cropAngle", canvas->crop_angle},
        {"cropRatio", canvas->crop_ratio},
        {"width", canvas->project().state().size.width()},
        {"height", canvas->project().state().size.height()},
        {"content", content_status},
        {"ready", ready},
        {"presetCount", count},
        {"history", int(canvas->project().history_position())},
        {"saved",
         QJsonDocument::fromJson(QSettings().value("brush/presets").toByteArray()).array()},
        {"hash",
         QString::fromLatin1(QCryptographicHash::hash(
                                 QByteArrayView(reinterpret_cast<const char*>(rendered.constBits()),
                                                rendered.sizeInBytes()),
                                 QCryptographicHash::Sha256)
                                 .toHex())}};
}
