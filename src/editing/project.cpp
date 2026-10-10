#include "huelith/editing/project.h"
#include "huelith/editing/tiff_io.h"
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace huelith::editing {
namespace {
constexpr qint64 max_pixels = 64'000'000;
QImage blank(QSize size) {
    QImage image(size, QImage::Format_RGBA32FPx4_Premultiplied);
    if (image.isNull())
        throw EditError(QStringLiteral("无法分配图像内存"));
    image.fill(Qt::transparent);
    image.setColorSpace(working_space());
    return image;
}
void check_size(QSize size) {
    if (size.isEmpty() || size.width() > 65536 || size.height() > 65536 ||
        qint64(size.width()) * size.height() > max_pixels)
        throw EditError(QStringLiteral("图片尺寸超出当前 6400 万像素预算"));
}
float coverage(const Layer& layer, int x, int y) {
    float value = layer.opacity;
    QPointF position(x + .5, y + .5);
    if (layer.warped && layer.mask_linked && (!layer.mask.isNull() || layer.vector_enabled)) {
        const auto local =
            warp_point(layer, layer.pixels.isNull() ? layer.mask.size() : layer.pixels.size(),
                       layer.transform.inverted().map(position), true);
        if (!local)
            return 0;
        position = layer.transform.map(*local);
    }
    if (layer.mask_enabled && !layer.mask.isNull()) {
        const auto p = layer.mask_transform.inverted().map(position);
        const int mx = static_cast<int>(std::floor(p.x()));
        const int my = static_cast<int>(std::floor(p.y()));
        value *=
            layer.mask.rect().contains(mx, my) ? layer.mask.constScanLine(my)[mx] / 255.F : 0.F;
    }
    if (layer.vector_enabled &&
        !layer.vector_mask.contains(layer.mask_transform.inverted().map(position)))
        return 0.F;
    return value;
}
float encode(float value) {
    value = std::clamp(value, 0.F, 1.F);
    return value <= .0031308F ? 12.92F * value : 1.055F * std::pow(value, 1.F / 2.4F) - .055F;
}
float decode(float value) {
    return value <= .04045F ? value / 12.92F : std::pow((value + .055F) / 1.055F, 2.4F);
}
void composite(QImage& target, const QImage& source, const Layer& layer, const QImage& clip) {
    const bool artistic = layer.blend != Blend::normal;
    QImage destination = artistic
                             ? target.convertedToColorSpace(QColorSpace::SRgbLinear,
                                                            QImage::Format_RGBA32FPx4_Premultiplied)
                             : target;
    const QImage input = artistic
                             ? source.convertedToColorSpace(QColorSpace::SRgbLinear,
                                                            QImage::Format_RGBA32FPx4_Premultiplied)
                             : source;
    for (int y = 0; y < target.height(); ++y) {
        auto* dst = reinterpret_cast<float*>(destination.scanLine(y));
        const auto* src = reinterpret_cast<const float*>(input.constScanLine(y));
        const auto* base =
            clip.isNull() ? nullptr : reinterpret_cast<const float*>(clip.constScanLine(y));
        for (int x = 0; x < target.width(); ++x) {
            const int i = x * 4;
            const float k = coverage(layer, x, y) * (base ? base[i + 3] : 1.F);
            const float alpha = src[i + 3] * k, backdrop_alpha = dst[i + 3];
            Color term{};
            if (artistic && alpha > 0 && backdrop_alpha > 0) {
                Color a{}, b{};
                for (std::size_t c = 0; c < 3; ++c) {
                    a[c] = encode(src[i + c] / src[i + 3]);
                    b[c] = encode(dst[i + c] / backdrop_alpha);
                }
                term = artistic_term(a, b, layer.blend);
            }
            for (int c = 0; c < 3; ++c) {
                if (artistic && alpha > 0 && backdrop_alpha > 0)
                    dst[i + c] = src[i + c] * k * (1 - backdrop_alpha) + dst[i + c] * (1 - alpha) +
                                 alpha * backdrop_alpha * decode(term[static_cast<std::size_t>(c)]);
                else
                    dst[i + c] = src[i + c] * k + dst[i + c] * (1 - alpha);
            }
            dst[i + 3] = alpha + backdrop_alpha * (1 - alpha);
        }
    }
    target = artistic ? destination.convertedToColorSpace(working_space(),
                                                          QImage::Format_RGBA32FPx4_Premultiplied)
                      : destination;
}
QColor working_color(QColor color) {
    QImage sample(1, 1, QImage::Format_RGBA8888);
    sample.fill(color);
    const auto converted = to_working(sample);
    const auto* p = reinterpret_cast<const float*>(converted.constBits());
    return p[3] > 0 ? QColor::fromRgbF(p[0] / p[3], p[1] / p[3], p[2] / p[3], p[3])
                    : QColor(Qt::transparent);
}
void transform_children(std::vector<Layer>& children, const QTransform& transform) {
    for (auto& child : children) {
        child.transform = child.transform * transform;
        child.mask_transform = child.mask_transform * transform;
    }
}

void paint_warp(QPainter& painter, const QImage& image, const Layer& layer) {
    if (!layer.warped) {
        painter.drawImage(QPointF(), image);
        return;
    }
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const double w = image.width() / 2., h = image.height() / 2.;
            const QRectF source(x * w, y * h, w, h);
            const auto point = [&](int column, int row) {
                const auto value = layer.warp_grid[std::size_t(row * 3 + column)];
                return QPointF(value.x() * image.width(), value.y() * image.height());
            };
            const QPolygonF target{point(x, y), point(x + 1, y), point(x + 1, y + 1),
                                   point(x, y + 1)};
            QTransform matrix;
            if (!QTransform::quadToQuad(QPolygonF{source.topLeft(), source.topRight(),
                                                  source.bottomRight(), source.bottomLeft()},
                                        target, matrix))
                continue;
            painter.save();
            painter.setTransform(matrix, true);
            painter.setClipRect(source);
            painter.drawImage(QPointF(), image);
            painter.restore();
        }
}
void render_layers(QImage& target, const std::vector<Layer>& layers) {
    QImage base;
    for (const auto& layer : layers) {
        if (!layer.visible) {
            if (!layer.clipped)
                base = blank(target.size());
            continue;
        }
        if (layer.clipped && base.isNull())
            continue;
        if (layer.kind == LayerKind::adjustment) {
            const float gain = static_cast<float>(std::exp2(layer.exposure));
            const auto toned = apply_tone(target, layer.tone);
            for (int y = 0; y < target.height(); ++y) {
                auto* row = reinterpret_cast<float*>(target.scanLine(y));
                const auto* adjusted = reinterpret_cast<const float*>(toned.constScanLine(y));
                const auto* clip =
                    base.isNull() ? nullptr : reinterpret_cast<const float*>(base.constScanLine(y));
                for (int x = 0; x < target.width(); ++x) {
                    const float k =
                        coverage(layer, x, y) * (layer.clipped && clip ? clip[4 * x + 3] : 1.F);
                    for (int c = 0; c < 3; ++c)
                        row[4 * x + c] += k * (adjusted[4 * x + c] * gain - row[4 * x + c]);
                }
            }
            continue;
        }
        if (layer.kind == LayerKind::group && !layer.isolated && !layer.clipped &&
            layer.blend == Blend::normal && !layer.warped) {
            const QImage before = target;
            auto children = layer.children;
            transform_children(children, layer.transform);
            render_layers(target, children);
            for (int y = 0; y < target.height(); ++y) {
                auto* row = reinterpret_cast<float*>(target.scanLine(y));
                const auto* old = reinterpret_cast<const float*>(before.constScanLine(y));
                for (int x = 0; x < target.width(); ++x)
                    for (int c = 0; c < 4; ++c)
                        row[4 * x + c] = old[4 * x + c] +
                                         coverage(layer, x, y) * (row[4 * x + c] - old[4 * x + c]);
            }
            // Clipping uses the group's intrinsic alpha, never the external backdrop.
            base = blank(target.size());
            render_layers(base, children);
            QImage masked = blank(target.size());
            composite(masked, base, layer, {});
            base = masked;
            continue;
        }
        QImage local = blank(target.size());
        if (layer.kind == LayerKind::group || layer.kind == LayerKind::composition) {
            render_layers(local, layer.children);
            if (!layer.transform.isIdentity() || layer.warped) {
                QImage transformed = blank(target.size());
                QPainter painter(&transformed);
                painter.setTransform(layer.transform);
                paint_warp(painter, local, layer);
                painter.end();
                local = transformed;
            }
        } else {
            QPainter painter(&local);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setTransform(layer.transform);
            if (layer.kind == LayerKind::fill) {
                if (layer.gradient) {
                    QLinearGradient gradient(0, 0, target.width(), target.height());
                    gradient.setColorAt(0, working_color(layer.fill));
                    gradient.setColorAt(1, working_color(layer.fill_end));
                    painter.fillRect(local.rect(), gradient);
                } else
                    painter.fillRect(local.rect(), working_color(layer.fill));
            } else
                paint_warp(painter, layer.pixels, layer);
        }
        if (layer.exposure != 0) {
            const float gain = static_cast<float>(std::exp2(layer.exposure));
            for (int y = 0; y < local.height(); ++y) {
                auto* row = reinterpret_cast<float*>(local.scanLine(y));
                for (int x = 0; x < local.width(); ++x)
                    for (int c = 0; c < 3; ++c)
                        row[4 * x + c] *= gain;
            }
        }
        local = apply_tone(local, layer.tone);
        composite(target, local, layer, layer.clipped ? base : QImage{});
        if (!layer.clipped) {
            base = blank(target.size());
            composite(base, local, layer, {});
        }
    }
}
void validate_layers(const std::vector<Layer>& layers, QSet<QUuid>& ids, int depth) {
    if (depth > 32)
        throw EditError(QStringLiteral("图层嵌套超过 32 层"));
    for (const auto& layer : layers) {
        validate_tone(layer.tone);
        if (layer.background &&
            (depth != 0 || &layer != &layers.front() || layer.kind != LayerKind::pixels ||
             !layer.transform.isIdentity() || layer.warped || layer.opacity != 1.F ||
             layer.blend != Blend::normal || layer.clipped || !layer.mask.isNull() ||
             layer.vector_enabled || !layer.children.empty()))
            throw EditError(
                QStringLiteral("背景图层固定在底部；请先转换为普通图层再变换或添加透明度"));
        if (int(layer.kind) < 0 || int(layer.kind) > int(LayerKind::composition) ||
            int(layer.blend) < 0 || int(layer.blend) > 23)
            throw EditError(QStringLiteral("未知图层类型或混合模式"));
        if (layer.id.isNull() || ids.contains(layer.id) || ids.size() >= 4096)
            throw EditError(QStringLiteral("图层标识重复或数量过多"));
        ids.insert(layer.id);
        for (const auto& matrix : {layer.transform, layer.mask_transform})
            for (double value :
                 {matrix.m11(), matrix.m12(), matrix.m13(), matrix.m21(), matrix.m22(),
                  matrix.m23(), matrix.m31(), matrix.m32(), matrix.m33()})
                if (!std::isfinite(value))
                    throw EditError(QStringLiteral("变换含非有限数值"));
        for (const auto& point : layer.warp_grid)
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
                std::abs(point.x()) > 100 || std::abs(point.y()) > 100)
                throw EditError(QStringLiteral("无效网格变形坐标"));
        if (layer.name.trimmed().isEmpty() || !std::isfinite(layer.opacity) || layer.opacity < 0 ||
            layer.opacity > 1 || !std::isfinite(layer.exposure) || std::abs(layer.exposure) > 16 ||
            !layer.transform.isInvertible() || !layer.mask_transform.isInvertible())
            throw EditError(QStringLiteral("无效图层参数或变换"));
        if (!layer.pixels.isNull() &&
            layer.pixels.format() != QImage::Format_RGBA32FPx4_Premultiplied)
            throw EditError(QStringLiteral("图层需要 FP32 像素"));
        if (!layer.mask.isNull() && layer.mask.format() != QImage::Format_Grayscale8)
            throw EditError(QStringLiteral("蒙版格式无效"));
        validate_layers(layer.children, ids, depth + 1);
    }
}
bool erase(std::vector<Layer>& layers, const QUuid& id) {
    const auto it = std::find_if(layers.begin(), layers.end(),
                                 [&](const Layer& layer) { return layer.id == id; });
    if (it != layers.end()) {
        layers.erase(it);
        return true;
    }
    for (auto& layer : layers)
        if (erase(layer.children, id))
            return true;
    return false;
}
} // namespace
QColorSpace working_space() {
    return {QColorSpace::Primaries::Bt2020, QColorSpace::TransferFunction::Linear};
}
QImage to_working(QImage image) {
    check_size(image.size());
    if (!image.colorSpace().isValid())
        image.setColorSpace(QColorSpace::SRgb);
    auto result =
        image.convertedToColorSpace(working_space(), QImage::Format_RGBA32FPx4_Premultiplied);
    if (result.isNull())
        throw EditError(QStringLiteral("颜色转换失败"));
    return result;
}
QImage import_image(const QString& path) {
    if (path.endsWith(".tif", Qt::CaseInsensitive) || path.endsWith(".tiff", Qt::CaseInsensitive))
        return read_tiff(path);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (reader.format().toLower() != "png" && reader.format().toLower() != "jpeg" &&
        reader.format().toLower() != "jpg")
        throw EditError(QStringLiteral("支持 JPEG / PNG / TIFF；RAW 请通过显影窗口打开"));
    check_size(reader.size());
    auto image = reader.read();
    if (image.isNull())
        throw EditError(reader.errorString());
    return to_working(std::move(image));
}
QImage display_image(const QImage& image, const QColorSpace& output) {
    if (!output.isValid())
        throw EditError(QStringLiteral("显示配置文件无效"));
    return image.convertedToColorSpace(output, QImage::Format_RGBA8888);
}
QImage render(const State& state) {
    validate_state(state);
    auto result = blank(state.size);
    render_layers(result, state.layers);
    return result;
}
Layer* find_layer(std::vector<Layer>& layers, const QUuid& id) {
    for (auto& layer : layers) {
        if (layer.id == id)
            return &layer;
        if (auto* found = find_layer(layer.children, id))
            return found;
    }
    return nullptr;
}
const Layer* find_layer(const std::vector<Layer>& layers, const QUuid& id) {
    for (const auto& layer : layers) {
        if (layer.id == id)
            return &layer;
        if (const auto* found = find_layer(layer.children, id))
            return found;
    }
    return nullptr;
}
void validate_state(const State& state) {
    check_size(state.size);
    if (state.name.trimmed().isEmpty() || !std::isfinite(state.ppi) || state.ppi <= 0)
        throw EditError(QStringLiteral("无效文档参数"));
    if (!state.selection.isNull() && (state.selection.size() != state.size ||
                                      state.selection.format() != QImage::Format_Grayscale8))
        throw EditError(QStringLiteral("选区尺寸或格式无效"));
    QSet<QUuid> ids;
    validate_layers(state.layers, ids, 0);
}
bool layer_locked(const std::vector<Layer>& layers, const QUuid& id) {
    for (const auto& layer : layers) {
        if (layer.id == id)
            return layer.locked;
        if (find_layer(layer.children, id))
            return layer.locked || layer_locked(layer.children, id);
    }
    return false;
}
bool layer_position_locked(const std::vector<Layer>& layers, const QUuid& id) {
    const auto* layer = find_layer(layers, id);
    return layer_locked(layers, id) || (layer && layer->background);
}
void shift_layer(State& state, const QUuid& id, int direction) {
    std::function<bool(const std::vector<Layer>&, const QUuid&)> locate;
    locate = [&](const std::vector<Layer>& layers, const QUuid& parent) {
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].id == id) {
                move_layer(state, id, parent, int(i) + (direction > 0 ? 2 : -1));
                return true;
            }
            if (locate(layers[i].children, layers[i].id))
                return true;
        }
        return false;
    };
    if (!locate(state.layers, {}))
        throw EditError(QStringLiteral("图层不存在"));
}
void remove_layer(State& state, const QUuid& id) {
    const auto* layer = find_layer(state.layers, id);
    if (!layer || layer_locked(state.layers, id))
        throw EditError(QStringLiteral("图层不存在或已锁定"));
    erase(state.layers, id);
}
void move_layer(State& state, const QUuid& id, const QUuid& parent, int position) {
    const auto* source = find_layer(state.layers, id);
    if (!source || layer_position_locked(state.layers, id) || id == parent ||
        find_layer(source->children, parent))
        throw EditError(QStringLiteral("禁止循环引用或移动锁定图层"));
    if (!parent.isNull()) {
        const auto* destination = find_layer(state.layers, parent);
        if (!destination || layer_locked(state.layers, parent) ||
            (destination->kind != LayerKind::group && destination->kind != LayerKind::composition))
            throw EditError(QStringLiteral("目标不是可编辑组"));
    }
    const auto& previous_target =
        parent.isNull() ? state.layers : find_layer(state.layers, parent)->children;
    for (std::size_t i = 0; i < previous_target.size(); ++i)
        if (previous_target[i].id == id && int(i) < position) {
            --position;
            break;
        }
    Layer moving = *source;
    erase(state.layers, id);
    auto& target = parent.isNull() ? state.layers : find_layer(state.layers, parent)->children;
    const int minimum = !target.empty() && target.front().background ? 1 : 0;
    const auto index = std::clamp(position, minimum, static_cast<int>(target.size()));
    target.insert(target.begin() + index, std::move(moving));
}
Layer duplicate_layer(const Layer& source) {
    auto result = source;
    result.id = QUuid::createUuid();
    result.background = false;
    for (auto& child : result.children)
        child = duplicate_layer(child);
    return result;
}
QImage path_range(QSize size, const QPainterPath& path) {
    QImage rgba(size, QImage::Format_ARGB32_Premultiplied);
    rgba.fill(Qt::black);
    QPainter painter(&rgba);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillPath(path, Qt::white);
    painter.end();
    return rgba.convertToFormat(QImage::Format_Grayscale8);
}
void combine_range(State& state, const QImage& range, RangeOp operation) {
    if (range.size() != state.size || range.format() != QImage::Format_Grayscale8)
        throw EditError(QStringLiteral("选区格式不匹配"));
    if (operation == RangeOp::replace || state.selection.isNull()) {
        state.selection = range;
        return;
    }
    for (int y = 0; y < range.height(); ++y) {
        auto* dst = state.selection.scanLine(y);
        const auto* src = range.constScanLine(y);
        for (int x = 0; x < range.width(); ++x) {
            const int a = dst[x], b = src[x];
            dst[x] = static_cast<uchar>(operation == RangeOp::add        ? std::max(a, b)
                                        : operation == RangeOp::subtract ? std::max(0, a - b)
                                                                         : std::min(a, b));
        }
    }
}
void modify_range(State& state, int radius, int operation) {
    if (state.selection.isNull() || radius < 1 || radius > 64)
        throw EditError(QStringLiteral("需要选区；半径为 1–64"));
    // Separable bounded kernel retains soft coverage; no thresholded binary conversion.
    for (int axis = 0; axis < 2; ++axis) {
        const QImage before = state.selection;
        for (int y = 0; y < before.height(); ++y)
            for (int x = 0; x < before.width(); ++x) {
                int value = operation < 0 ? 255 : 0;
                for (int k = -radius; k <= radius; ++k) {
                    const int xx = std::clamp(x + (axis == 0 ? k : 0), 0, before.width() - 1);
                    const int yy = std::clamp(y + (axis == 1 ? k : 0), 0, before.height() - 1);
                    const int v = before.constScanLine(yy)[xx];
                    value = operation == 0  ? value + v
                            : operation > 0 ? std::max(value, v)
                                            : std::min(value, v);
                }
                state.selection.scanLine(y)[x] =
                    static_cast<uchar>(operation == 0 ? value / (2 * radius + 1) : value);
            }
    }
}
void export_image(const State& state, const QString& path, QColor matte, int quality) {
    auto image = display_image(render(state));
    const auto suffix = path.section('.', -1).toLower();
    if (suffix != "png" && suffix != "jpg" && suffix != "jpeg")
        throw EditError(QStringLiteral("导出请选择 PNG 或 JPEG"));
    if (suffix != "png") {
        QImage opaque(image.size(), QImage::Format_RGB888);
        opaque.fill(matte);
        QPainter painter(&opaque);
        painter.drawImage(0, 0, image);
        painter.end();
        opaque.setColorSpace(QColorSpace::SRgb);
        image = opaque;
    }
    image.setDotsPerMeterX(qRound(state.ppi / 0.0254));
    image.setDotsPerMeterY(qRound(state.ppi / 0.0254));
    QImageWriter writer(path, suffix == "png" ? "png" : "jpeg");
    writer.setQuality(quality);
    if (!writer.write(image))
        throw EditError(writer.errorString());
}
Project::Project(State state) : state_(std::move(state)) {
    validate_state(state_);
    history_.push_back({state_, QStringLiteral("初始状态"), revision_});
}
void Project::edit(const QString& name, const std::function<void(State&)>& operation) {
    State draft = state_;
    operation(draft);
    validate_state(draft);
    const quint64 next = next_revision_++;
    history_.resize(position_ + 1);
    history_.push_back({draft, name, next});
    state_ = std::move(draft);
    revision_ = next;
    ++position_;
}
bool Project::undo() {
    if (!can_undo())
        return false;
    --position_;
    state_ = history_[position_].state;
    revision_ = history_[position_].revision;
    return true;
}
bool Project::redo() {
    if (!can_redo())
        return false;
    ++position_;
    state_ = history_[position_].state;
    revision_ = history_[position_].revision;
    return true;
}
QStringList Project::history_names() const {
    QStringList result;
    for (const auto& entry : history_)
        result << entry.name;
    return result;
}

