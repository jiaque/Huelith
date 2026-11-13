#include <QCoreApplication>
#include "huelith/editing/layout_regions.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/photo_operations.h"
#include <cmath>

namespace huelith::editing {
void resize_document(State& state, QSize size, double ppi) {
    if (size.isEmpty() || size.width() > 65536 || size.height() > 65536 ||
        qint64(size.width()) * size.height() > maximum_image_pixels || !std::isfinite(ppi) ||
        ppi < 1 || ppi > 100000)
        throw EditError(QCoreApplication::translate("Huelith", "图像尺寸或分辨率无效"));
    const double sx = double(size.width()) / state.size.width(),
                 sy = double(size.height()) / state.size.height();
    const auto scale = QTransform::fromScale(sx, sy);
    for (auto& layer : state.layers) {
        layer.transform *= scale;
        layer.mask_transform *= scale;
        layer.background = false;
    }
    const auto resize = [&](QImage& mask) {
        if (!mask.isNull())
            mask = mask.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    };
    resize(state.selection);
    for (auto& mask : state.saved_selections)
        resize(mask);
    for (auto& path : state.paths)
        path = scale.map(path);
    for (auto& slice : state.slices)
        slice.bounds = scale.mapRect(slice.bounds);
    for (auto& guide : state.horizontal_guides)
        guide *= sy;
    for (auto& guide : state.vertical_guides)
        guide *= sx;
    state.size = size;
    state.ppi = ppi;
    state.snapshots.clear();
}
void set_artboard_bounds(Layer& board, QRect bounds) {
    if (bounds.isEmpty() || bounds.width() > 65536 || bounds.height() > 65536 ||
        std::abs(qint64(bounds.x())) > 1000000 || std::abs(qint64(bounds.y())) > 1000000)
        throw EditError(QCoreApplication::translate("Huelith", "画板尺寸或位置无效"));
    board.kind = LayerKind::group;
    board.artboard = true;
    board.isolated = true;
    board.frame = QRectF(QPointF(), bounds.size());
    board.transform = QTransform::fromTranslate(bounds.x(), bounds.y());
    board.vector_mask = {};
    board.vector_mask.addRect(*board.frame);
    board.vector_enabled = true;
}
QUuid create_artboard(State& state, QString name, QRect bounds,
                      const std::vector<QUuid>& contents) {
    if (name.trimmed().isEmpty())
        throw EditError(QCoreApplication::translate("Huelith", "请输入画板名称"));
    Layer board;
    board.name = name.trimmed();
    set_artboard_bounds(board, bounds);
    const auto roots = root_targets(state, contents);
    for (const auto& id : roots) {
        const auto* original = find_layer(state.layers, id);
        if (!original || layer_locked(state.layers, id))
            throw EditError(QCoreApplication::translate("Huelith", "选中的图层不存在或已锁定"));
        auto child = *original;
        child.background = false;
        child.transform *= parent_matrix(state, id) * board.transform.inverted();
        child.mask_transform *= parent_matrix(state, id) * board.transform.inverted();
        board.children.push_back(std::move(child));
    }
    for (const auto& id : roots)
        remove_layer(state, id);
    const auto id = board.id;
    state.layers.push_back(std::move(board));
    return id;
}
std::vector<Snapshot> export_regions(const Snapshot& snapshot, int scope,
                                     const std::vector<QUuid>& selected) {
    if (scope == 0)
        return {snapshot};
    if (scope < 0 || scope > 3)
        throw EditError(QCoreApplication::translate("Huelith", "导出范围无效"));
    std::vector<Snapshot> result;
    auto append = [&](State state, QString name, QRect bounds) {
        if (scope != 2)
            bounds = bounds.intersected(QRect(QPoint(), snapshot.state.size));
        if (bounds.isEmpty())
            throw EditError(QCoreApplication::translate("Huelith", "导出区域在画布之外：") + name);
        state.slices.clear();
        state.selection = {};
        crop_canvas(state, bounds, 0, false);
        state.name = snapshot.state.name + "-" + name;
        result.push_back({std::move(state), snapshot.revision});
    };
    if (scope == 3) {
        for (const auto& slice : snapshot.state.slices)
            append(snapshot.state, slice.name, slice.bounds);
    } else {
        auto ids = root_targets(snapshot.state, selected);
        if (scope == 2) {
            ids.clear();
            const auto visit = [&](auto&& self, const std::vector<Layer>& layers) -> void {
                for (const auto& layer : layers) {
                    if (layer.artboard)
                        ids.push_back(layer.id);
                    else
                        self(self, layer.children);
                }
            };
            visit(visit, snapshot.state.layers);
        }
        for (const auto& id : ids) {
            const auto* source = find_layer(snapshot.state.layers, id);
            if (!source)
                throw EditError(QCoreApplication::translate("Huelith", "待导出图层不存在"));
            auto state = snapshot.state;
            if (source->clipped)
                throw EditError(QCoreApplication::translate("Huelith", "剪贴图层请连同底图放入组后导出，避免丢失剪贴范围"));
            const auto isolate = [&](auto&& self, std::vector<Layer>& layers) -> bool {
                for (auto it = layers.begin(); it != layers.end();) {
                    if (it->id == id) {
                        it->visible = true;
                        it->background = false;
                        ++it;
                    } else if (self(self, it->children)) {
                        if (it->clipped)
                            throw EditError(QCoreApplication::translate("Huelith", "父组使用剪贴蒙版，请连同底图一起导出"));
                        it->visible = true;
                        ++it;
                    } else
                        it = layers.erase(it);
                }
                return !layers.empty();
            };
            isolate(isolate, state.layers);
            auto bounds = source->artboard ? (source->transform * parent_matrix(snapshot.state, id))
                                                 .mapRect(*source->frame)
                                                 .toAlignedRect()
                                           : layer_bounds(snapshot.state, id).toAlignedRect();
            if (!source->artboard) {
                // Include rendered shadows and strokes, which extend beyond the source rectangle.
                const auto image = render(state);
                int left = image.width(), top = image.height(), right = -1, bottom = -1;
                for (int y = 0; y < image.height(); ++y) {
                    const auto* row = reinterpret_cast<const float*>(image.constScanLine(y));
                    for (int x = 0; x < image.width(); ++x)
                        if (row[x * 4 + 3] > 0) {
                            left = std::min(left, x);
                            right = std::max(right, x);
                            top = std::min(top, y);
                            bottom = std::max(bottom, y);
                        }
                }
                if (right >= left)
                    bounds = QRect(QPoint(left, top), QPoint(right, bottom));
            }
            append(std::move(state), source->name, bounds);
        }
    }
    if (result.empty())
        throw EditError(QCoreApplication::translate("Huelith", "当前范围没有可导出的图层、画板或切片"));
    return result;
}
} // namespace huelith::editing
