#include "huelith/editing/portrait.h"
#include "huelith/editing/raw_recipe.h"
#include "tone_panel.h"
// Local Python automation adapter. Assertions and test scenarios live in Python;
// this DLL calls the same C++ targets linked into Huelith, without a test EXE.
#include "canvas_preferences.h"
#include "canvas_view.h"
#include <QGraphicsPixmapItem>
#include "export_dialog.h"
#include "huelith/editing/demosaic.h"
#include "huelith/editing/healing.h"
#include "huelith/editing/optics.h"
#include "huelith/editing/project.h"
#include "huelith/editing/raw_adjustments.h"
#include "huelith/editing/raw_cache.h"
#include "huelith/editing/raw_develop.h"
#include "huelith/editing/raw_geometry.h"
#include "huelith/editing/semantic.h"
#include "huelith/editing/selection_refine.h"
#include "huelith/editing/tiff_io.h"
#include "huelith/editing/white_balance.h"
#include "main_window.h"
#include "raw_dialog.h"
#include "semantic_dialog.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsView>
#include <QHelpEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cmath>
#include <memory>
#include <optional>
#include <QLibraryInfo>

QJsonObject design_command(huelith::desktop::MainWindow*, const QJsonObject&);
QJsonObject brush_command(huelith::desktop::MainWindow*, const QJsonObject&);
QJsonObject refine_command(const QJsonObject&);
QJsonObject object_command(huelith::desktop::MainWindow*, const QJsonObject&);
QJsonObject filmstrip_command(QDialog*, const QJsonObject&);
QJsonObject raw_profile_probe(const QJsonObject&);
QJsonObject raw_profile_ui(QDialog*, const QJsonObject&);
QJsonObject raw_sidecar_probe(const QJsonObject&);
QJsonObject enhancement_probe(const QJsonObject&);
QJsonObject enhancement_ui(huelith::desktop::MainWindow*, const QJsonObject&);
QJsonObject photo_merge_probe(const QJsonObject&);
QJsonObject photo_merge_ui(huelith::desktop::MainWindow*, const QJsonObject&);
QJsonObject dng_probe(const QJsonObject&);
QJsonObject metadata_probe(const QJsonObject&);
QJsonObject large_image_probe(const QJsonObject&);
QJsonObject work_window_probe(huelith::desktop::MainWindow*,const QJsonObject&);
QJsonObject embedded_probe(const QJsonObject&);
QJsonObject dng_ui(QDialog*, const QJsonObject&);
QJsonObject border_ui(huelith::desktop::MainWindow*, const QJsonObject&);
QJsonObject portrait_ui_command(const QJsonObject&);
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
    std::unique_ptr<huelith::desktop::RawDialog> raw_window;
    std::unique_ptr<huelith::desktop::SemanticDialog> semantic_window;
    std::unique_ptr<RawCache> raw_cache;
    std::unique_ptr<ExportQueue> export_queue;
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
    if (op == "portrait_window" || op == "portrait_state" || op == "portrait_close" || op == "portrait_geometry") return portrait_ui_command(o);
    if (op == "portrait_probe") {
        const std::atomic_bool cancelled{o["cancelled"].toBool()};
        auto people = huelith::editing::analyze_portrait(QImage(o["path"].toString()),
            o["models"].toString(), cancelled);
        std::vector<huelith::editing::PortraitPerson> previous;
        for (const auto& item : o["previous"].toArray()) previous.push_back(huelith::editing::portrait_person_from_json(item.toObject()));
        huelith::editing::match_portrait_people(people, previous);
        QJsonArray records;
        for (std::size_t i = 0; i < people.size(); ++i) {
            records.append(huelith::editing::portrait_person_json(people[i]));
            if (!o["maskPrefix"].toString().isEmpty() && !people[i].body_mask.isNull())
                people[i].body_mask.save(o["maskPrefix"].toString() + QString::number(i) + ".png");
        }
        return {{"people",records}};
    }
    if (op == "enhancement_probe") return enhancement_probe(o);
    if (op == "enhancement_ui") return enhancement_ui(d.window.get(), o);
    if (op == "photo_merge_probe") return photo_merge_probe(o);
    if (op == "photo_merge_ui") return photo_merge_ui(d.window.get(),o);
    if (op == "dng_probe") return dng_probe(o);
    if (op == "metadata_probe") return metadata_probe(o);
    if (op == "large_image_probe") return large_image_probe(o);
    if (op == "work_window_probe") return work_window_probe(d.window.get(),o);
    if (op == "embedded_probe") return embedded_probe(o);
    if (op == "dng_ui") return dng_ui(d.raw_window.get(),o);
    if (op == "border_ui") return border_ui(d.window.get(),o);
    if(op=="refine_window")return refine_command(o);
    if(op=="object_window")return object_command(d.window.get(),o);
    if(op=="brush_probe" || op=="window_brush") return brush_command(d.window.get(),o);
    if (op == "design_probe" || op == "window_design" || op == "window_style" ||
        op == "layout_probe")
        return design_command(d.window.get(), o);
    if (op == "white_balance_probe") {
        const auto settings = white_balance_from_json(o.value("settings").toObject());
        const auto xy = white_balance_xy(settings.temperature, settings.tint);
        QJsonObject result{{"xy", QJsonArray{xy.x(), xy.y()}},
                           {"roundtrip", white_balance_json(white_balance_temperature(xy))}};
        if (o.contains("xy")) {
            const auto input = o.value("xy").toArray();
            result.insert("converted", white_balance_json(white_balance_temperature(
                                           {input[0].toDouble(), input[1].toDouble()})));
        }
        if (o.contains("metadata")) {
            const auto metadata = o.value("metadata").toObject();
            result.insert("asShot", white_balance_json(white_balance_as_shot(metadata)));
            QImage input(4, 4, QImage::Format_RGBA32FPx4_Premultiplied);
            input.setColorSpace(working_space());
            const auto rgba = o.value("rgba").toArray();
            for (int i = 0; i < 16; ++i)
                for (int c = 0; c < 4; ++c)
                    reinterpret_cast<float*>(input.bits())[i * 4 + c] = float(rgba[c].toDouble());
            std::atomic_bool cancelled{o.value("cancelled").toBool()};
            const auto output = apply_white_balance(
                input, {{"metadata", metadata}, {"whiteBalance", white_balance_json(settings)}},
                &cancelled);
            QJsonArray values;
            for (int c = 0; c < 4; ++c)
                values.append(reinterpret_cast<const float*>(output.constBits())[c]);
            result.insert("rgba", values);
            if (o.value("sample").toBool())
                result.insert("sample", white_balance_json(
                                            white_balance_sample(input, metadata, input.rect())));
        }
        return result;
    }
    if (op == "raw_profile_probe") return raw_profile_probe(o);
    if (op == "raw_sidecar_probe") return raw_sidecar_probe(o);
    if (op == "raw_mask_probe") {
        const int width=o.value("width").toInt(100),height=o.value("height").toInt(80);
        if(width<=0 || height<=0 || qint64(width)*height>1000000)throw EditError("Invalid mask probe size");
        QImage reference(width,height,QImage::Format_RGBA32FPx4_Premultiplied);reference.setColorSpace(working_space());
        const auto pixels=o.value("rgba").toArray();
        if(!pixels.isEmpty() && pixels.size()!=width*height*4)throw EditError("Invalid probe pixels");
        for(int y=0;y<height;++y){auto* row=reinterpret_cast<float*>(reference.scanLine(y));for(int x=0;x<width*4;++x)row[x]=pixels.isEmpty()?(x%4==3?1.F:.2F):float(pixels[y*width*4+x].toDouble());}
        const auto mask=raw_local_mask(reference.size(),o.value("local").toObject(),o.value("geometry").toObject(),reference);
        const auto display=display_image(reference).convertToFormat(QImage::Format_ARGB32);
        QJsonArray coverage,encoded;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){coverage.append(int(mask.constScanLine(y)[x]));const auto c=display.pixelColor(x,y);encoded.append(QJsonArray{c.red(),c.green(),c.blue(),c.alpha()});}
        return {{"mask",coverage},{"display",encoded}};
    }
    if (op == "raw_local_probe") {
        QImage input(100, 80, QImage::Format_RGBA32FPx4_Premultiplied);
        input.setColorSpace(working_space());
        for (int y = 0; y < input.height(); ++y) {
            auto* row = reinterpret_cast<float*>(input.scanLine(y));
            for (int x = 0; x < input.width(); ++x) {
                row[x * 4] = .2F;
                row[x * 4 + 1] = .2F;
                row[x * 4 + 2] = .2F;
                row[x * 4 + 3] = .5F;
            }
        }
        const auto recipe = o.value("recipe").toObject();
        const auto output = apply_raw_adjustments(input, recipe);
        const auto mask =
            raw_local_mask(input.size(), recipe.value("locals").toArray().first().toObject(), recipe.value("geometry").toObject(),input);
        QJsonArray samples;
        for (const auto& value : o.value("points").toArray()) {
            const auto p = value.toArray();
            const int x = p[0].toInt(), y = p[1].toInt();
            const auto* pixel = reinterpret_cast<const float*>(output.constScanLine(y)) + x * 4;
            samples.append(QJsonObject{{"rgba", QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]}},
                                       {"mask", int(mask.constScanLine(y)[x])}});
        }
        return {{"samples", samples}, {"width", output.width()}, {"height", output.height()}};
    }
    if (op == "export_queue") {
        QJsonObject window_state;
        if (!d.export_queue || o.value("reset").toBool())
            d.export_queue = std::make_unique<ExportQueue>();
        auto* queue = d.export_queue.get();
        if (o.value("window").toBool()) {
            auto* dialog = dynamic_cast<huelith::desktop::ExportDialog*>(
                d.window->findChild<QDialog*>(o.value("windowName").toString("exportQueueDialog")));
            if (!dialog)
                throw EditError("export queue window not open");
            queue = &dialog->queue();
            for (auto it = o.begin(); it != o.end(); ++it) {
                if (auto* line = dialog->findChild<QLineEdit*>(it.key()))
                    line->setText(it.value().toString());
                if (auto* combo = dialog->findChild<QComboBox*>(it.key()))
                    combo->setCurrentIndex(it.value().toInt());
                if (auto* spin=dialog->findChild<QSpinBox*>(it.key()))spin->setValue(it.value().toInt());
                if (auto* check=dialog->findChild<QCheckBox*>(it.key()))check->setChecked(it.value().toBool());
            }
            if (o.contains("click"))
                if (auto* button = dialog->findChild<QPushButton*>(o.value("click").toString()))
                    button->click();
            if (o.contains("capture"))
                dialog->grab().save(o.value("capture").toString());
            for(auto* combo:dialog->findChildren<QComboBox*>()){QJsonArray names;for(int i=0;i<combo->count();++i)names.append(combo->itemText(i));window_state.insert(combo->objectName(),QJsonObject{{"index",combo->currentIndex()},{"items",names}});}
            for(auto* line:dialog->findChildren<QLineEdit*>())if(line->objectName().startsWith("exportQueue"))window_state.insert(line->objectName(),line->text());
            for(auto* spin:dialog->findChildren<QSpinBox*>())if(spin->objectName().startsWith("exportQueue"))window_state.insert(spin->objectName(),spin->value());
            window_state.insert("alpha",dialog->findChild<QCheckBox*>("exportQueueAlpha")->isChecked());
        }
        quint64 id{};
        if (o.contains("path")) {
            ExportOptions options;
            options.path = o.value("path").toString();
            options.format = o.value("format").toString("png").toLatin1();
            options.bits = o.value("bits").toInt(8);
            const std::array<QColorSpace::NamedColorSpace,4> spaces{QColorSpace::SRgb,QColorSpace::AdobeRgb,QColorSpace::DisplayP3,QColorSpace::ProPhotoRgb};
            const int profile=o.value("profile").toInt();if(profile<0 || profile>=4)throw EditError("Invalid export profile");
            options.space=QColorSpace(spaces[profile]);options.alpha=o.value("alpha").toBool(true);options.matte=QColor(o.value("matte").toString("#ffffff"));options.quality=o.value("quality").toInt(95);
            options.size = {o.value("width").toInt(), o.value("height").toInt()};
            options.conflict = static_cast<ExportConflict>(o.value("conflict").toInt());
            options.protected_paths = {o.value("protected").toString()};
            id = queue->add(d.window->active_canvas()->project().snapshot(), options);
        }
        if (o.value("start").toBool())
            queue->start();
        if (o.contains("cancel"))
            queue->cancel(quint64(o.value("cancel").toInteger()));
        if (o.value("cancelAll").toBool())
            queue->cancel_all();
        if (o.value("retry").toBool())
            queue->retry_failed();
        QJsonArray items;
        for (const auto& item : queue->items())
            items.append(QJsonObject{{"id", qint64(item.id)},
                                     {"revision", qint64(item.revision)},
                                     {"path", item.path},
                                     {"status", int(item.status)},
                                     {"error", item.error}});
        return {{"id", qint64(id)}, {"items", items},{"windowState",window_state}};
    }
    if (op == "demosaic_probe") {
        const int width = o.value("width").toInt(), height = o.value("height").toInt();
        const auto pixels = o.value("mosaic").toArray();
        const auto pattern = o.value("pattern").toString("RGGB");
        if (width < 4 || height < 4 || width > 512 || height > 512 || width % 2 || height % 2 ||
            pixels.size() != width * height ||
            !QStringList{"RGGB", "BGGR", "GRBG", "GBRG"}.contains(pattern))
            throw EditError("Invalid Bayer fixture");
        std::array<QPoint, 4> positions;
        std::vector<float> packed(std::size_t(width) * height);
        const auto plane = std::size_t(width / 2) * (height / 2);
        int green{};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const auto letter = pattern[y * 2 + x];
                const int c = letter == 'R' ? 0 : letter == 'B' ? 3 : ++green;
                positions[c] = {x, y};
                for (int yy = 0; yy < height / 2; ++yy)
                    for (int xx = 0; xx < width / 2; ++xx)
                        packed[std::size_t(c) * plane + std::size_t(yy) * (width / 2) + xx] =
                            float(pixels[(2 * yy + y) * width + 2 * xx + x].toDouble());
            }
        QImage image(width, height, QImage::Format_RGBA32FPx4_Premultiplied);
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        demosaic_malvar(packed, positions, image, cancelled);
        QJsonArray output;
        for (int y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const float*>(image.constScanLine(y));
            for (int x = 0; x < width; ++x)
                output.append(
                    QJsonArray{row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]});
        }
        return {{"pixels", output}, {"quality", raw_high_quality(o.value("recipe").toObject())}};
    }
    if (op == "window_tooltip") {
        d.window->raise();
        d.window->activateWindow();
        QApplication::processEvents();
        auto* button = d.window->findChild<QToolButton*>("toolButton_7");
        const bool fade = QApplication::isEffectEnabled(Qt::UI_FadeTooltip);
        const bool animate = QApplication::isEffectEnabled(Qt::UI_AnimateTooltip);
        QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
        QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
        QToolTip::hideText();
        const auto point = button->rect().center();
        QHelpEvent event(QEvent::ToolTip, point, button->mapToGlobal(point));
        QApplication::sendEvent(button, &event);
        const bool visible = QToolTip::isVisible();
        QToolTip::hideText();
        QApplication::setEffectEnabled(Qt::UI_FadeTooltip, fade);
        QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, animate);
        return {{"visible", visible},
                {"text", button->toolTip()},
                {"buttonVisible", button->isVisible()},
                {"enabled", button->isEnabled()},
                {"active", d.window->isActiveWindow()},
                {"preference", QSettings().value("tools/showTooltips", true).toBool()}};
    }
    if(op=="raw_recipe_probe") {
        if(o.contains("data"))return {{"preset",read_raw_preset(o.value("data").toString().toUtf8())}};
        RawGroups groups{};for(const auto& index:o.value("groups").toArray()){if(index.toInt()<0 || index.toInt()>=14)throw EditError("Invalid group");groups[index.toInt()]=true;}
        const auto preset=raw_preset(o.value("name").toString("Test"),o.value("source").toObject(),groups);
        return {{"preset",read_raw_preset(QJsonDocument(preset).toJson())},{"target",transfer_raw_settings(o.value("source").toObject(),o.value("target").toObject(),groups)}};
    }
    if(op=="target_probe") {
        huelith::desktop::TonePanel panel(tone_from_json(o.value("settings").toObject()));
        panel.set_target_mode(o.value("mode").toInt());
        const auto rgb=o.value("rgb").toArray();
        const bool started=panel.begin_target(QColor::fromRgbF(rgb[0].toDouble(),rgb[1].toDouble(),rgb[2].toDouble()));
        panel.move_target(o.value("amount").toDouble());panel.finish_target(o.value("cancel").toBool());
        return {{"started",started},{"settings",tone_json(panel.settings())}};
    }
    if(op=="curve_probe") {
        const auto settings=tone_from_json(o.value("settings").toObject());QJsonArray result;
        const int channel=o.value("channel").toInt();if(channel<0||channel>3)throw EditError("curve channel invalid");
        for(const auto value:o.value("values").toArray())result.append(o.value("parametric").toBool()?evaluate_parametric_curve(value.toDouble(),settings):evaluate_point_curve(value.toDouble(),channel==0?settings.curve:settings.channel_curves[channel-1],settings.legacy_curve_interpolation));
        return {{"values",result},{"settings",tone_json(settings)}};
    }
    if (op == "canvas_preferences_state") {
        const auto p = huelith::desktop::read_canvas_preferences();
        auto* canvas = d.window->active_canvas();
        if (o.contains("grid"))
            canvas->show_grid = o.value("grid").toBool();
        canvas->repaint();
        const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QJsonObject counts;
        for (const auto& name :
             {p.checker_light.name(), p.checker_dark.name(), p.canvas_color.name()}) {
            int count{};
            const auto rgb = QColor(name).rgb();
            for (int y = 0; y < image.height(); ++y) {
                const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    if (row[x] == rgb)
                        ++count;
            }
            counts.insert(name, count);
        }
        if (o.contains("capture"))
            image.save(o.value("capture").toString());
        return {{"rawPerformance",QJsonObject{{"backend",p.raw_backend},{"denoise",p.raw_denoise},{"highQuality",p.raw_high_quality},{"amount",p.raw_denoise_amount},{"preview",p.raw_preview_edge},{"drag",p.raw_drag_edge},{"delay",p.raw_refine_delay},{"memoryMB",p.raw_memory_mb},{"diskMB",p.raw_disk_mb}}},
                {"paintingCursor", p.painting_cursor},
                {"tool", int(canvas->tool())},
                {"shiftToolSwitch", p.shift_tool_switch},
                {"springTools", p.spring_tools},
                {"springDelay", p.spring_delay},
                {"historyLimit", p.history_states},
                {"historyCount", canvas->project().history_names().size()},
                {"crosshair", p.cursor_crosshair},
                {"unit", p.ruler_unit},
                {"gridSpacing", canvas->grid_spacing},
                {"divisions", p.grid_subdivisions},
                {"snapGuides", p.snap_guides},
                {"snapGrid", p.snap_grid},
                {"snapLayers", p.snap_layers},
                {"snapDocument", p.snap_document},
                {"snapDistance", p.snap_distance},
                {"checkerSize", p.checker_size},
                {"cursorShape", int(canvas->cursor().shape())},
                {"colors", counts},
                {"unitX", huelith::desktop::ruler_units_per_pixel(
                              p.ruler_unit, canvas->project().state().ppi, canvas->spec().width)},
                {"unitY", huelith::desktop::ruler_units_per_pixel(
                              p.ruler_unit, canvas->project().state().ppi, canvas->spec().height)},
                {"digest", QString::fromLatin1(
                               QCryptographicHash::hash(
                                   QByteArrayView(reinterpret_cast<const char*>(image.constBits()),
                                                  image.sizeInBytes()),
                                   QCryptographicHash::Sha256)
                                   .toHex())},
                {"revision", qint64(canvas->project().revision())}};
    }
    if (op == "optics_probe" || op == "geometry_probe") {
        const bool geometry = op == "geometry_probe";
        const auto settings =
            optics_from_json(geometry ? QJsonObject{} : o.value("settings").toObject());
        const auto geometric =
            geometry_from_json(geometry ? o.value("settings").toObject() : QJsonObject{});
        const int width = o.value("width").toInt(64), height = o.value("height").toInt(48);
        if (width < 2 || height < 2 || width > 256 || height > 256)
            throw EditError("Probe size invalid");
        QImage source(width, height, QImage::Format_RGBA32FPx4_Premultiplied);
        source.setColorSpace(working_space());
        for (int y = 0; y < height; ++y) {
            auto* row = reinterpret_cast<float*>(source.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const float alpha = o.value("alpha").toBool() ? .5F : 1.F;
                row[x * 4] = float(x) / (width - 1) * alpha;
                row[x * 4 + 1] = float(y) / (height - 1) * alpha;
                row[x * 4 + 2] = .2F * alpha;
                row[x * 4 + 3] = alpha;
            }
        }
        if (o.contains("pixels")) {
            const auto input = o.value("pixels").toArray();
            if (input.size() != width * height)
                throw EditError("Pixel fixture length invalid");
            for (int y = 0; y < height; ++y) {
                auto* row = reinterpret_cast<float*>(source.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    const auto rgba = input[y * width + x].toArray();
                    if (rgba.size() != 4)
                        throw EditError("RGBA fixture invalid");
                    for (int c = 0; c < 4; ++c)
                        row[x * 4 + c] = float(rgba[c].toDouble());
                }
            }
        }
        if (o.value("srgbLinear").toBool()) {
            source.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
            source = source.convertedToColorSpace(working_space());
        }
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        auto result = geometry ? apply_geometry(source, geometric, &cancelled)
                               : apply_optics(source, settings, &cancelled);
        const bool identity = source == result;
        if (o.value("srgbLinear").toBool())
            result = result.convertedToColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        QJsonArray pixels;
        for (int y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const float*>(result.constScanLine(y));
            for (int x = 0; x < width; ++x)
                pixels.append(
                    QJsonArray{row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]});
        }
        const auto crop = geometry_crop_rect(source.size(), geometric);
        return {{"pixels", pixels},
                {"settings", geometry ? geometry_json(geometric) : optics_json(settings)},
                {"identity", identity},
                {"crop", QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()}}};
    }
    if (op == "lens_profiles") {
        const auto profiles = lens_profiles(o.value("directory").toString());
        QJsonArray result;
        for (const auto& p : profiles)
            result.append(QJsonObject{{"maker", p.maker},
                                      {"model", p.model},
                                      {"xml", p.xml},
                                      {"distortion", p.distortion},
                                      {"vignette", p.vignette},
                                      {"ca", p.ca},
                                      {"cropFactor", p.crop_factor}});
        return {{"profiles", result},
                {"match", match_lens_profile(profiles, o.value("metadata").toObject())}};
    }
    if (op == "point_color_probe") {
        const auto input = o.value("rgb").toArray();
        if (input.size() != 3)
            throw EditError("RGB required");
        const std::array<double, 3> rgb{input[0].toDouble(), input[1].toDouble(),
                                        input[2].toDouble()};
        auto colors = point_colors_from_json(o.value("colors").toArray());
        if (o.value("sample").toBool())
            colors.push_back(sample_point_color(QColor::fromRgbF(rgb[0], rgb[1], rgb[2])));
        validate_point_colors(colors);
        const auto output = apply_point_colors(rgb, colors);
        const auto hsl = point_color_hsl(rgb);
        QJsonArray weights;
        for (const auto& color : colors)
            weights.append(point_color_weight(hsl, color));
        return {{"rgb", QJsonArray{output[0], output[1], output[2]}},
                {"hsl", QJsonArray{hsl[0], hsl[1], hsl[2]}},
                {"weights", weights},
                {"colors", point_colors_json(colors)}};
    }
    if (op == "calibration_probe") {
        const auto settings = calibration_from_json(o.value("settings").toObject());
        const auto transform = calibration_transform(settings);
        const auto input = o.value("rgb").toArray();
        if (input.size() != 3)
            throw EditError("RGB required");
        const auto output =
            transform.apply({input[0].toDouble(), input[1].toDouble(), input[2].toDouble()});
        QJsonArray matrix;
        for (const auto& row : transform.matrix)
            matrix.append(QJsonArray{row[0], row[1], row[2]});
        return {{"rgb", QJsonArray{output[0], output[1], output[2]}},
                {"matrix", matrix},
                {"settings", calibration_json(settings)}};
    }
    if (op == "refine_probe") {
        QImage source(o.value("image").toString());
        QImage mask(o.value("mask").toString());
        if(!mask.isNull()) mask=mask.convertToFormat(QImage::Format_Grayscale8);
        const auto options=o.value("settings").toObject();
        SelectionRefineSettings settings;
        settings.radius=options.value("radius").toDouble();
        settings.smart_radius=options.value("smart").toBool();
        settings.smooth=options.value("smooth").toDouble();
        settings.feather=options.value("feather").toDouble();
        settings.contrast=options.value("contrast").toDouble();
        settings.shift=options.value("shift").toDouble();
        settings.invert=options.value("invert").toBool();
        settings.decontaminate=options.value("decontaminate").toDouble();
        QImage area(o.value("area").toString());
        if(!area.isNull()) area=area.convertToFormat(QImage::Format_Grayscale8);
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        QElapsedTimer timer;timer.start();
        const auto result=refine_selection(source,mask,settings,area,&cancelled,o.value("scale").toDouble(1));
        if(o.contains("output"))result.mask.save(o.value("output").toString());
        if(o.contains("pixels") && !result.pixels.isNull()){
            QFile file(o.value("pixels").toString());if(!file.open(QIODevice::WriteOnly))throw EditError("Cannot save refined pixels");
            for(int y=0;y<result.pixels.height();++y)file.write(reinterpret_cast<const char*>(result.pixels.constScanLine(y)),result.pixels.width()*16);
        }
        return {{"ms",timer.elapsed()},{"width",result.mask.width()},{"height",result.mask.height()},{"recolored",!result.pixels.isNull()}};
    }
    if (op == "instance_probe") {
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        QElapsedTimer timer;
        timer.start();
        const int delay = o.value("cancelAfterMs").toInt(-1);
        std::jthread cancel([&](std::stop_token stop) {
            while (delay >= 0 && !stop.stop_requested() && timer.elapsed() < delay)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (delay >= 0 && !stop.stop_requested())
                cancelled = true;
        });
        const auto image = QImage(o.value("path").toString());
        const auto instances = semantic_instances(image,
            o.value("model").toString("build/windows-release/models/maskrcnn-r50-v2.onnx"), cancelled);
        cancel.request_stop();
        QJsonArray result;
        int index{};
        for (const auto& instance : instances) {
            const auto path = o.value("output").toString() + QString("/mask-%1.png").arg(index++);
            if (o.contains("output") && !instance.mask.save(path))
                throw EditError("Could not write instance mask");
            result.append(QJsonObject{{"category",instance.category},{"name",semantic_category_name(instance.category)},
                {"confidence",instance.confidence},{"pixels",instance.pixels},{"width",instance.mask.width()},
                {"height",instance.mask.height()},{"bounds",QJsonArray{instance.bounds.x(),instance.bounds.y(),instance.bounds.width(),instance.bounds.height()}},
                {"mask",path}});
        }
        return {{"instances",result},{"ms",timer.elapsed()}};
    }
    if (op == "semantic_window") {
        d.semantic_window = std::make_unique<huelith::desktop::SemanticDialog>(
            QImage(o.value("path").toString()), SemanticTarget(o.value("target").toInt(3)), nullptr);
        d.semantic_window->open();
        QApplication::processEvents();
        return {{"opened",true}};
    }
    if (op == "semantic_window_state") {
        auto* dialog = d.semantic_window.get();
        if (!dialog) throw EditError("No semantic window");
        if(o.contains("target")) dialog->findChild<QComboBox*>("semanticTarget")->setCurrentIndex(o.value("target").toInt());
        if(o.contains("person")) dialog->findChild<QComboBox*>("semanticPerson")->setCurrentIndex(o.value("person").toInt());
        if(o.contains("invert")) dialog->findChild<QCheckBox*>("semanticInvert")->setChecked(o.value("invert").toBool());
        if(o.contains("click")) {
            auto* label=dialog->findChild<QLabel*>("semanticPreview");
            const auto size=label->pixmap().size();const auto p=o.value("click").toArray();
            const QPointF position((label->width()-size.width())*.5+p[0].toDouble()*size.width(),(label->height()-size.height())*.5+p[1].toDouble()*size.height());
            QMouseEvent press(QEvent::MouseButtonPress,position,label->mapToGlobal(position.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(label,&press);
        }
        if(o.contains("save")) dialog->mask().save(o.value("save").toString());
        if(o.contains("capture")) dialog->grab().save(o.value("capture").toString());
        if(o.value("accept").toBool()) dialog->accept();
        if(o.value("reject").toBool()) dialog->reject();
        auto* candidates=dialog->findChild<QComboBox*>("semanticPerson");
        QJsonArray names;for(int i=0;i<candidates->count();++i) names.append(candidates->itemText(i));
        return {{"ready",dialog->property("semanticReady").toBool()},{"error",dialog->property("semanticError").toBool()},
            {"status",dialog->findChild<QLabel*>("semanticStatus")->text()},
            {"count",candidates->count()},{"names",names},{"person",candidates->currentIndex()},
            {"selected",dialog->property("selectedPixels").toLongLong()},{"result",dialog->result()},
            {"visible",dialog->isVisible()}};
    }
    if (op == "semantic_probe") {
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        const auto labels = semantic_labels(
            QImage(o.value("path").toString()),
            o.value("model").toString("build/windows-release/models/pp-mobileseg-base.onnx"),
            cancelled);
        if (o.contains("labels"))
            labels.save(o.value("labels").toString());
        QJsonArray counts;
        std::array<int, 150> histogram{};
        for (int y = 0; y < labels.height(); ++y)
            for (int x = 0; x < labels.width(); ++x)
                ++histogram[labels.constScanLine(y)[x]];
        for (int value : histogram)
            counts.append(value);
        QJsonArray people;
        for (const auto& region : semantic_people(labels))
            people.append(QJsonObject{{"pixels", region.pixels},
                                      {"x", region.bounds.x()},
                                      {"y", region.bounds.y()},
                                      {"width", region.bounds.width()},
                                      {"height", region.bounds.height()}});
        return {{"counts", counts},
                {"people", people},
                {"width", labels.width()},
                {"height", labels.height()}};
    }
    if (op == "detail_probe") {
        const int w = o.value("width").toInt(), h = o.value("height").toInt();
        const auto values = o.value("pixels").toArray();
        if (w < 1 || h < 1 || w > 512 || h > 512 || values.size() != w * h * 4)
            throw EditError("Invalid detail fixture");
        QImage image(w, h, QImage::Format_RGBA32FPx4_Premultiplied);
        image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        for (int i = 0; i < values.size(); ++i)
            reinterpret_cast<float*>(image.bits())[i] = float(values[i].toDouble());
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        const auto settings = detail_from_json(o.value("settings").toObject());
        const auto result =
            apply_detail(image, settings, o.value("amount").toDouble(), &cancelled,
                         o.value("pixelScale").toDouble(1), o.value("preview").toInt());
        QJsonArray pixels;
        for (int i = 0; i < values.size(); ++i)
            pixels.append(reinterpret_cast<const float*>(result.constBits())[i]);
        return {{"pixels", pixels}, {"settings", detail_json(settings)}};
    }
    if (op == "tone_spatial_probe") {
        QImage source(128, 96, QImage::Format_RGBA32FPx4_Premultiplied);
        source.setColorSpace(working_space());
        for (int y = 0; y < source.height(); ++y) {
            auto* row = reinterpret_cast<float*>(source.scanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                const float alpha = x < 4 ? 0.F : .5F;
                const float value = .2F + (x % 16 < 8 ? .04F : -.04F) + y / 200.F;
                row[x * 4] = value * alpha;
                row[x * 4 + 1] = value * .8F * alpha;
                row[x * 4 + 2] = value * .6F * alpha;
                row[x * 4 + 3] = alpha;
            }
        }
        const auto settings = tone_from_json(o.value("settings").toObject());
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        const auto result = apply_tone(source, settings, &cancelled);
        double maximum{};
        for (int y = 0; y < result.height(); ++y) {
            const auto* a = reinterpret_cast<const float*>(source.constScanLine(y));
            const auto* b = reinterpret_cast<const float*>(result.constScanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                if (a[x * 4 + 3] != b[x * 4 + 3])
                    throw EditError("Effect changed alpha");
                for (int c = 0; c < 3; ++c) {
                    if (!std::isfinite(b[x * 4 + c]))
                        throw EditError("Non-finite effect output");
                    maximum = std::max(maximum, double(std::abs(a[x * 4 + c] - b[x * 4 + c])));
                }
            }
        }
        const auto bytes =
            QByteArrayView(reinterpret_cast<const char*>(result.constBits()), result.sizeInBytes());
        return {
            {"maximumChange", maximum},
            {"sha256", QString::fromLatin1(
                           QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
    }
    if (op == "effect_probe") {
        QImage input(2, 1, QImage::Format_RGBA32FPx4_Premultiplied);
        input.setColorSpace(working_space());
        const auto rgba = o.value("rgba").toArray();
        if (rgba.size() != 4)
            throw EditError("RGBA required");
        auto* values = reinterpret_cast<float*>(input.bits());
        for (int i = 0; i < 8; ++i)
            values[i] = float(rgba[i % 4].toDouble());
        std::vector<Effect> effects;
        for (const auto& value : o.value("effects").toArray()) {
            const auto json = value.toObject();
            Effect effect;
            effect.name = json.value("name").toString("effect");
            effect.enabled = json.value("enabled").toBool(true);
            effect.amount = json.value("amount").toDouble(1);
            effect.tone = tone_from_json(json.value("tone").toObject());
            if (json.contains("lut"))
                effect.lut = parse_cube(json.value("lut").toString());
            if (json.contains("mask")) {
                effect.mask = QImage(2, 1, QImage::Format_Grayscale8);
                const auto mask = json.value("mask").toArray();
                if (mask.size() != 2)
                    throw EditError("Two mask values required");
                for (int i = 0; i < 2; ++i)
                    effect.mask.bits()[i] = uchar(mask[i].toInt());
            }
            effects.push_back(std::move(effect));
        }
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        const auto image = apply_effects(input, effects, &cancelled);
        QJsonArray pixels;
        for (int i = 0; i < 8; ++i)
            pixels.append(reinterpret_cast<const float*>(image.constBits())[i]);
        State state;
        state.name = "effects";
        state.size = input.size();
        Layer layer;
        layer.name = "effect pixels";
        layer.pixels = input;
        layer.effects = effects;
        state.layers = {layer};
        if (render(state) != image)
            throw EditError("Render does not apply effects");
        if (o.contains("save")) {
            save_project({state, 1}, o.value("save").toString());
            const auto loaded = load_project(o.value("save").toString());
            if (render(loaded) != image || loaded.layers[0].effects.size() != effects.size())
                throw EditError("Effects persistence mismatch");
        }
        return {{"pixels", pixels}};
    }
    if (op == "healing_probe") {
        const int w = o.value("width").toInt(), h = o.value("height").toInt();
        const auto data = o.value("pixels").toArray();
        if (w < 1 || h < 1 || qint64(w) * h > 1000000 || data.size() != qint64(w) * h * 4)
            throw EditError("Invalid healing fixture");
        QImage input(w, h, QImage::Format_RGBA32FPx4_Premultiplied);
        input.setColorSpace(working_space());
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<float*>(input.scanLine(y));
            for (int x = 0; x < w * 4; ++x)
                row[x] = float(data[y * w * 4 + x].toDouble());
        }
        auto strokes = o.value("strokes").toArray();
        if (o.value("automatic").toBool()) {
            auto stroke = healing_from_json(strokes[0].toObject());
            stroke.offset = automatic_healing_offset(input, stroke, o.value("alternative").toInt());
            strokes[0] = healing_json(stroke);
        }
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        QElapsedTimer healing_timer;
        healing_timer.start();
        const auto result = apply_healing(input, strokes, &cancelled);
        const double healing_ms = healing_timer.nsecsElapsed() / 1000000.;
        QJsonArray pixels;
        for (int y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const float*>(result.constScanLine(y));
            for (int x = 0; x < w * 4; ++x)
                pixels.append(row[x]);
        }
        return {{"pixels", pixels}, {"strokes", strokes}, {"elapsedMs", healing_ms}};
    }
    if (op == "film_probe") {
        const int width = o.value("width").toInt(128), height = o.value("height").toInt(96);
        if (width < 1 || height < 1 || qint64(width) * height > 1000000)
            throw EditError("Invalid film probe dimensions");
        const auto settings = tone_from_json(o.value("settings").toObject());
        QImage input(width, height, QImage::Format_RGBA32FPx4_Premultiplied);
        input.setColorSpace(QColorSpace::SRgbLinear);
        const auto values = o.value("rgba").toArray();
        if (values.size() != 4)
            throw EditError("RGBA required");
        for (int y = 0; y < height; ++y) {
            auto* row = reinterpret_cast<float*>(input.scanLine(y));
            for (int x = 0; x < width; ++x)
                for (int c = 0; c < 4; ++c)
                    row[x * 4 + c] = float(values[c].toDouble());
        }
        auto source = to_working(input);
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        QImage result;
        if (o.contains("crop"))
            result = apply_raw_adjustments(
                source, {{"tone", tone_json(settings)}, {"crop", o.value("crop")}}, true,
                &cancelled, o.value("scale").toDouble(1));
        else
            result = apply_tone(source, settings, &cancelled, o.value("scale").toDouble(1));
        if (o.contains("save")) {
            State state;
            state.name = "Film effects validation";
            state.size = source.size();
            Layer layer;
            layer.name = "film";
            layer.pixels = source;
            layer.tone = settings;
            state.layers.push_back(layer);
            save_project({state, 1}, o.value("save").toString());
            const auto loaded = load_project(o.value("save").toString());
            if (loaded.layers.front().tone != settings || render(loaded) != render(state))
                throw EditError("Film persistence mismatch");
        }
        if (o.contains("capture"))
            display_image(result).save(o.value("capture").toString());
        QJsonArray working_pixels, input_working;
        for (int y = 0; y < result.height(); ++y) {
            const auto* row = reinterpret_cast<const float*>(result.constScanLine(y));
            for (int x = 0; x < result.width() * 4; ++x)
                working_pixels.append(row[x]);
        }
        for (int c = 0; c < 4; ++c)
            input_working.append(reinterpret_cast<const float*>(source.constBits())[c]);
        result = result.convertedToColorSpace(QColorSpace::SRgbLinear, result.format());
        QJsonArray pixels;
        for (int y = 0; y < result.height(); ++y) {
            const auto* row = reinterpret_cast<const float*>(result.constScanLine(y));
            for (int x = 0; x < result.width() * 4; ++x)
                pixels.append(row[x]);
        }
        return {{"pixels", pixels},
                {"workingPixels", working_pixels},
                {"inputWorking", input_working},
                {"width", result.width()},
                {"height", result.height()},
                {"settings", tone_json(settings)}};
    }
    if (op == "tone_probe") {
        const auto settings = tone_from_json(o.value("settings").toObject());
        QImage input(1, 1, QImage::Format_RGBA32FPx4_Premultiplied);
        input.setColorSpace(working_space());
        const auto values = o.value("rgba").toArray();
        if (values.size() != 4)
            throw EditError("RGBA required");
        auto* source = reinterpret_cast<float*>(input.bits());
        for (int i = 0; i < 4; ++i)
            source[i] = float(values[i].toDouble());
        const auto image = apply_tone(input, settings);
        const auto* pixel = reinterpret_cast<const float*>(image.constBits());
        QJsonArray result;
        for (int i = 0; i < 4; ++i)
            result.append(pixel[i]);
        if (o.contains("save")) {
            State state;
            state.name = "tone";
            state.size = {1, 1};
            Layer layer;
            layer.name = "pixels";
            layer.pixels = input;
            layer.tone = settings;
            state.layers = {layer};
            save_project({state, 1}, o.value("save").toString());
            const auto loaded = load_project(o.value("save").toString());
            if (loaded.layers[0].tone != settings || render(loaded) != render(state))
                throw EditError("Tone persistence mismatch");
        }
        return {{"rgba", result}};
    }
    if (op == "tiff_roundtrip") {
        QImage image(1024, 2, QImage::Format_RGBA64);
        image.setColorSpace(QColorSpace::AdobeRgb);
        for (int y = 0; y < image.height(); ++y) {
            auto* row = reinterpret_cast<QRgba64*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
                row[x] = QRgba64::fromRgba64(quint16(x * 61), quint16(65535 - x * 47), 12345,
                                             y == 0 ? 65535 : 32768);
        }
        write_tiff(image, o.value("path").toString(), 16,
                   std::atomic_bool{o.value("cancelled").toBool()});
        const auto reopened = read_tiff(o.value("path").toString());
        const auto expected = to_working(image);
        double maximum{};
        for (int y = 0; y < image.height(); ++y) {
            const auto* actual = reinterpret_cast<const float*>(reopened.constScanLine(y));
            const auto* reference = reinterpret_cast<const float*>(expected.constScanLine(y));
            for (int x = 0; x < image.width() * 4; ++x)
                maximum = std::max(maximum, double(std::abs(actual[x] - reference[x])));
        }
        return {
            {"maximumError", maximum}, {"width", reopened.width()}, {"height", reopened.height()}};
    }
    if (op == "raw_cache") {
        if (o.contains("memoryBudget"))
            d.raw_cache = std::make_unique<RawCache>(o.value("memoryBudget").toInteger(),
                                                     o.value("diskBudget").toInteger());
        if (!d.raw_cache)
            throw EditError("Cache not initialized");
        if(o.value("clear").toBool())d.raw_cache->clear();
        if(o.contains("configureMemory"))d.raw_cache->configure(o.value("configureMemory").toInteger(),o.value("configureDisk").toInteger());
        const auto key = o.value("key").toString();
        if (o.contains("pixels")) {
            QImage image(2, 2, QImage::Format_RGBA32FPx4_Premultiplied);
            image.setColorSpace(working_space());
            const auto values = o.value("pixels").toArray();
            if (values.size() != 16)
                throw EditError("Expected 16 float components");
            for (int i = 0; i < 16; ++i)
                reinterpret_cast<float*>(image.bits())[i] = float(values[i].toDouble());
            QImage reference;
            if (o.contains("reference")) {
                reference = image.copy();
                const auto original = o.value("reference").toArray();
                if (original.size() != 16)
                    throw EditError("Expected 16 reference components");
                for (int i = 0; i < 16; ++i)
                    reinterpret_cast<float*>(reference.bits())[i] = float(original[i].toDouble());
            }
            d.raw_cache->put(key, {image, {{"identity", key}}, reference});
        }
        QJsonArray pixels;
        QJsonArray reference_pixels;
        bool color_space{};
        if (o.value("get").toBool()) {
            if (const auto result = d.raw_cache->get(key)) {
                const auto blended = blend_raw_denoise(result->image, result->reference,
                                                       o.value("amount").toDouble(100));
                for (int i = 0; i < 16; ++i) {
                    pixels.append(reinterpret_cast<const float*>(blended.constBits())[i]);
                    if (!result->reference.isNull())
                        reference_pixels.append(
                            reinterpret_cast<const float*>(result->reference.constBits())[i]);
                }
                color_space = result->image.colorSpace() == working_space();
            }
        }
        return {{"contains", d.raw_cache->contains(key)}, {"pixels", pixels},
                {"reference", reference_pixels},          {"colorSpacePreserved", color_space},
                {"memory", d.raw_cache->memory_bytes()},  {"disk", d.raw_cache->disk_bytes()}};
    }
    if (op == "raw_cache_key") {
        RawOptions options;
        options.denoise = o.value("denoise").toBool();
        options.directml = o.value("directml").toBool(true);
        options.model_path = o.value("model").toString();
        options.exposure = o.value("exposure").toDouble();
        options.high_quality = o.value("highQuality").toBool(true);
        return {{"key", raw_cache_key(o.value("source").toString().toUtf8(), options)}};
    }
    if (op == "raw_window") {
        std::vector<Layer> sources;
        auto paths = o.value("paths").toArray();
        if (paths.isEmpty())
            paths.append(o.value("path"));
        for (const auto& path : paths) {
            Layer layer;
            layer.kind = LayerKind::source;
            layer.name = "RAW validation";
            layer.linked_path = path.toString();
            QFile source(layer.linked_path);
            if (!source.open(QIODevice::ReadOnly))
                throw EditError(source.errorString());
            layer.raw_source = source.readAll();
            // Most validation windows represent embedded RAW sources and must
            // not write sidecars beside shared research fixtures. File workflow
            // tests opt into the production linked-source behavior explicitly.
            if(!o.value("linked").toBool())layer.linked_path.clear();
            layer.raw_recipe = {{"denoise", o.value("denoise").toBool()}, {"directml", true}};
            if (o.contains("recipe"))
                layer.raw_recipe = o.value("recipe").toObject();
            sources.push_back(std::move(layer));
        }
        QSettings().setValue("raw/model", o.value("model").toString());
        d.raw_window =
            std::make_unique<huelith::desktop::RawDialog>(std::move(sources), d.window.get());
        d.raw_window->open();
        return {{"ok", true}};
    }
    if (op == "raw_hsl") {
        auto* dialog = d.raw_window.get();
        if (!dialog)
            throw EditError("RAW window missing");
        auto* tabs = dialog->findChild<QTabBar*>("rawHslTabs");
        if (!tabs)
            throw EditError("RAW HSL tabs missing");
        dialog->findChild<QToolButton*>("rawSection4")->setChecked(true);
        dialog->findChild<QToolButton*>("rawSection0")->setChecked(false);
        if (o.contains("enabled"))
            dialog->findChild<QToolButton*>("rawSectionEnable4")
                ->setChecked(o.value("enabled").toBool());
        if (o.contains("tab")) {
            const QPointF point = tabs->tabRect(o.value("tab").toInt()).center();
            for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                QMouseEvent event(type, point, tabs->mapToGlobal(point.toPoint()), Qt::LeftButton,
                                  type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                                  Qt::NoModifier);
                QApplication::sendEvent(tabs, &event);
            }
        }
        if (o.contains("mode"))
            dialog->findChild<QComboBox*>("rawHslMode")->setCurrentIndex(o.value("mode").toInt());
        if (o.contains("band"))
            dialog->findChild<QTabBar*>("rawHslColors")->setCurrentIndex(o.value("band").toInt());
        if (o.contains("control")) {
            auto* spin = dialog->findChild<QDoubleSpinBox*>(o.value("control").toString());
            if (!spin)
                throw EditError("HSL numeric control missing");
            spin->setValue(o.value("value").toDouble());
        }
        if (o.contains("slider")) {
            auto* slider = dialog->findChild<QSlider*>(o.value("slider").toString());
            if (!slider)
                throw EditError("HSL slider missing");
            if (o.contains("position")) {
                const QPointF position(6 + (slider->width() - 12.) * o.value("position").toDouble(),
                                       7);
                for (const auto type :
                     {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type, position, slider->mapToGlobal(position.toPoint()),
                                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                      type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                         : Qt::LeftButton,
                                      Qt::NoModifier);
                    QApplication::sendEvent(slider, &event);
                }
            }
            if (o.value("reset").toBool()) {
                const QPointF position(slider->width() / 2., 7);
                QMouseEvent event(QEvent::MouseButtonDblClick, position,
                                  slider->mapToGlobal(position.toPoint()), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(slider, &event);
            }
            if (o.value("right").toBool()) {
                QKeyEvent event(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
                QApplication::sendEvent(slider, &event);
            }
        }
        QApplication::processEvents();
        QJsonArray labels, rows;
        for (int i = 0; i < tabs->count(); ++i)
            labels.append(tabs->tabText(i));
        auto* mixer = dialog->findChild<QWidget*>("rawHslMixer");
        for (auto* spin : mixer->findChildren<QDoubleSpinBox*>())
            if (spin->isVisible())
                rows.append(QJsonObject{{"name", spin->objectName()}, {"value", spin->value()}});
        if (o.contains("capture"))
            mixer->grab().save(o.value("capture").toString());
        return {{"tabs", labels},
                {"current", tabs->currentIndex()},
                {"rows", rows},
                {"recipe", dialog->results().front().raw_recipe}};
    }
    if (op == "raw_slider_drag") {
        auto* dialog = d.raw_window.get();
        if (!dialog)
            throw EditError("RAW window missing");
        auto* slider =
            dialog->findChild<QSlider*>(o.value("slider").toString("toneExposureSlider"));
        if (!slider || !slider->isVisible())
            throw EditError("RAW slider not visible");
        QStyleOptionSlider option;
        option.initFrom(slider);
        option.orientation = Qt::Horizontal;
        option.minimum = slider->minimum();
        option.maximum = slider->maximum();
        option.sliderPosition = slider->value();
        option.sliderValue = slider->value();
        const auto handle = slider->style()->subControlRect(QStyle::CC_Slider, &option,
                                                            QStyle::SC_SliderHandle, slider);
        const auto send = [&](QEvent::Type type, QPointF point) {
            QMouseEvent event(type, point, slider->mapToGlobal(point.toPoint()),
                              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(slider, &event);
        };
        send(QEvent::MouseButtonPress, handle.center());
        QJsonArray milliseconds, presented;
        QTimer heartbeat;
        int ticks{};
        QObject::connect(&heartbeat, &QTimer::timeout, dialog, [&ticks] { ++ticks; });
        heartbeat.start(16);
        QPointF point;
        for (int i = 0; i < 50; ++i) {
            point = QPointF(slider->width() * (.35 + .3 * i / 49), handle.center().y());
            QElapsedTimer timer;
            timer.start();
            send(QEvent::MouseMove, point);
            milliseconds.append(timer.nsecsElapsed() / 1e6);
            QEventLoop loop;
            QTimer::singleShot(25, &loop, &QEventLoop::quit);
            loop.exec();
            presented.append(dialog->property("previewPresented").toDouble());
        }
        send(QEvent::MouseButtonRelease, point);
        return {{"eventMs", milliseconds},
                {"heartbeatTicks", ticks},
                {"presented", presented},
                {"sliderValue", slider->value()},
                {"exposure", dialog->findChild<QDoubleSpinBox*>("rawExposure")->value()}};
    }
    if (op == "raw_window_state") {
        auto* dialog = d.raw_window.get();
        if (!dialog)
            throw EditError("RAW window missing");
        if(o.contains("film"))filmstrip_command(dialog,o.value("film").toObject());
        if(o.contains("profileUI"))raw_profile_ui(dialog,o.value("profileUI").toObject());
        if(o.contains("outputAction")){
            auto* action=dialog->findChild<QAction*>(o.value("outputAction").toString());if(!action)throw EditError("RAW output action missing");action->trigger();
        }
        if(o.contains("maskItem")) {
            auto* tree=dialog->findChild<QTreeWidget*>("rawMaskTree");const auto item=o.value("maskItem").toObject();
            const int local=item.value("local").toInt(-1),component=item.value("component").toInt(-1);
            const auto locate=[&]() {auto* root=tree->topLevelItem(local+1);return component<0?root:(root?root->child(component):nullptr);};
            if(!locate())throw EditError("Mask tree item missing");
            tree->setCurrentItem(locate());
            if(item.contains("name"))locate()->setText(0,item.value("name").toString());
            if(item.contains("enabled"))locate()->setCheckState(0,item.value("enabled").toBool()?Qt::Checked:Qt::Unchecked);
            QApplication::processEvents();
        }
        if(o.contains("nav")) {
            const auto options=o.value("nav").toObject();auto* view=dialog->findChild<QGraphicsView*>(options.value("view").toString("rawPreview"));
            const auto xy=options.value("position").toArray();const QPointF point((xy.isEmpty()?.5:xy[0].toDouble())*view->viewport()->width(),(xy.isEmpty()?.5:xy[1].toDouble())*view->viewport()->height());
            const auto modifiers=Qt::KeyboardModifiers(options.value("modifiers").toInt());
            if(options.contains("wheel")){QWheelEvent event(point,view->viewport()->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,options.value("wheel").toInt()),Qt::NoButton,modifiers,Qt::NoScrollPhase,false);QApplication::sendEvent(view->viewport(),&event);}
            if(options.contains("key")){QKeyEvent event(options.value("release").toBool()?QEvent::KeyRelease:QEvent::KeyPress,options.value("key").toInt(),modifiers);QApplication::sendEvent(view,&event);}
            if(options.contains("drag")){const auto points=options.value("drag").toArray();const auto button=options.value("middle").toBool()?Qt::MiddleButton:Qt::LeftButton;
                for(int i=0;i<points.size();++i){const auto point_values=points[i].toArray();const QPointF p(point_values[0].toDouble()*view->viewport()->width(),point_values[1].toDouble()*view->viewport()->height());const auto type=i==0?QEvent::MouseButtonPress:i+1==points.size()?QEvent::MouseButtonRelease:QEvent::MouseMove;QMouseEvent event(type,p,view->viewport()->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:button,type==QEvent::MouseButtonRelease?Qt::NoButton:button,modifiers);QApplication::sendEvent(view->viewport(),&event);}
            }
        }
        if(o.contains("settingsAction")) {
            const auto steps=o.value("settingsSteps").toArray();
            if(!steps.isEmpty())QTimer::singleShot(20,[dialog,steps]{
                auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!modal || modal->objectName()!="rawSettingsDialog")return;
                for(const auto& value:steps){const auto step=value.toObject();
                    if(step.contains("tab"))modal->findChild<QTabBar*>("rawSettingsTabs")->setCurrentIndex(step.value("tab").toInt());
                    if(step.contains("index"))modal->findChild<QListWidget*>("rawSettingsList")->setCurrentRow(step.value("index").toInt());
                    if(step.contains("name"))modal->findChild<QLineEdit*>("rawSettingsName")->setText(step.value("name").toString());
                    if(step.contains("groups")){const auto groups=step.value("groups").toArray();for(int i=0;i<14;++i)modal->findChild<QCheckBox*>(QString("rawPresetGroup%1").arg(i))->setChecked(groups.contains(i));}
                    if(step.contains("click")){auto* button=modal->findChild<QAbstractButton*>(step.value("click").toString());if(button)button->click();}
                    if(step.contains("capture"))modal->grab().save(step.value("capture").toString());
                }
                QJsonArray names;auto* list=modal->findChild<QListWidget*>("rawSettingsList");for(int i=0;i<list->count();++i)names.append(list->item(i)->text());
                dialog->setProperty("settingsProbe",QJsonObject{{"names",names},{"status",modal->findChild<QLabel*>("rawSettingsStatus")->text()}});
                if(modal->isVisible())modal->reject();
            });
            auto* action=dialog->findChild<QAction*>(o.value("settingsAction").toString());if(!action)throw EditError("RAW settings action missing");action->trigger();
        }
        if(o.contains("preferences")) {
            const auto values=o.value("preferences").toObject();
            QTimer::singleShot(20,[values]{auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!modal || modal->objectName()!="navigationPreferences")return;
                for(auto it=values.begin();it!=values.end();++it){if(auto* spin=modal->findChild<QSpinBox*>(it.key()))spin->setValue(it.value().toInt());else if(auto* combo=modal->findChild<QComboBox*>(it.key()))combo->setCurrentIndex(it.value().toInt());else if(auto* check=modal->findChild<QCheckBox*>(it.key()))check->setChecked(it.value().toBool());}
                if(values.value("clear").toBool())modal->findChild<QPushButton*>("rawClearCachePreference")->click();
                if(values.contains("capture"))modal->grab().save(values.value("capture").toString());
                if(values.value("accept").toBool())modal->accept();else modal->reject();
            });
            dialog->findChild<QPushButton*>("rawPreferences")->click();
        }
        if(o.contains("curveGesture")) {
            auto* curve=dialog->findChild<QWidget*>("toneCurve");curve->setFocus();const QRectF area=QRectF(curve->rect()).adjusted(12,12,-12,-25);
            const auto gesture=o.value("curveGesture").toObject();const auto from=gesture.value("from").toArray(),to=gesture.value("to").toArray();
            const bool split=gesture.value("split").toBool();
            const auto map=[&](const QJsonArray& v){return QPointF(area.left()+v[0].toDouble()*area.width(),split?area.bottom()+14:area.bottom()-v[1].toDouble()*area.height());};
            const QPointF a=map(from),b=map(to);const auto button=gesture.value("right").toBool()?Qt::RightButton:Qt::LeftButton;
            QMouseEvent press(QEvent::MouseButtonPress,a,curve->mapToGlobal(a),button,button,Qt::NoModifier);QApplication::sendEvent(curve,&press);
            QMouseEvent move(QEvent::MouseMove,b,curve->mapToGlobal(b),Qt::NoButton,button,Qt::NoModifier);QApplication::sendEvent(curve,&move);
            if(gesture.value("holdMs").toInt()>0){QElapsedTimer hold;hold.start();while(hold.elapsed()<gesture.value("holdMs").toInt()){QApplication::processEvents();QThread::msleep(5);}}
            if(gesture.value("escape").toBool()){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(curve,&escape);}
            QMouseEvent release(QEvent::MouseButtonRelease,b,curve->mapToGlobal(b),button,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(curve,&release);
        }
        if(o.contains("curveKey")){auto* curve=dialog->findChild<QWidget*>("toneCurve");QKeyEvent key(QEvent::KeyPress,o.value("curveKey").toInt(),Qt::KeyboardModifiers(o.value("modifiers").toInt()));QApplication::sendEvent(curve,&key);}
        if (o.contains("hover")) {
            auto* view = dialog->findChild<QGraphicsView*>("rawPreview");
            const auto point = o.value("hover").toArray();
            const auto size = view->sceneRect().size();
            const QPointF p = view->mapFromScene(QPointF(point[0].toDouble() * size.width(), point[1].toDouble() * size.height()));
            QMouseEvent event(QEvent::MouseMove, p, view->viewport()->mapToGlobal(p.toPoint()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &event);
        }
        if (o.value("labReadoutToggle").toBool()) {
            QTimer::singleShot(0, [] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (menu && !menu->actions().isEmpty()) {
                    menu->actions().front()->trigger();
                    menu->close();
                }
            });
            auto* histogram = dialog->findChild<QLabel*>("rawHistogram");
            QMetaObject::invokeMethod(histogram, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, QPoint(5, 5)));
        }
        if (o.contains("closeTab")) {
            auto* tabs = dialog->findChild<QTabBar*>(o.value("closeTab").toString());
            if (!tabs)
                throw EditError("RAW tab bar missing");
            const int index = o.value("tabIndex").toInt();
            auto* close = tabs->tabButton(index, QTabBar::RightSide);
            if (!close)
                close = tabs->tabButton(index, QTabBar::LeftSide);
            if (!close)
                throw EditError("RAW tab close button missing");
            const QPointF point = close->rect().center();
            for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                QMouseEvent event(type, point, close->mapToGlobal(point.toPoint()), Qt::LeftButton,
                                  type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                                  Qt::NoModifier);
                QApplication::sendEvent(close, &event);
            }
        }
        if (o.contains("tab")) {
            auto* tab = dialog->findChild<QTabBar*>(o.value("tab").toString());
            if (tab)
                tab->setCurrentIndex(o.value("tabIndex").toInt());
            else if (auto* tabs = dialog->findChild<QTabWidget*>(o.value("tab").toString()))
                tabs->setCurrentIndex(o.value("tabIndex").toInt());
            else
                throw EditError("RAW tab missing");
        }
        if (o.value("escape").toBool()) {
            QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(dialog, &event);
        }
        if (o.contains("widget")) {
            auto* widget = dialog->findChild<QWidget*>(o.value("widget").toString());
            if (!widget)
                throw EditError("RAW widget missing");
            for (auto* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
                if (auto* scroll = qobject_cast<QScrollArea*>(parent))
                    scroll->ensureWidgetVisible(widget);
            if (o.contains("position")) {
                const auto xy = o.value("position").toArray();
                const QPointF point(xy[0].toDouble() * widget->width(),
                                    xy[1].toDouble() * widget->height());
                if (o.value("mousePhase").toString() == "move") {
                    QMouseEvent event(QEvent::MouseMove, point,
                        widget->mapToGlobal(point.toPoint()), Qt::NoButton, Qt::LeftButton,
                        Qt::KeyboardModifiers(o.value("modifiers").toInt()));
                    QApplication::sendEvent(widget, &event);
                }
                for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                    if (o.value("mousePhase").toString() == "move")
                        continue;
                    if (o.value("mousePhase").toString() == "press" &&
                        type != QEvent::MouseButtonPress)
                        continue;
                    if (o.value("mousePhase").toString() == "release" &&
                        type != QEvent::MouseButtonRelease)
                        continue;
                    QMouseEvent event(
                        type, point, widget->mapToGlobal(point.toPoint()), Qt::LeftButton,
                        type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                        Qt::KeyboardModifiers(o.value("modifiers").toInt()));
                    QApplication::sendEvent(widget, &event);
                }
            }
            if (o.contains("key")) {
                QKeyEvent event(QEvent::KeyPress, o.value("key").toInt(),
                    Qt::KeyboardModifiers(o.value("modifiers").toInt()));
                QApplication::sendEvent(widget, &event);
            }
        }
        if (o.contains("index"))
            dialog->findChild<QListWidget*>("rawFiles")->setCurrentRow(o.value("index").toInt());
        if (o.contains("included"))
            dialog->findChild<QListWidget*>("rawFiles")
                ->currentItem()
                ->setCheckState(o.value("included").toBool() ? Qt::Checked : Qt::Unchecked);
        if (o.contains("model"))
            dialog->findChild<QLineEdit*>("rawModel")->setText(o.value("model").toString());
        if (o.contains("combo")) {
            auto* combo = dialog->findChild<QComboBox*>(o.value("combo").toString());
            if (!combo)
                throw EditError("RAW combo missing");
            const int index = o.contains("text") ? combo->findText(o.value("text").toString())
                                                 : o.value("comboIndex").toInt();
            if (index < 0 || index >= combo->count())
                throw EditError("RAW combo value missing");
            combo->setCurrentIndex(index);
        }
        if (o.contains("denoise"))
            dialog->findChild<QCheckBox*>("rawDenoise")->setChecked(o.value("denoise").toBool());
        if (o.contains("mode"))
            dialog->findChild<QComboBox*>("rawEditMode")->setCurrentIndex(o.value("mode").toInt());
        if (o.contains("target"))
            dialog->findChild<QComboBox*>("rawAdjustmentTarget")
                ->setCurrentIndex(o.value("target").toInt());
        if (o.value("click").toString() == "rawSynchronize") {
            const auto options = o.value("sync").toObject();
            QTimer::singleShot(0, [options] {
                auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!modal || modal->objectName() != "rawSyncDialog")
                    return;
                if (options.contains("groups")) {
                    const auto groups = options.value("groups").toArray();
                    for (int i = 0; i < 14; ++i)
                        modal->findChild<QCheckBox*>(QString("rawSync%1").arg(i))
                            ->setChecked(groups.contains(i));
                }
                if (options.contains("capture"))
                    modal->grab().save(options.value("capture").toString());
                auto* footer = modal->findChild<QDialogButtonBox*>();
                footer
                    ->button(options.value("cancel").toBool() ? QDialogButtonBox::Cancel
                                                              : QDialogButtonBox::Ok)
                    ->click();
            });
        }
        if (o.contains("refinement")) {
            const auto settings = o.value("refinement").toObject();
            auto* timer = new QTimer(dialog);
            timer->setInterval(20);
            auto phase = std::make_shared<int>(0);
            auto ticks = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, dialog, [timer, settings, phase, ticks] {
                auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!modal || modal->objectName() != "selectionRefineDialog") return;
                if (++*ticks > 3000 || modal->property("refineError").toBool()) {
                    modal->reject();timer->stop();timer->deleteLater();return;
                }
                if (*phase == 0) {
                    *phase = 1;
                    for (auto it = settings.begin(); it != settings.end(); ++it)
                        if (auto* number = modal->findChild<QDoubleSpinBox*>(it.key()))
                            number->setValue(it.value().toDouble());
                }
                if (!modal->property("refineReady").toBool()) return;
                if (settings.value("accept").toBool()) modal->accept();
                else modal->reject();
                timer->stop();timer->deleteLater();
            });
            timer->start();
        }
        if (o.contains("semantic")) {
            const auto settings = o.value("semantic").toObject();
            auto* timer = new QTimer(dialog);
            timer->setInterval(20);
            auto ticks = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, dialog, [timer, settings, ticks] {
                auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!modal || modal->objectName() != "semanticDialog")
                    return;
                if (settings.contains("target"))
                    modal->findChild<QComboBox*>("semanticTarget")->setCurrentIndex(settings.value("target").toInt());
                if (++*ticks < 1500 && !settings.value("immediateCancel").toBool() &&
                    !modal->property("semanticReady").toBool() &&
                    !modal->property("semanticError").toBool())
                    return;
                if (settings.contains("target"))
                    modal->findChild<QComboBox*>("semanticTarget")
                        ->setCurrentIndex(settings.value("target").toInt());
                if (settings.contains("person"))
                    modal->findChild<QComboBox*>("semanticPerson")
                        ->setCurrentIndex(settings.value("person").toInt());
                if (settings.contains("capture"))
                    modal->grab().save(settings.value("capture").toString());
                if (settings.value("accept").toBool() && *ticks < 1500 &&
                    modal->property("selectedPixels").toLongLong() > 0)
                    modal->accept();
                else
                    modal->reject();
                timer->stop();
                timer->deleteLater();
            });
            timer->start();
        }
        if (o.contains("click")) {
            auto* button = dialog->findChild<QAbstractButton*>(o.value("click").toString());
            if (!button)
                throw EditError("RAW control missing");
            button->click();
        }
        if (o.contains("section"))
            dialog->findChild<QToolButton*>(QString("rawSection%1").arg(o.value("section").toInt()))
                ->setChecked(true);
        if (o.contains("dehaze"))
            dialog->findChild<QDoubleSpinBox*>("toneDehaze")
                ->setValue(o.value("dehaze").toDouble());
        if (o.contains("control")) {
            auto* control = dialog->findChild<QDoubleSpinBox*>(o.value("control").toString());
            if (!control)
                throw EditError("RAW numeric control missing");
            control->setValue(o.value("value").toDouble());
        }
        if (o.contains("modelChoice"))
            dialog->findChild<QComboBox*>("rawModelChoice")
                ->setCurrentIndex(o.value("modelChoice").toInt());
        if (o.contains("exposure"))
            dialog->findChild<QDoubleSpinBox*>("rawExposure")
                ->setValue(o.value("exposure").toDouble());
        if (o.contains("stroke")) {
            auto* view = dialog->findChild<QGraphicsView*>("rawPreview");
            const auto points = o.value("stroke").toArray();
            for (int i = 0; i < points.size(); ++i) {
                const auto point = points[i].toArray();
                const auto size = view->sceneRect().size();
                const QPointF position = view->mapFromScene(QPointF(
                    point[0].toDouble() * size.width(), point[1].toDouble() * size.height()));
                const auto type = i == 0                   ? QEvent::MouseButtonPress
                                  : i + 1 == points.size() ? QEvent::MouseButtonRelease
                                                           : QEvent::MouseMove;
                QMouseEvent event(type, position, view->viewport()->mapToGlobal(position.toPoint()),
                                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                  type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                     : Qt::LeftButton,
                                  Qt::KeyboardModifiers(o.value("modifiers").toInt()));
                QApplication::sendEvent(view->viewport(), &event);
                if(i==1 && o.value("strokeHoldMs").toInt()>0) {QElapsedTimer hold;hold.start();while(hold.elapsed()<o.value("strokeHoldMs").toInt()){QApplication::processEvents();QThread::msleep(5);}}
                if(i==1 && o.value("strokeEscape").toBool()){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(dialog,&escape);}
            }
        }
        if (o.contains("capture"))
            dialog->grab().save(o.value("capture").toString());
        if (o.value("accept").toBool()) {
            dialog->accept();
            if (!o.value("asyncAccept").toBool()) {
                QElapsedTimer wait;
                wait.start();
                while (dialog->isVisible() &&
                       dialog->findChild<QPushButton*>("rawCancelCompute")->isEnabled() &&
                       wait.elapsed() < 120000) {
                    QApplication::processEvents();
                    QThread::msleep(10);
                }
            }
        }
        if (o.value("reject").toBool())
            dialog->reject();
        if (o.contains("save")) {
            if (dialog->result() != QDialog::Accepted)
                throw EditError("RAW not accepted");
            State state;
            state.name = "RAW persistence validation";
            state.size = dialog->results().front().pixels.size();
            state.layers = dialog->results();
            save_project({state, 1}, o.value("save").toString());
            const auto reopened = load_project(o.value("save").toString());
            for (std::size_t i = 0; i < state.layers.size(); ++i)
                if (reopened.layers[i].raw_source != state.layers[i].raw_source ||
                    reopened.layers[i].raw_recipe != state.layers[i].raw_recipe ||
                    reopened.layers[i].pixels != state.layers[i].pixels)
                    throw EditError("RAW persistence mismatch");
        }
        QJsonArray navigation;
        for(const auto* name:{"rawPreview","rawBeforePreview"}){
            auto* view=dialog->findChild<QGraphicsView*>(QLatin1String(name));if(!view)continue;
            const auto center=view->mapToScene(view->viewport()->rect().center());
            QJsonObject data{{"name",name},{"scale",std::hypot(view->transform().m11(),view->transform().m12())},{"rotation",std::atan2(view->transform().m12(),view->transform().m11())*180/3.141592653589793},{"center",QJsonArray{center.x(),center.y()}},{"visible",view->isVisible()},{"viewport",QJsonArray{view->viewport()->width(),view->viewport()->height()}}};
            if(o.contains("navPoint")){const auto xy=o.value("navPoint").toArray();const auto p=view->mapToScene(QPoint(int(xy[0].toDouble()*view->viewport()->width()),int(xy[1].toDouble()*view->viewport()->height())));data.insert("point",QJsonArray{p.x(),p.y()});}
            if(o.contains("previewOutput"))for(auto* item:view->scene()->items())if(auto* pixels=qgraphicsitem_cast<QGraphicsPixmapItem*>(item)){pixels->pixmap().save(o.value("previewOutput").toString()+QString::fromLatin1(name)+".png");break;}
            navigation.append(data);
        }
        QJsonArray recipes;
        for (const auto& source : dialog->results())
            recipes.append(source.raw_recipe);
        QJsonArray controls;
        if (o.value("inventory").toBool())
            for (auto* widget : dialog->findChildren<QWidget*>()) {
                if (widget->objectName().isEmpty() || widget->objectName().startsWith("qt_"))
                    continue;
                QJsonObject item{{"name", widget->objectName()},
                                 {"type", widget->metaObject()->className()},
                                 {"visible", widget->isVisible()},
                                 {"enabled", widget->isEnabled()},
                                 {"height", widget->height()},
                                 {"minimumHeight", widget->minimumSizeHint().height()}};
                if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
                    item.insert("text", button->text());
                    item.insert("checked", button->isChecked());
                    item.insert("tooltip", button->toolTip());
                }
                if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                    item.insert("currentText", combo->currentText());
                    item.insert("currentIndex", combo->currentIndex());
                    QJsonArray items;
                    for (int i = 0; i < combo->count(); ++i)
                        items.append(combo->itemText(i));
                    item.insert("items", items);
                }
                if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
                    item.insert("minimum", spin->minimum());
                    item.insert("maximum", spin->maximum());
                    item.insert("value", spin->value());
                }
                controls.append(item);
            }
        qint64 red_pixels{}, blue_pixels{};
        if (o.value("inspectPixels").toBool()) {
            auto* view = dialog->findChild<QGraphicsView*>("rawPreview");
            for (auto* item : view->scene()->items())
                if (auto* pixmap = dynamic_cast<QGraphicsPixmapItem*>(item)) {
                    const auto image = pixmap->pixmap().toImage();
                    for (int y = 0; y < image.height(); ++y)
                        for (int x = 0; x < image.width(); ++x) {
                            const auto c = image.pixelColor(x, y);
                            if (c.red() == 255 && c.green() == 0 && c.blue() == 0) ++red_pixels;
                            if (c.red() == 0 && c.green() == 0 && c.blue() == 255) ++blue_pixels;
                        }
                }
        }
        return {{"previewFullResolution", dialog->property("previewFullResolution").toBool()},
                {"colorReadout", dialog->findChild<QLabel*>("rawColorReadout")->text()},
                {"highlightClippedPixels", dialog->property("highlightClippedPixels").toLongLong()},
                {"shadowClippedPixels", dialog->property("shadowClippedPixels").toLongLong()},
                {"displayedRedPixels", red_pixels}, {"displayedBluePixels", blue_pixels},
                {"detailPreview", dialog->property("detailPreview").toInt()},
                {"controls", controls},
                {"mode", dialog->findChild<QComboBox*>("rawEditMode")->currentIndex()},
                {"previewEdge",dialog->property("previewEdge").toInt()},
                {"cacheMemoryBytes",dialog->property("cacheMemoryBytes").toLongLong()},
                {"cacheDiskBytes",dialog->property("cacheDiskBytes").toLongLong()},
                {"previewRequested", dialog->property("previewRequested").toDouble()},
                {"navigation",navigation},
                {"settingsProbe", QJsonValue::fromVariant(dialog->property("settingsProbe"))},
                {"film",filmstrip_command(dialog,{})},
                {"profiles",raw_profile_ui(dialog,{})},
                {"previewPresented", dialog->property("previewPresented").toDouble()},
                {"modelChoice", dialog->findChild<QComboBox*>("rawModelChoice")->currentIndex()},
                {"modelPath", dialog->findChild<QLineEdit*>("rawModel")->text()},
                {"ready", dialog->findChild<QPushButton*>("rawApply")->isEnabled()},
                {"target", dialog->findChild<QComboBox*>("rawAdjustmentTarget")->currentIndex()},
                {"idle", !dialog->findChild<QPushButton*>("rawCancelCompute")->isEnabled()},
                {"denoiseButton", dialog->findChild<QPushButton*>("rawCompute")->text()},
                {"denoiseSupported", dialog->property("rawDenoiseSupported").toBool()},
                {"dngSupported", dialog->property("rawDngSupported").toBool()},
                {"denoiseToggleEnabled", dialog->findChild<QCheckBox*>("rawDenoise")->isEnabled()},
                {"capabilityNote", dialog->findChild<QLabel*>("rawCapabilityNote")->text()},
                {"dngEnabled", dialog->findChild<QPushButton*>("rawSaveDng")->isEnabled()},
                {"denoiseEnabled", dialog->findChild<QPushButton*>("rawCompute")->isEnabled()},
                {"denoiseInSection", dialog->findChild<QWidget*>("rawSectionBody7")->isAncestorOf(dialog->findChild<QPushButton*>("rawCompute"))},
                {"progressInSection", dialog->findChild<QWidget*>("rawSectionBody7")->isAncestorOf(dialog->findChild<QWidget*>("rawProgress"))},
                {"progressVisible", dialog->findChild<QWidget*>("rawProgress")->isVisible()},
                {"scale", std::hypot(dialog->findChild<QGraphicsView*>("rawPreview")->transform().m11(),dialog->findChild<QGraphicsView*>("rawPreview")->transform().m12())},
                {"histogramSamples",
                 dialog->findChild<QLabel*>("rawHistogram")->property("sampleCount").toInt()},
                {"recipes", recipes},
                {"count", int(dialog->results().size())},{"outputWidth",dialog->results().front().pixels.width()},{"outputHeight",dialog->results().front().pixels.height()},
                {"visible", dialog->isVisible()},
                {"result", dialog->result()},
                {"status", dialog->findChild<QLabel*>("rawStatus")->text()},
                {"recipe", dialog->results().front().raw_recipe}};
    }
    if (op == "raw_capabilities") {
        QFile file(o["path"].toString());if(!file.open(QIODevice::ReadOnly))throw EditError("RAW source missing");
        auto result=raw_capabilities(file.readAll());result.insert("recognized",is_raw_file(o["path"].toString()));return result;
    }
    if (op == "raw_develop") {
        RawOptions options;
        options.denoise = o.value("denoise").toBool();
        options.directml = o.value("directml").toBool(true);
        options.model_path = o.value("model").toString();
        options.exposure = o.value("exposure").toDouble();
        options.high_quality = o.value("highQuality").toBool(true);
        std::atomic_bool cancelled{o.value("cancelled").toBool()};
        QJsonArray progress;
        QElapsedTimer timer;
        timer.start();
        auto result = develop_raw(
            o.value("path").toString(), options, cancelled, [&](int completed, int total) {
                progress.append(QJsonArray{completed, total});
                if (o.contains("cancelAfter") && completed >= o.value("cancelAfter").toInt())
                    cancelled.store(true);
            });
        if (o.contains("referenceOutput")) {
            QFile file(o.value("referenceOutput").toString());
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(reinterpret_cast<const char*>(result.reference.constBits()),
                           result.reference.sizeInBytes()) != result.reference.sizeInBytes())
                throw EditError("Cannot write reference evidence");
        }
        result.image =
            blend_raw_denoise(result.image, result.reference, o.value("amount").toDouble(100));
        if (o.contains("floatOutput")) {
            QFile file(o.value("floatOutput").toString());
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(reinterpret_cast<const char*>(result.image.constBits()),
                           result.image.sizeInBytes()) != result.image.sizeInBytes())
                throw EditError("Cannot write float evidence");
        }
        if (o.contains("preview"))
            if (!display_image(result.image).save(o.value("preview").toString()))
                throw EditError("Cannot write RAW preview evidence");
        return {{"metadata", result.metadata},
                {"progress", progress},
                {"width", result.image.width()},
                {"height", result.image.height()},
                {"seconds", timer.elapsed() / 1000.}};
    }
    if (op == "init") {
        qputenv("HUELITH_AI_WORKER_EXE", QByteArray(HUELITH_TEST_WORKER_PATH));
        qputenv("HUELITH_EXIFTOOL_PATH",QFileInfo(QString::fromUtf8(HUELITH_TEST_WORKER_PATH)).absolutePath().toUtf8()+"/tools/exiftool/exiftool.exe");
        qputenv("HUELITH_AI_MODEL_DIR", QFileInfo(QString::fromUtf8(HUELITH_TEST_WORKER_PATH)).absolutePath().toUtf8() + "/models");
        if (!qEnvironmentVariable("PATH").contains(QLibraryInfo::path(QLibraryInfo::BinariesPath)))
            qputenv("PATH", QLibraryInfo::path(QLibraryInfo::BinariesPath).toLocal8Bit() + ";" + qgetenv("PATH"));
        if (qEnvironmentVariableIsEmpty("HUELITH_LENSFUN_DATABASE"))
            qputenv("HUELITH_LENSFUN_DATABASE",
                    QDir::current().absoluteFilePath("build/windows-release/lensfun").toUtf8());
        if (!d.application) {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
            QCoreApplication::setOrganizationName("HuelithValidation");
            QCoreApplication::setApplicationName("PythonM1");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                               o.value("settings").toString());
            huelith::desktop::configure_interface_scale();
            d.application = std::make_unique<QApplication>(d.argc, d.argv);
            if (!o.value("settings").toString().isEmpty())
                QSettings().setValue("storage/recoveryDirectory",
                                     o.value("settings").toString() + "/recovery");
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
        // These windows also have a Qt parent; release their unique owners before replacing it.
        d.raw_window.reset();
        d.semantic_window.reset();
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
            layer.background = item.value("backgroundLayer").toBool();
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
        else if (auto* button = qobject_cast<QToolButton*>(widget))
            button->click();
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
    if (op == "window_recovery_state") {
        auto* c = d.window->active_canvas();
        auto* timer = d.window->findChild<QTimer*>("autoRecoveryTimer");
        if (o.contains("path"))
            c->recovery_file = o.value("path").toString();
        if (o.contains("recent")) {
            QStringList files;
            for (const auto& value : o.value("recent").toArray())
                files.append(value.toString());
            QSettings().setValue("recentProjects", files);
        }
        QElapsedTimer elapsed;
        elapsed.start();
        if (o.value("trigger").toBool())
            QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        const double milliseconds = elapsed.nsecsElapsed() / 1e6;
        QJsonArray recent;
        for (const auto* action : d.window->findChild<QMenu*>("recentFilesMenu")->actions())
            recent.append(action->text());
        QJsonObject result{{"busy", d.window->property("recoveryBusy").toBool()},
                           {"enabled", timer->isActive()},
                           {"interval", timer->interval()},
                           {"triggerMs", milliseconds},
                           {"path", c->recovery_file},
                           {"exists", QFileInfo::exists(c->recovery_file)},
                           {"dirty", c->project().dirty()},
                           {"revision", qint64(c->project().revision())},
                           {"recoveredRevision", c->property("recoveredRevision").toLongLong()},
                           {"recent", recent},
                           {"status", d.window->statusBar()->currentMessage()}};
        if (o.value("inspect").toBool()) {
            const auto state = load_project(c->recovery_file);
            result.insert("savedExposure", state.layers.front().exposure);
        }
        return result;
    }
    if (op == "window_recovery_file")
        return {{"path", d.window->active_canvas()->recovery_file}};
    if (op == "window_workspace") {
        if(o.contains("activateDocument")) {
            auto* canvas=d.window->canvas_at(o.value("activateDocument").toInt());
            if(!canvas)throw EditError("document missing");
            QEvent activate(QEvent::WindowActivate);QApplication::sendEvent(canvas->window(),&activate);
        }
        if(o.contains("closeFloating")) {
            auto* canvas=d.window->active_canvas();
            if(canvas && canvas->window()!=d.window.get())canvas->window()->close();
        }
        if (o.contains("dock")) {
            auto* dock = d.window->findChild<QDockWidget*>(o.value("dock").toString());
            if (!dock) throw EditError("Workspace dock missing");
            if (o.contains("visible")) dock->setVisible(o.value("visible").toBool());
        }
        if (o.value("close").toBool()) d.window->close();
        QApplication::processEvents();
        auto* tabs = d.window->findChild<QTabWidget*>("documents");
        auto* menu = d.window->findChild<QMenuBar*>();
        QJsonObject docks;
        for (auto* dock : d.window->findChildren<QDockWidget*>())
            docks.insert(dock->objectName(), QJsonObject{{"visible", dock->isVisible()},
                {"movable", dock->features().testFlag(QDockWidget::DockWidgetMovable)},
                {"floatable", dock->features().testFlag(QDockWidget::DockWidgetFloatable)}});
        const QSettings settings;
        return {{"documentCount",d.window->document_count()},
                {"floating",d.window->active_canvas() && d.window->active_canvas()->window()!=d.window.get()},
                {"dpr",d.window->devicePixelRatioF()},
                {"scalePreference",huelith::desktop::read_canvas_preferences().interface_scale},
                {"docks", docks}, {"movableTabs", tabs->isMovable()},
                {"closeTabs", tabs->tabsClosable()}, {"fontPixels", menu->font().pixelSize()},
                {"fontHeight", menu->fontMetrics().height()},
                {"tabHeight", tabs->count() ? tabs->tabBar()->tabRect(0).height() : 0},
                {"saved", QJsonArray::fromStringList(settings.value("workspace/savedLayouts").toMap().keys())},
                {"current", settings.value("workspace/currentName").toString()},
                {"visible", d.window->isVisible()}};
    }
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
                QJsonArray effects;
                for (const auto& effect : layer.effects)
                    effects.append(QJsonObject{{"id", effect.id.toString()},
                                               {"name", effect.name},
                                               {"enabled", effect.enabled},
                                               {"amount", effect.amount},
                                               {"tone", tone_json(effect.tone)},
                                               {"mask", !effect.mask.isNull()},
                                               {"lut", bool(effect.lut)}});
                layers.append(QJsonObject{{"name", layer.name},
                                          {"effects", effects},
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
    if (op == "window_open_batch") {
        QStringList paths;
        for (const auto& path : o.value("paths").toArray())
            paths.append(path.toString());
        QTimer timer;
        timer.setInterval(40);
        QElapsedTimer elapsed;
        elapsed.start();
        bool started{};
        bool timed_out{};
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto* dialog =
                dynamic_cast<huelith::desktop::RawDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (o.value("cancel").toBool() || elapsed.elapsed() > 120000) {
                timed_out = elapsed.elapsed() > 120000;
                dialog->reject();
            } else if (dialog->findChild<QPushButton*>("rawApply")->isEnabled()) {
                dialog->accept();
            } else if (!started && !dialog->findChild<QPushButton*>("rawCancelCompute")->isEnabled()) {
                started = true;
                dialog->findChild<QPushButton*>("rawComputeAll")->click();
            }
        });
        timer.start();
        d.window->open_documents(paths);
        timer.stop();
        return {{"documents", d.window->document_count()}, {"timedOut", timed_out}};
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
        if (o.value("whiteBackground").toBool())
            spec.background = huelith::CanvasBackground::white;
        const bool success = d.window->create_document(spec);
        QApplication::processEvents();
        return {{"ok", success}, {"count", d.window->document_count()}};
    }
    if (op == "pump") {
        if (o.value("waitMs").toInt() > 0) {
            QEventLoop loop;
            QTimer::singleShot(std::clamp(o.value("waitMs").toInt(), 1, 5000), &loop,
                               &QEventLoop::quit);
            loop.exec();
        } else {
            QApplication::processEvents();
        }
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
    if (op == "window_tone_validation") {
        QTimer timer;
        timer.setInterval(20);
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 last = 0, maximum_gap = 0;
        int edits = 0, ticks = 0;
        bool presented = false, timed_out = false;
        QObject::connect(&timer, &QTimer::timeout, d.window.get(), [&] {
            const auto now = elapsed.elapsed();
            maximum_gap = std::max(maximum_gap, now - last);
            last = now;
            ++ticks;
            auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!modal || modal->objectName() != "toneDialog")
                return;
            if (elapsed.elapsed() > 30000) {
                timed_out = true;
                timer.stop();
                modal->reject();
                return;
            }
            if (edits < 12) {
                modal->findChild<QDoubleSpinBox*>("toneExposure")->setValue(++edits / 10.);
                modal->setProperty("previewPresented", 0);
                return;
            }
            if (modal->property("previewPresented").toInt() == 0)
                return;
            presented = true;
            if (o.contains("capture"))
                modal->grab().save(o.value("capture").toString());
            timer.stop();
            if (o.value("accept").toBool())
                modal->accept();
            else
                modal->reject();
        });
        timer.start();
        bool found = false;
        for (auto* action : d.window->findChildren<QAction*>())
            if (action->text() == QStringLiteral("曝光与光线…")) {
                found = true;
                action->trigger();
                break;
            }
        timer.stop();
        return {{"found", found}, {"presented", presented}, {"timedOut", timed_out},
                {"edits", edits}, {"ticks", ticks},         {"maximumGapMs", maximum_gap}};
    }
    if (op == "window_tone_tools") {
        QTimer timer;
        timer.setInterval(30);
        int stage = 0, ticks = 0, samples = 0, auxiliary = 0;
        bool escaped = false, timed_out = false;
        const bool detail = o.value("detail").toBool();
        QObject::connect(&timer, &QTimer::timeout, d.window.get(), [&] {
            auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!modal || modal->objectName() != "toneDialog")
                return;
            if (++ticks > 1000) {
                timed_out = true;
                modal->reject();
                timer.stop();
                return;
            }
            if (modal->property("previewPresented").toInt() == 0 ||
                modal->property("previewBusy").toBool())
                return;
            if (stage == 0) {
                if (detail) {
                    modal->findChild<QListWidget*>("toneCategories")->setCurrentRow(7);
                    modal->findChild<QDoubleSpinBox*>("toneSharpening")->setValue(70);
                    modal->findChild<QDoubleSpinBox*>("toneSharpenMasking")->setValue(50);
                } else {
                    modal->findChild<QTabBar*>("rawMixerTabs")->setCurrentIndex(1);
                    modal->findChild<QToolButton*>("rawPointPicker")->click();
                }
                ++stage;
                return;
            }
            if (stage == 1) {
                auto* widget =
                    detail ? static_cast<QWidget*>(
                                 modal->findChild<QSlider*>("toneSharpenMaskingSlider"))
                           : static_cast<QWidget*>(modal->findChild<QLabel*>("tonePreview"));
                const QPointF point = widget->rect().center();
                for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                    if (detail && type == QEvent::MouseButtonRelease)
                        break;
                    QMouseEvent event(
                        type, point, widget->mapToGlobal(point.toPoint()), Qt::LeftButton,
                        type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                        detail ? Qt::AltModifier : Qt::NoModifier);
                    QApplication::sendEvent(widget, &event);
                }
                ++stage;
                return;
            }
            if (stage == 2) {
                if (detail)
                    auxiliary = modal->property("detailPreview").toInt();
                else {
                    samples = modal->findChild<QTabBar*>("rawPointSwatches")->count();
                    modal->findChild<QCheckBox*>("rawPointVisualize")->setChecked(true);
                    modal->findChild<QToolButton*>("rawPointPicker")->setChecked(true);
                    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QApplication::sendEvent(modal, &event);
                    escaped = modal->isVisible() &&
                              !modal->findChild<QToolButton*>("rawPointPicker")->isChecked();
                }
                ++stage;
                return;
            }
            if (stage == 3) {
                if (o.contains("capture"))
                    modal->grab().save(o.value("capture").toString());
                if (detail) {
                    auto* slider = modal->findChild<QSlider*>("toneSharpenMaskingSlider");
                    QPointF point = slider->rect().center();
                    QMouseEvent event(QEvent::MouseButtonRelease, point,
                                      slider->mapToGlobal(point.toPoint()), Qt::LeftButton,
                                      Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(slider, &event);
                }
                ++stage;
                return;
            }
            timer.stop();
            if (o.value("accept").toBool())
                modal->accept();
            else
                modal->reject();
        });
        timer.start();
        for (auto* action : d.window->findChildren<QAction*>())
            if (action->text() == QStringLiteral("色相/饱和度…")) {
                action->trigger();
                break;
            }
        timer.stop();
        const auto* c = d.window->active_canvas();
        const auto* selected = find_layer(c->project().state().layers, c->active_layer);
        return {{"stage", stage},
                {"timedOut", timed_out},
                {"samples", samples},
                {"escaped", escaped},
                {"auxiliary", auxiliary},
                {"tone", selected ? tone_json(selected->tone) : QJsonObject{}}};
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
                    if (modal->objectName() == "semanticDialog" &&
                        !modal->property("semanticReady").toBool() &&
                        !modal->property("semanticError").toBool())
                        return;
                    if (auto* file = qobject_cast<QFileDialog*>(modal)) {
                        if (settings.contains("steps"))
                            return;
                        file->selectFile(settings.value("path").toString());
                        static_cast<QDialog*>(file)->accept();
                        timer->stop();
                        timer->deleteLater();
                        return;
                    }
                    if (*phase == 0) {
                        *phase = 1; // Nested file dialogs must not re-enter this command sequence.
                        for (const auto& entry : settings.value("steps").toArray()) {
                            const auto step = entry.toObject();
                            if (step.contains("file")) {
                                QTimer::singleShot(0, [step] {
                                    auto* file = qobject_cast<QFileDialog*>(
                                        QApplication::activeModalWidget());
                                    if (!file)
                                        return;
                                    file->selectFile(step.value("file").toString());
                                    static_cast<QDialog*>(file)->accept();
                                });
                            }
                            if (step.contains("color")) {
                                QTimer::singleShot(0, [step] {
                                    if (auto* color = qobject_cast<QColorDialog*>(
                                            QApplication::activeModalWidget())) {
                                        color->setCurrentColor(
                                            QColor(step.value("color").toString()));
                                        color->accept();
                                    }
                                });
                            }
                            if (step.contains("row"))
                                if (auto* list = modal->findChild<QListWidget*>("effectList"))
                                    list->setCurrentRow(step.value("row").toInt());
                            if (step.contains("checked"))
                                if (auto* list = modal->findChild<QListWidget*>("effectList"))
                                    if (list->currentItem())
                                        list->currentItem()->setCheckState(
                                            step.value("checked").toBool() ? Qt::Checked
                                                                           : Qt::Unchecked);
                            if (step.contains("click"))
                                if (auto* button = modal->findChild<QPushButton*>(
                                        step.value("click").toString()))
                                    button->click();
                            if (step.contains("control")) {
                                if (auto* decimal = modal->findChild<QDoubleSpinBox*>(
                                        step.value("control").toString()))
                                    decimal->setValue(step.value("value").toDouble());
                                else if (auto* line = modal->findChild<QLineEdit*>(step.value("control").toString()))
                                    line->setText(step.value("value").toString());
                                else if (auto* combo = modal->findChild<QComboBox*>(step.value("control").toString()))
                                    combo->setCurrentIndex(step.value("value").toInt());
                            }
                        }
                        for (auto it = settings.begin(); it != settings.end(); ++it) {
                            if (auto* spin = modal->findChild<QSpinBox*>(it.key()))
                                spin->setValue(it.value().toInt());
                            else if (auto* decimal = modal->findChild<QDoubleSpinBox*>(it.key()))
                                decimal->setValue(it.value().toDouble());
                            else if (auto* combo = modal->findChild<QComboBox*>(it.key()))
                                combo->setCurrentIndex(it.value().toInt());
                            else if (auto* check = modal->findChild<QCheckBox*>(it.key()))
                                check->setChecked(it.value().toBool());
                            else if (auto* line = modal->findChild<QLineEdit*>(it.key()))
                                line->setText(it.value().toString());
                        }
                        if (auto* shortcuts = modal->findChild<QTableWidget*>("shortcutTable")) {
                            int visible = 0;
                            for (int row = 0; row < shortcuts->rowCount(); ++row)
                                visible += !shortcuts->isRowHidden(row);
                            window->setProperty("shortcutVisibleRows", visible);
                        }
                        if (settings.value("restoreDefaults").toBool())
                            modal->findChild<QDialogButtonBox*>()
                                ->button(QDialogButtonBox::RestoreDefaults)
                                ->click();
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
                        {"conflictDetected", d.window->property("conflictDetected").toBool()},
                        {"shortcutVisibleRows", d.window->property("shortcutVisibleRows").toInt()}};
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
        if(o.contains("visibilityDragTo")){const auto last=o["visibilityDragTo"].toInt();for(int row=o["index"].toInt()+1;row<=last;++row){auto* target=tree->topLevelItem(row);if(!target)break;const auto target_rect=tree->visualItemRect(target);const QPointF next(target_rect.left()+10,target_rect.center().y());QMouseEvent move(QEvent::MouseMove,next,tree->viewport()->mapToGlobal(next),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&move);}}
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
            QJsonArray visibility;for(const auto& layer:c->project().state().layers)visibility.append(layer.visible);result.insert("visibility",visibility);
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
    if (op == "window_focus_loss") {
        QEvent event(QEvent::WindowDeactivate);
        QApplication::sendEvent(d.window->active_canvas(), &event);
        return {{"ok", true}};
    }
    if (op == "window_key") {
        auto* c = d.window->active_canvas();
        c->window()->activateWindow();
        QApplication::processEvents();
        c->setFocus();
        const auto keys = QKeySequence(o.value("key").toString());
        const auto combination = keys[0];
        QKeyEvent press(o.value("release").toBool() ? QEvent::KeyRelease : QEvent::KeyPress,
                        combination.key(), combination.keyboardModifiers());
        if (o.contains("timestamp"))
            press.setTimestamp(o.value("timestamp").toInteger());
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
    if (op == "window_navigation_frames") {
        auto* c = d.window->active_canvas();
        QJsonArray frames;
        for (const auto zoom : o.value("zooms").toArray()) {
            QElapsedTimer timer;
            timer.start();
            c->set_zoom(zoom.toDouble());
            c->repaint();
            QApplication::processEvents();
            frames.append(timer.nsecsElapsed() / 1e6);
        }
        const auto bounds =
            c->view_transform().mapRect(QRectF(QPointF(), c->project().state().size));
        return {
            {"frameMs", frames}, {"zoom", c->zoom()}, {"fits", QRectF(c->rect()).contains(bounds)}};
    }
    if (op == "window_wheel") {
        auto* c = d.window->active_canvas();
        const QPointF point(c->width() / 2., c->height() / 2.);
        QWheelEvent event(point, c->mapToGlobal(point.toPoint()), QPoint(),
                          QPoint(0, o.value("delta").toInt(120)), Qt::NoButton,
                          Qt::KeyboardModifiers(o.value("modifiers").toInt()), Qt::NoScrollPhase,
                          false);
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
        for (auto* action : d.window->findChild<QActionGroup*>("toolActions")->actions())
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
                {"quickMask", c->quick_mask},
                {"backgroundLayer", layer && layer->background},
                {"rotation", c->rotation},
                {"screenMode", d.window->property("screenMode").toInt()},
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
                {"background", c->background.name()},
                {"m12", matrix.m12()},
                {"viewportWidth", c->width()},
                {"viewportHeight", c->height()},
                {"viewX", origin.x()},
                {"viewY", origin.y()}};
    }
    if (op == "window_color_button") {
        auto* button = d.window->findChild<QToolButton*>(o.value("name").toString());
        if (!button)
            throw EditError("color control missing");
        auto* timer = new QTimer(d.window.get());
        timer->setInterval(20);
        const auto options = o;
        auto ticks = std::make_shared<int>(0);
        QObject::connect(timer, &QTimer::timeout, d.window.get(), [timer, options, ticks] {
            if (auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
                dialog->setCurrentColor(QColor(options.value("color").toString("#123456")));
                if (options.contains("capture"))
                    dialog->grab().save(options.value("capture").toString());
                if (options.value("accept").toBool(true))
                    dialog->accept();
                else
                    dialog->reject();
                timer->stop();
            } else if (++*ticks > 100) {
                if (auto* active = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                    active->reject();
                timer->stop();
            }
        });
        timer->start();
        button->click();
        timer->stop();
        timer->deleteLater();
        QApplication::processEvents();
        return {{"enabled", button->isEnabled()}, {"visible", button->isVisible()}};
    }
    if (op == "window_chrome") {
        QApplication::processEvents();
        QJsonArray buttons, menus;
        auto* panel = d.window->findChild<QWidget*>("toolGrid");
        if (panel) {
            for (auto* button : panel->findChildren<QToolButton*>()) {
                if (!button->defaultAction() || !button->objectName().startsWith("toolButton_"))
                    continue;
                if (o.contains("click") &&
                    button->defaultAction()->data().toInt() == o.value("click").toInt())
                    button->click();
                const auto position = button->mapTo(panel, QPoint());
                buttons.append(QJsonObject{{"tool", button->defaultAction()->data().toInt()},
                                           {"category", button->property("category").toString()},
                                           {"x", position.x()},
                                           {"y", position.y()},
                                           {"width", button->width()},
                                           {"height", button->height()},
                                           {"visible", button->isVisible()},
                                           {"checked", button->isChecked()}});
            }
        }
        const auto visit = [&](const auto& self, const QList<QAction*>& actions,
                               const QString& prefix) -> void {
            for (auto* action : actions) {
                if (action->isSeparator())
                    continue;
                const auto path = prefix + "/" + action->text();
                if (action->menu())
                    self(self, action->menu()->actions(), path);
                else
                    menus.append(QJsonObject{{"path", path},
                                             {"text", action->text()},
                                             {"shortcut", action->shortcut().toString()}});
            }
        };
        visit(visit, d.window->findChild<QMenuBar*>()->actions(), QString{});
        QJsonArray options;
        if (auto* bar = d.window->findChild<QToolBar*>("viewToolbar"))
            for (auto* widget : bar->findChildren<QWidget*>()) {
                if (widget->objectName().isEmpty() || widget->objectName().startsWith("qt_"))
                    continue;
                QJsonObject item{{"name", widget->objectName()},
                                 {"visible", widget->isVisible()},
                                 {"enabled", widget->isEnabled()},
                                 {"type", widget->metaObject()->className()},
                                 {"tooltip", widget->toolTip()}};
                if (auto* button = qobject_cast<QToolButton*>(widget))
                    item.insert("checked", button->isChecked());
                if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
                    item.insert("value", spin->value());
                    item.insert("text", spin->text());
                }
                options.append(item);
            }
        return {{"buttons", buttons}, {"menus", menus}, {"options", options}};
    }
    if (op == "window_capture") {
        QApplication::processEvents();
        const bool saved = d.window->grab().save(o.value("path").toString());
        return {{"ok", saved}, {"phase", d.window->active_canvas() ? d.window->active_canvas()->ants_phase() : 0}};
    }
    if (op == "shutdown") {
        refine_command({{"close",true}});
        d.semantic_window.reset();
        d.raw_window.reset();
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