bool Project::seek_history(std::size_t position) {
    if (position >= history_.size())
        return false;
    position_ = position;
    state_ = history_[position].state;
    revision_ = history_[position].revision;
    return true;
}
State Project::history_state(std::size_t position) const {
    if (position >= history_.size())
        throw EditError(QStringLiteral("无效历史位置"));
    return history_[position].state;
}
QTransform parent_matrix(const State& state, const QUuid& id) {
    QTransform result;
    std::function<bool(const std::vector<Layer>&, QTransform)> visit;
    visit = [&](const auto& layers, QTransform parent) {
        for (const auto& layer : layers) {
            if (layer.id == id) {
                result = parent;
                return true;
            }
            if (visit(layer.children, layer.transform * parent))
                return true;
        }
        return false;
    };
    visit(state.layers, {});
    return result;
}
QRectF layer_bounds(const State& state, const QUuid& id) {
    const auto* layer = find_layer(state.layers, id);
    if (!layer)
        return {};
    if (!layer->children.empty()) {
        QRectF bounds;
        for (const auto& child : layer->children)
            bounds = bounds.united(layer_bounds(state, child.id));
        return bounds;
    }
    const QRectF rect = layer->pixels.isNull() ? QRectF(QPointF(), QSizeF(state.size))
                                               : QRectF(layer->pixels.rect());
    return (layer->transform * parent_matrix(state, id)).mapRect(rect);
}
std::vector<QUuid> root_targets(const State& state, const std::vector<QUuid>& ids) {
    std::vector<QUuid> result;
    std::function<void(const std::vector<Layer>&)> visit = [&](const auto& layers) {
        for (const auto& layer : layers) {
            if (std::find(ids.begin(), ids.end(), layer.id) != ids.end()) {
                result.push_back(layer.id);
            } else
                visit(layer.children);
        }
    };
    visit(state.layers);
    return result;
}
void transform_targets(State& state, const std::vector<QUuid>& ids, const QTransform& delta,
                       bool mask) {
    // Validate the whole selection before mutating any member.
    for (const auto& id : root_targets(state, ids))
        if (layer_position_locked(state.layers, id))
            throw EditError(QStringLiteral("选择包含背景或锁定图层；请先转换背景为普通图层"));
    for (const auto& id : root_targets(state, ids)) {
        if (layer_locked(state.layers, id))
            throw EditError(QStringLiteral("选择包含锁定图层"));
        const auto parent = parent_matrix(state, id);
        auto* layer = find_layer(state.layers, id);
        const auto local = parent * delta * parent.inverted();
        if (mask)
            layer->mask_transform *= local;
        else {
            layer->transform *= local;
            if (layer->mask_linked)
                layer->mask_transform *= local;
        }
    }
}
std::optional<QPointF> warp_point(const Layer& layer, QSize size, QPointF point, bool inverse) {
    if (!layer.warped)
        return point;
    if (size.isEmpty())
        return std::nullopt;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const double w = size.width() / 2., h = size.height() / 2.;
            const QRectF source(x * w, y * h, w, h);
            const auto grid = [&](int col, int row) {
                const auto p = layer.warp_grid[std::size_t(row * 3 + col)];
                return QPointF(p.x() * size.width(), p.y() * size.height());
            };
            const QPolygonF target{grid(x, y), grid(x + 1, y), grid(x + 1, y + 1), grid(x, y + 1)};
            QTransform matrix;
            if (!QTransform::quadToQuad(QPolygonF{source.topLeft(), source.topRight(),
                                                  source.bottomRight(), source.bottomLeft()},
                                        target, matrix))
                continue;
            if (inverse) {
                const auto mapped = matrix.inverted().map(point);
                if (source.adjusted(-1e-8, -1e-8, 1e-8, 1e-8).contains(mapped))
                    return mapped;
            } else if (source.contains(point))
                return matrix.map(point);
        }
    return std::nullopt;
}
QUuid hit_layer(const State& state, QPointF point, double tolerance) {
    // Use visible source alpha, nested transforms and clipping bases, not rectangular bounds.
    std::function<float(const Layer&, QTransform, QPointF)> alpha_at;
    alpha_at = [&](const Layer& layer, QTransform parent, QPointF p) -> float {
        if (!layer.visible || layer.kind == LayerKind::adjustment)
            return 0;
        const auto world = layer.transform * parent;
        const auto size = layer.pixels.isNull() ? state.size : layer.pixels.size();
        const auto local = warp_point(layer, size, world.inverted().map(p), true);
        if (!local)
            return 0;
        const auto original = world.map(*local);
        Layer mask_layer = layer;
        mask_layer.warped = false;
        mask_layer.mask_transform *= parent;
        const auto mask_position = layer.mask_linked ? original : p;
        float alpha = coverage(mask_layer, int(std::floor(mask_position.x())),
                               int(std::floor(mask_position.y())));
        if (!layer.children.empty()) {
            float combined = 0, base = 0;
            for (const auto& child : layer.children) {
                float value = alpha_at(child, world, original);
                if (child.clipped)
                    value *= base;
                else
                    base = value;
                combined = value + combined * (1 - value);
            }
            return alpha * combined;
        }
        if (layer.kind == LayerKind::fill)
            return QRectF(QPointF(), QSizeF(state.size)).contains(*local)
                       ? alpha * float(layer.fill.alphaF())
                       : 0;
        const QPoint pixel(int(std::floor(local->x())), int(std::floor(local->y())));
        return layer.pixels.rect().contains(pixel)
                   ? alpha * reinterpret_cast<const float*>(
                                 layer.pixels.constScanLine(pixel.y()))[pixel.x() * 4 + 3]
                   : 0;
    };
    std::function<QUuid(const std::vector<Layer>&, QTransform, QPointF)> visit;
    visit = [&](const auto& layers, QTransform parent, QPointF p) -> QUuid {
        for (std::size_t index = layers.size(); index > 0; --index) {
            const auto& layer = layers[index - 1];
            if (layer.locked || !layer.visible)
                continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const QPointF sample = p + QPointF(dx * tolerance, dy * tolerance);
                    float alpha = alpha_at(layer, parent, sample);
                    if (layer.clipped) {
                        std::size_t base = index - 1;
                        while (base > 0 && layers[base].clipped)
                            --base;
                        alpha *= layers[base].clipped ? 0 : alpha_at(layers[base], parent, sample);
                    }
                    if (alpha <= .01F)
                        continue;
                    if (!layer.children.empty()) {
                        const auto world = layer.transform * parent;
                        const auto local =
                            warp_point(layer, state.size, world.inverted().map(sample), true);
                        if (local) {
                            const auto child = visit(layer.children, world, world.map(*local));
                            if (!child.isNull())
                                return child;
                        }
                        continue;
                    }
                    return layer.id;
                }
        }
        return {};
    };
    return visit(state.layers, {}, point);
}
} // namespace huelith::editing
