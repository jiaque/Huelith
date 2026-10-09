#include "huelith/editing/photo_operations.h"
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace huelith::editing {
namespace {
using Pixel = std::array<float, 4>;
Pixel pixel(const QImage& image, int x, int y) {
    if (!image.rect().contains(x, y))
        return {};
    const auto* p = reinterpret_cast<const float*>(image.constScanLine(y)) + 4 * x;
    return {p[0], p[1], p[2], p[3]};
}
QImage blank(QSize size) {
    QImage image(size, QImage::Format_RGBA32FPx4_Premultiplied);
    if (image.isNull())
        throw EditError(QStringLiteral("图像内存不足"));
    image.fill(Qt::transparent);
    image.setColorSpace(working_space());
    return image;
}
Layer& writable(State& state, const QUuid& id) {
    auto* layer = find_layer(state.layers, id);
    if (!layer || layer_locked(state.layers, id))
        throw EditError(QStringLiteral("请选择未锁定的像素或修复图层"));
    if (layer->kind != LayerKind::pixels && layer->kind != LayerKind::repair)
        throw EditError(QStringLiteral("此工具需要像素／修复图层；源对象请先栅格化或新建修复层"));
    if (layer->pixels.isNull())
        layer->pixels = blank(state.size);
    return *layer;
}
float selection_at(const State& state, QPoint p) {
    if (!QRect(QPoint(), state.size).contains(p))
        return 0;
    return state.selection.isNull() ? 1.F : state.selection.constScanLine(p.y())[p.x()] / 255.F;
}
// Separable box kernel in premultiplied linear RGB. Never quantize intermediate pixels.
QImage low_pass(const QImage& image, int radius) {
    auto horizontal = blank(image.size());
    auto result = blank(image.size());
    for (int pass = 0; pass < 2; ++pass) {
        const auto& source = pass == 0 ? image : horizontal;
        auto& destination = pass == 0 ? horizontal : result;
        const int length = pass == 0 ? image.width() : image.height();
        const int lines = pass == 0 ? image.height() : image.width();
        for (int line = 0; line < lines; ++line) {
            std::array<double, 4> sum{};
            const auto read = [&](int position) {
                position = std::clamp(position, 0, length - 1);
                return pass == 0 ? pixel(source, position, line) : pixel(source, line, position);
            };
            for (int k = -radius; k <= radius; ++k) {
                const auto p = read(k);
                for (int c = 0; c < 4; ++c)
                    sum[c] += p[c];
            }
            for (int k = 0; k < length; ++k) {
                auto* dst = reinterpret_cast<float*>(destination.scanLine(pass == 0 ? line : k)) +
                            4 * (pass == 0 ? k : line);
                for (int c = 0; c < 4; ++c)
                    dst[c] = static_cast<float>(sum[c] / (2 * radius + 1));
                const auto subtract = read(k - radius), add = read(k + radius + 1);
                for (int c = 0; c < 4; ++c)
                    sum[c] += double(add[c]) - subtract[c];
            }
        }
    }
    return result;
}
bool restrict_to_target(std::vector<Layer>& layers, const QUuid& id, SampleLayers scope) {
    bool found = false;
    for (auto& layer : layers) {
        if (found) {
            layer.visible = false;
        } else if (layer.id == id) {
            layer.visible = true;
            found = true;
        } else if (!layer.children.empty() && find_layer(layer.children, id)) {
            restrict_to_target(layer.children, id, scope);
            found = true;
        } else if (scope == SampleLayers::current) {
            layer.visible = false;
        }
    }
    return found;
}
} // namespace

QImage sample_layers(const State& state, const QUuid& target, SampleLayers scope) {
    if (scope < SampleLayers::current || scope > SampleLayers::visible)
        throw EditError(QStringLiteral("取样范围无效"));
    if (!find_layer(state.layers, target))
        throw EditError(QStringLiteral("取样图层不存在"));
    auto sampled = state;
    if (scope != SampleLayers::visible)
        restrict_to_target(sampled.layers, target, scope);
    return render(sampled);
}

QImage color_range(const QImage& image, QPoint seed, int tolerance, bool contiguous) {
    if (!image.rect().contains(seed) || tolerance < 0 || tolerance > 255)
        throw EditError(QStringLiteral("颜色取样点或容差无效"));
    const auto display = display_image(image);
    const auto reference = display.pixelColor(seed);
    QImage eligible(image.size(), QImage::Format_Grayscale8);
    eligible.fill(0);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto value = display.pixelColor(x, y);
            const int difference = std::max({std::abs(reference.red() - value.red()),
                                             std::abs(reference.green() - value.green()),
                                             std::abs(reference.blue() - value.blue()),
                                             std::abs(reference.alpha() - value.alpha())});
            if (difference <= tolerance)
                eligible.scanLine(y)[x] = 255;
        }
    if (!contiguous)
        return eligible;
    QImage connected(image.size(), QImage::Format_Grayscale8);
    connected.fill(0);
    std::vector<QPoint> pending{seed};
    connected.scanLine(seed.y())[seed.x()] = 255;
    for (std::size_t i = 0; i < pending.size(); ++i)
        for (const QPoint delta : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
            const auto next = pending[i] + delta;
            if (eligible.rect().contains(next) && eligible.constScanLine(next.y())[next.x()] &&
                !connected.constScanLine(next.y())[next.x()]) {
                connected.scanLine(next.y())[next.x()] = 255;
                pending.push_back(next);
            }
        }
    return connected;
}

