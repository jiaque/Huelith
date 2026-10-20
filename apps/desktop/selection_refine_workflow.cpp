#include "canvas_view.h"
#include "huelith/editing/image_storage.h"
#include "editor_controller.h"
#include "main_window.h"
#include "selection_refine_dialog.h"
#include <QPainter>
#include <QPointer>
#include <algorithm>
#include <cmath>
namespace huelith::desktop {
namespace {
uchar mask_sample(const QImage& image, QPointF position) {
    const double px = position.x() - .5, py = position.y() - .5;
    const int x = int(std::floor(px)), y = int(std::floor(py));
    const auto value = [&](int xx, int yy) {
        return image.rect().contains(xx, yy) ? double(image.constScanLine(yy)[xx]) : 0.;
    };
    return uchar(std::clamp(
        qRound(std::lerp(std::lerp(value(x, y), value(x + 1, y), px - x),
                         std::lerp(value(x, y + 1), value(x + 1, y + 1), px - x), py - y)),
        0, 255));
}
QImage mask_to_canvas(const editing::State& state, const editing::Layer& layer) {
    QImage result(state.size, QImage::Format_Grayscale8);
    result.fill(0);
    if (layer.mask.isNull())
        return result;
    const auto parent = editing::parent_matrix(state, layer.id);
    const auto world = layer.transform * parent;
    const auto inverse = world.inverted();
    const auto mask_inverse = (layer.mask_transform * parent).inverted();
    for (int y = 0; y < result.height(); ++y) {
        auto* row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            QPointF p(x + .5, y + .5);
            if (layer.warped && layer.mask_linked) {
                const auto local = editing::warp_point(
                    layer, editing::layer_source_size(layer, state.size), inverse.map(p), true);
                if (!local)
                    continue;
                p = world.map(*local);
            }
            row[x] = mask_sample(layer.mask, mask_inverse.map(p));
        }
    }
    return result;
}
void install_canvas_mask(editing::State& state, editing::Layer& layer, const QImage& mask) {
    const bool existing = !layer.mask.isNull() || layer.vector_enabled;
    const auto size =
        layer.mask.isNull() ? editing::layer_source_size(layer, state.size) : layer.mask.size();
    if (!existing) {
        layer.mask_transform = layer.transform;
        layer.mask_linked = true;
    }
    const auto parent = editing::parent_matrix(state, layer.id);
    const auto mask_world = layer.mask_transform * parent;
    const auto world = layer.transform * parent;
    const auto to_local = layer.mask_transform * layer.transform.inverted();
    QImage result(size, QImage::Format_Grayscale8);
    result.fill(0);
    for (int y = 0; y < size.height(); ++y) {
        auto* row = result.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const QPointF p(x + .5, y + .5);
            auto canvas = mask_world.map(p);
            if (layer.warped && layer.mask_linked) {
                const auto warped = editing::warp_point(
                    layer, editing::layer_source_size(layer, state.size), to_local.map(p), false);
                if (!warped)
                    continue;
                canvas = world.map(*warped);
            }
            row[x] = mask_sample(mask, canvas);
        }
    }
    layer.mask = std::move(result);
    layer.mask_enabled = true;
    layer.background = false;
}
} // namespace
void EditorController::show_selection_refine() {
    auto* c = canvas();
    if (!c)
        return;
    guarded([&] {
        const QPointer<CanvasView> guard(c);
        const auto revision = c->project().revision();
        const auto before = c->project().state();
        const auto active_id = c->active_layer;
        const auto* active = editing::find_layer(before.layers, active_id);
        if (!active)
            throw editing::EditError(tr("请选择图层"));
        auto selected = before.selection;
        if (selected.isNull()) {
            selected = mask_to_canvas(before, *active);
        }
        auto unmasked = before;
        if (auto* layer = editing::find_layer(unmasked.layers, active_id))
            layer->mask_enabled = false;
        const auto image =
            editing::sample_layers(unmasked, active_id, editing::SampleLayers::current);
        auto backdrop = before;
        editing::find_layer(backdrop.layers, active_id)->visible = false;
        SelectionRefineDialog dialog(image, selected, window_, false, editing::render(backdrop));
        if (dialog.exec() != QDialog::Accepted)
            return;
        if (!guard || guard->project().revision() != revision)
            throw editing::EditError(tr("图像已变化，请重新细化选区"));
        const auto result = dialog.result_images();
        const int mode = dialog.output_mode();
        if (mode == 0) {
            c->edit(tr("选择并遮住"),
                    [&](editing::State& state) { state.selection = result.mask; });
            return;
        }
        if (mode == 1) {
            c->edit(tr("选择并遮住"), [&](editing::State& state) {
                if (editing::layer_locked(state.layers, active_id))
                    throw editing::EditError(tr("当前图层已锁定"));
                auto* layer = editing::find_layer(state.layers, active_id);
                install_canvas_mask(state, *layer, result.mask);
                state.selection = {};
            });
            return;
        }
        editing::Layer layer;
        layer.name = tr("选择并遮住");
        layer.pixels = result.pixels.isNull() ? image : result.pixels;
        if(mode!=3 && mode!=5 && layer.pixels.sizeInBytes()>=editing::mapped_image_threshold)
            layer.pixels=editing::copy_image(layer.pixels);
        if (mode == 3 || mode == 5)
            layer.mask = result.mask;
        else
            for (int y = 0; y < layer.pixels.height(); ++y) {
                auto* row = reinterpret_cast<float*>(layer.pixels.scanLine(y));
                const auto* mask = result.mask.constScanLine(y);
                for (int x = 0; x < layer.pixels.width(); ++x)
                    for (int channel = 0; channel < 4; ++channel)
                        row[x * 4 + channel] *= mask[x] / 255.F;
            }
        const auto id = layer.id;
        if (mode >= 4) {
            CanvasSpec spec;
            spec.width = before.size.width();
            spec.height = before.size.height();
            spec.resolution_ppi = before.ppi;
            spec.name = tr("%1 - 选区").arg(before.name).toStdString();
            if (!window_->create_document(spec))
                throw editing::EditError(tr("无法创建选区文档"));
            auto state = window_->active_canvas()->project().state();
            state.layers.push_back(std::move(layer));
            window_->active_canvas()->load_state(std::move(state), false);
            window_->active_canvas()->active_layer = id;
        } else {
            c->edit(tr("选择并遮住"), [&](editing::State& state) {
                state.layers.push_back(std::move(layer));
                state.selection = {};
            });
            c->active_layer = id;
        }
        refresh();
    });
}
} // namespace huelith::desktop