void fill_color(State& state, const QUuid& id, QPoint seed, QColor color, int tolerance,
                bool contiguous, bool all_layers, double opacity) {
    if (!color.isValid() || !std::isfinite(opacity) || opacity < 0 || opacity > 1)
        throw EditError(QStringLiteral("填充颜色或透明度无效"));
    const auto sampled =
        sample_layers(state, id, all_layers ? SampleLayers::visible : SampleLayers::current);
    const auto range = color_range(sampled, seed, tolerance, contiguous);
    auto& layer = writable(state, id);
    QImage swatch(1, 1, QImage::Format_RGBA8888);
    swatch.fill(color);
    const auto input = pixel(to_working(swatch), 0, 0);
    const auto matrix = layer.transform * parent_matrix(state, id);
    for (int y = 0; y < layer.pixels.height(); ++y) {
        auto* row = reinterpret_cast<float*>(layer.pixels.scanLine(y));
        for (int x = 0; x < layer.pixels.width(); ++x) {
            const auto warped =
                warp_point(layer, layer.pixels.size(), QPointF(x + .5, y + .5), false);
            if (!warped)
                continue;
            const auto position = matrix.map(*warped);
            const QPoint point(int(std::floor(position.x())), int(std::floor(position.y())));
            if (!range.rect().contains(point))
                continue;
            const float k = float(opacity) * range.constScanLine(point.y())[point.x()] / 255.F *
                            selection_at(state, point);
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] = row[x * 4 + c] * (1 - k * input[3]) + input[c] * k;
        }
    }
}

void retouch(State& state, const QUuid& id, const QImage& coverage, const RetouchOptions& options) {
    if (coverage.size() != state.size || coverage.format() != QImage::Format_Grayscale8 ||
        !std::isfinite(options.radius) || options.radius < 1 || options.radius > 500 ||
        !std::isfinite(options.strength) || options.strength < 0 || options.strength > 1 ||
        !std::isfinite(options.source_offset.x()) || !std::isfinite(options.source_offset.y()) ||
        options.operation < Retouch::clone || options.operation > Retouch::sponge)
        throw EditError(QStringLiteral("修饰笔画参数无效"));
    const QImage source = sample_layers(state, id, options.sample);
    const auto operation = options.operation;
    const QImage low = operation == Retouch::clone || operation == Retouch::dodge ||
                               operation == Retouch::burn || operation == Retouch::sponge
                           ? QImage{}
                           : low_pass(source, std::clamp(int(options.radius / 2), 1, 64));
    auto& layer = writable(state, id);
    const auto matrix = layer.transform * parent_matrix(state, id);
    for (int y = 0; y < layer.pixels.height(); ++y) {
        auto* row = reinterpret_cast<float*>(layer.pixels.scanLine(y));
        for (int x = 0; x < layer.pixels.width(); ++x) {
            const auto warped =
                warp_point(layer, layer.pixels.size(), QPointF(x + .5, y + .5), false);
            if (!warped)
                continue;
            const auto position = matrix.map(*warped);
            const QPoint p(int(std::floor(position.x())), int(std::floor(position.y())));
            if (!coverage.rect().contains(p))
                continue;
            const float k = coverage.constScanLine(p.y())[p.x()] / 255.F * selection_at(state, p) *
                            float(options.strength);
            if (k == 0)
                continue;
            const auto original = pixel(source, p.x(), p.y());
            auto value = original;
            if (operation == Retouch::clone || operation == Retouch::heal) {
                const auto point = QPointF(p) + options.source_offset;
                const QPoint q(int(std::floor(point.x())), int(std::floor(point.y())));
                if (!source.rect().contains(q))
                    continue;
                value = pixel(source, q.x(), q.y());
                if (operation == Retouch::heal) {
                    const auto src_low = pixel(low, q.x(), q.y()),
                               dst_low = pixel(low, p.x(), p.y());
                    for (int c = 0; c < 3; ++c)
                        value[c] = std::max(0.F, value[c] + dst_low[c] - src_low[c]);
                    value[3] = original[3];
                }
            } else if (operation == Retouch::spot) {
                // Choose an unpainted surrounding sample; protected pixels are never a source.
                double best = std::numeric_limits<double>::infinity();
                const auto target_low = pixel(low, p.x(), p.y());
                for (int direction = 0; direction < 16; ++direction) {
                    const double angle = direction * 6.283185307179586 / 16;
                    const QPoint q = p + QPoint(int(std::cos(angle) * options.radius * 3),
                                                int(std::sin(angle) * options.radius * 3));
                    if (!source.rect().contains(q) || coverage.constScanLine(q.y())[q.x()])
                        continue;
                    const auto candidate = pixel(source, q.x(), q.y());
                    if (candidate[3] < .99F)
                        continue;
                    const auto candidate_low = pixel(low, q.x(), q.y());
                    double error{};
                    for (int c = 0; c < 3; ++c)
                        error += std::pow(double(candidate_low[c] - target_low[c]), 2);
                    if (error < best) {
                        best = error;
                        value = candidate;
                    }
                }
            } else if (operation == Retouch::blur || operation == Retouch::sharpen) {
                const auto smooth = pixel(low, p.x(), p.y());
                for (int c = 0; c < 3; ++c) {
                    const float same_alpha =
                        smooth[3] > 0 ? smooth[c] / smooth[3] * original[3] : 0;
                    value[c] = operation == Retouch::blur
                                   ? same_alpha
                                   : std::max(0.F, original[c] * 2 - same_alpha);
                }
            } else if (operation == Retouch::sponge) {
                const float luminance =
                    original[0] * .2627F + original[1] * .678F + original[2] * .0593F;
                for (int c = 0; c < 3; ++c)
                    value[c] = luminance;
            } else {
                const float gain = operation == Retouch::dodge ? 2.F : .5F;
                for (int c = 0; c < 3; ++c)
                    value[c] *= gain;
            }
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] = row[x * 4 + c] * (1 - k) + value[c] * k;
        }
    }
}

void crop_canvas(State& state, const QRect& bounds) {
    if (bounds.isEmpty() || qint64(bounds.width()) * bounds.height() > 64'000'000)
        throw EditError(QStringLiteral("裁剪尺寸无效"));
    const auto delta = QTransform::fromTranslate(-bounds.x(), -bounds.y());
    for (auto& layer : state.layers) {
        layer.transform *= delta;
        layer.mask_transform *= delta;
    }
    const auto crop_range = [&](QImage& image) {
        if (image.isNull())
            return;
        QImage output(bounds.size(), image.format());
        output.fill(0);
        // Grayscale images cannot be QPainter targets.
        const auto overlap = bounds.intersected(image.rect());
        for (int y = overlap.top(); y <= overlap.bottom(); ++y)
            for (int x = overlap.left(); x <= overlap.right(); ++x)
                output.scanLine(y - bounds.y())[x - bounds.x()] = image.constScanLine(y)[x];
        image = output;
    };
    crop_range(state.selection);
    for (auto& range : state.saved_selections)
        crop_range(range);
    for (auto& path : state.paths)
        path = delta.map(path);
    for (auto& guide : state.horizontal_guides)
        guide -= bounds.y();
    for (auto& guide : state.vertical_guides)
        guide -= bounds.x();
    state.snapshots.clear();
    state.size = bounds.size();
}
} // namespace huelith::editing
