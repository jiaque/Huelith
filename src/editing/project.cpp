#include <cstring>
#include "huelith/editing/image_storage.h"
#include "huelith/editing/project.h"
#include "huelith/editing/tiff_io.h"
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace huelith::editing {
namespace {
constexpr qint64 max_pixels = huelith::editing::maximum_image_pixels;
void persist_state(State& state) {
    std::function<void(std::vector<Layer>&)> visit=[&](auto& layers) {
        for(auto& layer:layers) {
            persist_image(layer.pixels);persist_image(layer.mask);
            for(auto& effect:layer.effects){persist_image(effect.mask);persist_image(effect.liquify);}
            visit(layer.children);
        }
    };
    visit(state.layers);persist_image(state.selection);
    for(auto& image:state.saved_selections)persist_image(image);
    for(auto& image:state.snapshots)persist_image(image);
}
void check_cancelled(const std::atomic_bool* cancelled) {
    if (cancelled && cancelled->load(std::memory_order_relaxed))
        throw EditError(QStringLiteral("渲染已取消"));
}
QImage blank(QSize size) {
    auto image = allocate_image(size, QImage::Format_RGBA32FPx4_Premultiplied);
    if (image.isNull())
        throw EditError(QStringLiteral("无法分配图像内存"));
    image.fill(Qt::transparent);
    image.setColorSpace(working_space());
    return image;
}
void check_size(QSize size) {
    if (size.isEmpty() || size.width() > 65536 || size.height() > 65536 ||
        qint64(size.width()) * size.height() > max_pixels)
        throw EditError(QStringLiteral("图片尺寸超出当前 5.12 亿像素预算"));
}
float coverage(const Layer& layer, int x, int y) {
    float value = layer.opacity;
    QPointF position(x + .5, y + .5);
    if (layer.warped && layer.mask_linked && (!layer.mask.isNull() || layer.vector_enabled)) {
        const auto local = warp_point(layer, layer_source_size(layer, layer.mask.size()),
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
void composite(QImage& target, const QImage& source, const Layer& layer, const QImage& clip,
               const std::atomic_bool* cancelled) {
    check_cancelled(cancelled);
    const bool artistic = layer.blend != Blend::normal;
    QImage destination = artistic
                             ? convert_linear_rgb(target, QColorSpace::SRgbLinear, cancelled)
                             : std::move(target);
    const QImage input = artistic
                             ? convert_linear_rgb(source, QColorSpace::SRgbLinear, cancelled)
                             : source;
    if(destination.sizeInBytes()>=mapped_image_threshold && !destination.isDetached())
        destination=copy_image(destination);
    for (int y = 0; y < destination.height(); ++y) {
        if ((y & 15) == 0)
            check_cancelled(cancelled);
        auto* dst = reinterpret_cast<float*>(destination.scanLine(y));
        const auto* src = reinterpret_cast<const float*>(input.constScanLine(y));
        const auto* base =
            clip.isNull() ? nullptr : reinterpret_cast<const float*>(clip.constScanLine(y));
        for (int x = 0; x < destination.width(); ++x) {
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
    target = artistic ? convert_linear_rgb(destination, working_space(), cancelled)
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
QImage style_image(const QImage& content, const std::vector<float>& mask, const LayerStyle& style) {
    auto result = blank(content.size());
    const auto color = working_color(style.color);
    for (int y = 0; y < content.height(); ++y) {
        auto* row = reinterpret_cast<float*>(result.scanLine(y));
        const auto* source = reinterpret_cast<const float*>(content.constScanLine(y));
        for (int x = 0; x < content.width(); ++x) {
            const float alpha = mask[std::size_t(y) * content.width() + x] *
                                float(style.opacity * color.alphaF()) * (1 - source[x * 4 + 3]);
            row[x * 4] = alpha * float(color.redF());
            row[x * 4 + 1] = alpha * float(color.greenF());
            row[x * 4 + 2] = alpha * float(color.blueF());
            row[x * 4 + 3] = alpha;
        }
    }
    return result;
}
void style_interior(QImage& content, const std::vector<float>& mask, const LayerStyle& style) {
    const bool artistic = style.blend != Blend::normal;
    QImage working = artistic
                         ? content.convertedToColorSpace(QColorSpace::SRgbLinear,
                                                         QImage::Format_RGBA32FPx4_Premultiplied)
                         : content;
    QImage swatch(1, 1, QImage::Format_RGBA8888);
    swatch.fill(style.color);
    swatch.setColorSpace(QColorSpace::SRgb);
    swatch =
        swatch.convertedToColorSpace(working.colorSpace(), QImage::Format_RGBA32FPx4_Premultiplied);
    const auto* pixel = reinterpret_cast<const float*>(swatch.constBits());
    if (pixel[3] == 0)
        return;
    Color color{pixel[0] / pixel[3], pixel[1] / pixel[3], pixel[2] / pixel[3]};
    for (int y = 0; y < working.height(); ++y) {
        auto* row = reinterpret_cast<float*>(working.scanLine(y));
        for (int x = 0; x < working.width(); ++x) {
            auto* p = row + x * 4;
            if (p[3] <= 0)
                continue;
            const float k =
                std::clamp(mask[std::size_t(y) * working.width() + x] / p[3], 0.F, 1.F) *
                float(style.opacity) * pixel[3];
            Color term = color;
            if (artistic) {
                term = artistic_term(
                    {encode(color[0]), encode(color[1]), encode(color[2])},
                    {encode(p[0] / p[3]), encode(p[1] / p[3]), encode(p[2] / p[3])}, style.blend);
                for (auto& value : term)
                    value = decode(value);
            }
            for (int c = 0; c < 3; ++c)
                p[c] = std::lerp(p[c], term[std::size_t(c)] * p[3], k);
        }
    }
    content = artistic ? working.convertedToColorSpace(working_space(),
                                                       QImage::Format_RGBA32FPx4_Premultiplied)
                       : working;
}
void styled_composite(QImage& target, QImage content, const Layer& layer, const QImage& clip,
                      double scale, double global_angle, const std::atomic_bool* cancelled) {
    const auto& styles = layer.styles;
    const auto original = content;
    for (const int index : {1, 3, 0}) {
        const auto& style = styles.entries[index];
        if (!style.enabled || (index == 0 && style.position == 1))
            continue;
        const auto mask =
            style_coverage(original, static_cast<StyleKind>(index), style, global_angle, scale);
        auto effect_layer = layer;
        effect_layer.blend = style.blend;
        composite(target, style_image(original, mask, style), effect_layer, clip, cancelled);
    }
    for (const int index : {5, 2, 4, 0}) {
        const auto& style = styles.entries[index];
        if (!style.enabled || (index == 0 && style.position == 0))
            continue;
        const auto mask =
            style_coverage(original, static_cast<StyleKind>(index), style, global_angle, scale);
        style_interior(content, mask, style);
    }
    composite(target, content, layer, clip, cancelled);
}
void render_layers(QImage& target, const std::vector<Layer>& layers, const RenderOptions& options) {
    QImage base;
    for (std::size_t index = 0; index < layers.size(); ++index) {
        check_cancelled(options.cancelled);
        const auto& layer = layers[index];
        bool needs_base = false;
        for (std::size_t next = index + 1; next < layers.size(); ++next) {
            if (layers[next].clipped) {
                needs_base = true;
                break;
            }
            if (layers[next].kind != LayerKind::adjustment)
                break;
        }
        if (!layer.visible) {
            if (!layer.clipped)
                base = blank(target.size());
            continue;
        }
        if (layer.clipped && base.isNull())
            continue;
        if (layer.kind == LayerKind::adjustment) {
            const float gain = static_cast<float>(std::exp2(layer.exposure));
            const auto toned = apply_effects(
                apply_tone(target, layer.tone, options.cancelled, options.pixel_scale,
                           layer.id == options.detail_layer ? options.detail_preview : 0),
                layer.effects, options.cancelled);
            if(target.sizeInBytes()>=mapped_image_threshold && !target.isDetached())
                target=copy_image(target);
            for (int y = 0; y < target.height(); ++y) {
                if ((y & 15) == 0)
                    check_cancelled(options.cancelled);
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
            layer.blend == Blend::normal && !layer.warped && layer.effects.empty() &&
            !layer.styles.active()) {
            const QImage before = target;
            if(target.sizeInBytes()>=mapped_image_threshold)target=copy_image(target);
            auto children = layer.children;
            transform_children(children, layer.transform);
            render_layers(target, children, options);
            for (int y = 0; y < target.height(); ++y) {
                if ((y & 15) == 0)
                    check_cancelled(options.cancelled);
                auto* row = reinterpret_cast<float*>(target.scanLine(y));
                const auto* old = reinterpret_cast<const float*>(before.constScanLine(y));
                for (int x = 0; x < target.width(); ++x)
                    for (int c = 0; c < 4; ++c)
                        row[4 * x + c] = old[4 * x + c] +
                                         coverage(layer, x, y) * (row[4 * x + c] - old[4 * x + c]);
            }
            // Clipping uses the group's intrinsic alpha, never the external backdrop.
            if (needs_base) {
                base = blank(target.size());
                render_layers(base, children, options);
                QImage masked = blank(target.size());
                composite(masked, base, layer, {}, options.cancelled);
                base = masked;
            }
            continue;
        }
        QImage local = blank(target.size());
        if (layer.kind == LayerKind::group || layer.kind == LayerKind::composition) {
            render_layers(local, layer.children, options);
            local = apply_effects(local, layer.effects, options.cancelled);
            if (!layer.transform.isIdentity() || layer.warped) {
                QImage transformed = blank(target.size());
                QPainter painter(&transformed);
                painter.setTransform(layer.transform);
                paint_warp(painter, local, layer);
                painter.end();
                local = transformed;
            }
        } else if (layer.kind == LayerKind::text || layer.kind == LayerKind::shape) {
            const auto size =
                layer.warped ? layer_source_size(layer, target.size()) : target.size();
            check_size(size);
            QImage design(size, QImage::Format_RGBA64_Premultiplied);
            design.setDotsPerMeterX(3780);
            design.setDotsPerMeterY(3780);
            design.setColorSpace(QColorSpace::SRgb);
            design.fill(Qt::transparent);
            QPainter painter(&design);
            if (!layer.warped)
                painter.setTransform(layer.transform);
            paint_design(painter, layer);
            painter.end();
            auto rendered = to_working(std::move(design));
            if (layer.warped) {
                QPainter warp(&local);
                warp.setTransform(layer.transform);
                paint_warp(warp, rendered, layer);
                warp.end();
            } else
                local = std::move(rendered);
            local = apply_effects(local, layer.effects, options.cancelled);
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
                paint_warp(painter, apply_effects(layer.pixels, layer.effects, options.cancelled),
                           layer);
            painter.end();
            if (layer.kind == LayerKind::fill)
                local = apply_effects(local, layer.effects, options.cancelled);
        }
        if (layer.exposure != 0) {
            const float gain = static_cast<float>(std::exp2(layer.exposure));
            for (int y = 0; y < local.height(); ++y) {
                if ((y & 15) == 0)
                    check_cancelled(options.cancelled);
                auto* row = reinterpret_cast<float*>(local.scanLine(y));
                for (int x = 0; x < local.width(); ++x)
                    for (int c = 0; c < 3; ++c)
                        row[4 * x + c] *= gain;
            }
        }
        local = apply_tone(local, layer.tone, options.cancelled, options.pixel_scale,
                           layer.id == options.detail_layer ? options.detail_preview : 0);
        if (layer.styles.active())
            styled_composite(target, local, layer, layer.clipped ? base : QImage{},
                             options.pixel_scale, options.global_light_angle, options.cancelled);
        else
            composite(target, local, layer, layer.clipped ? base : QImage{}, options.cancelled);
        if (!layer.clipped && needs_base) {
            base = blank(target.size());
            composite(base, local, layer, {}, options.cancelled);
        }
    }
}
void validate_layers(const std::vector<Layer>& layers, QSet<QUuid>& ids, int depth) {
    if (depth > 32)
        throw EditError(QStringLiteral("图层嵌套超过 32 层"));
    for (const auto& layer : layers) {
        validate_tone(layer.tone);
        validate_effects(layer.effects);
        validate_styles(layer.styles);
        if (layer.frame &&
            (layer.kind != LayerKind::group || layer.frame->isEmpty() ||
             !std::isfinite(layer.frame->x()) || !std::isfinite(layer.frame->y()) ||
             !std::isfinite(layer.frame->width()) || !std::isfinite(layer.frame->height())))
            throw EditError(QStringLiteral("相框参数无效"));
        if (layer.kind == LayerKind::text)
            validate_text(layer.text);
        if (layer.kind == LayerKind::shape)
            validate_shape(layer.shape);
        if (layer.background &&
            (depth != 0 || &layer != &layers.front() || layer.kind != LayerKind::pixels ||
             !layer.transform.isIdentity() || layer.warped || layer.opacity != 1.F ||
             layer.blend != Blend::normal || layer.clipped || !layer.mask.isNull() ||
             layer.vector_enabled || !layer.children.empty()))
            throw EditError(
                QStringLiteral("背景图层固定在底部；请先转换为普通图层再变换或添加透明度"));
        if (int(layer.kind) < 0 || int(layer.kind) > int(LayerKind::shape) ||
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
QImage convert_linear_rgb(const QImage& source, const QColorSpace& destination,
                          const std::atomic_bool* cancelled) {
    if (source.colorSpace() == destination) return source;
    const bool to_srgb = destination == QColorSpace(QColorSpace::SRgbLinear);
    if ((source.colorSpace() != working_space() && source.colorSpace() != QColorSpace(QColorSpace::SRgbLinear)) ||
        (destination != working_space() && !to_srgb) || source.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        return convert_image(source, destination, QImage::Format_RGBA32FPx4_Premultiplied);
    // D65 RGB primary matrices. Both spaces are linear; premultiplication
    // commutes with this transform, including negative and scene-HDR samples.
    constexpr std::array<std::array<double, 3>, 3> from2020{
        {{1.660491002108434, -.58764113878855, -.072849863319884},
         {-.124550474521591, 1.13289989712596, -.008349422604369},
         {-.018150763354906, -.100578898008007, 1.118729661362913}}};
    constexpr std::array<std::array<double, 3>, 3> to2020{
        {{.627403895934699, .329283038377883, .043313065687418},
         {.069097289358232, .919540395075459, .011362315566309},
         {.01639143887515, .088013307877226, .895595253247624}}};
    const auto& matrix = to_srgb ? from2020 : to2020;
    auto result = copy_image(source);
    result.setColorSpace(destination);
    for (int y = 0; y < result.height(); ++y) {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("颜色转换已取消"));
        auto* row = reinterpret_cast<float*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            auto* p = row + x * 4;
            const std::array<double, 3> rgb{p[0], p[1], p[2]};
            for (int c = 0; c < 3; ++c)
                p[c] = float(matrix[c][0] * rgb[0] + matrix[c][1] * rgb[1] + matrix[c][2] * rgb[2]);
        }
    }
    return result;
}
QImage to_working(QImage image) {
    check_size(image.size());
    if (!image.colorSpace().isValid())
        image.setColorSpace(QColorSpace::SRgb);
    auto result =
        convert_linear_rgb(image, working_space());
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
    // The decoder's default 256 MiB guard is smaller than a valid large PNG.
    // Dimensions are checked above; decoded pixels are migrated to scratch storage.
    QImageReader::setAllocationLimit(8192);
    auto image = reader.read();
    if (image.isNull())
        throw EditError(reader.errorString());
    return to_working(std::move(image));
}
QImage display_image(const QImage& image, const QColorSpace& output) {
    if (!output.isValid())
        throw EditError(QStringLiteral("显示配置文件无效"));
    // Qt 6.10's combined FP32-premultiplied/color-space conversion can retain
    // associated RGB values in the straight-alpha result. Unassociate first so
    // translucent pixels have the same displayed color as their opaque peers.
    if (image.format() == QImage::Format_RGBA32FPx4_Premultiplied)
        return convert_image(image, output, QImage::Format_RGBA8888);
    return convert_image(image, output, QImage::Format_RGBA8888);
}
QImage render(const State& state, const RenderOptions& options) {
    check_cancelled(options.cancelled);
    validate_state(state);
    if (!std::isfinite(state.global_light_angle) || std::abs(state.global_light_angle) > 360)
        throw EditError(QStringLiteral("全局光角度无效"));
    auto result = blank(state.size);
    auto rendering = options;
    rendering.global_light_angle = state.global_light_angle;
    const auto tile_safe=[](const Layer& layer) {
        auto tone=layer.tone;tone.exposure=0;
        return layer.children.empty() && layer.effects.empty() && !layer.styles.active() &&
            !layer.gradient && !layer.warped && tone==ToneSettings{} &&
            (layer.kind==LayerKind::pixels || layer.kind==LayerKind::source ||
             layer.kind==LayerKind::repair || layer.kind==LayerKind::adjustment);
    };
    if(qint64(state.size.width())*state.size.height()>8'000'000 &&
       std::all_of(state.layers.begin(),state.layers.end(),tile_safe)) {
        constexpr int edge=512;
        for(int y=0;y<state.size.height();y+=edge) for(int x=0;x<state.size.width();x+=edge) {
            check_cancelled(options.cancelled);
            auto layers=state.layers;
            const auto shift=QTransform::fromTranslate(-x,-y);
            for(auto& layer:layers) {layer.transform*=shift;layer.mask_transform*=shift;}
            auto tile=blank({std::min(edge,state.size.width()-x),std::min(edge,state.size.height()-y)});
            render_layers(tile,layers,rendering);
            for(int row=0;row<tile.height();++row)
                std::memcpy(result.scanLine(y+row)+qint64(x)*16,tile.constScanLine(row),std::size_t(tile.width())*16);
        }
        return result;
    }
    render_layers(result, state.layers, rendering);
    check_cancelled(options.cancelled);
    return result;
}
State preview_state(const State& state, int maximum_edge) {
    if (maximum_edge < 1)
        throw EditError(QStringLiteral("预览尺寸无效"));
    auto preview = state;
    const double scale =
        std::min(1., double(maximum_edge) / std::max(state.size.width(), state.size.height()));
    if (scale == 1)
        return preview;
    const QTransform shrink = QTransform::fromScale(scale, scale);
    const auto grow = shrink.inverted();
    const auto resize = [scale](QImage& image) {
        if (!image.isNull())
            image = image.scaled(std::max(1, qRound(image.width() * scale)),
                                 std::max(1, qRound(image.height() * scale)), Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
    };
    std::function<void(std::vector<Layer>&)> visit = [&](std::vector<Layer>& layers) {
        for (auto& layer : layers) {
            resize(layer.pixels);
            resize(layer.mask);
            layer.vector_mask = shrink.map(layer.vector_mask);
            if (layer.frame)
                layer.frame = shrink.mapRect(*layer.frame);
            layer.transform = (layer.kind == LayerKind::text || layer.kind == LayerKind::shape)
                                  ? layer.transform * shrink
                                  : grow * layer.transform * shrink;
            layer.mask_transform = grow * layer.mask_transform * shrink;
            for (auto& effect : layer.effects) {
                resize(effect.mask);
                effect.blur_radius *= scale;
            }
            visit(layer.children);
        }
    };
    preview.size = QSize(std::max(1, qRound(state.size.width() * scale)),
                         std::max(1, qRound(state.size.height() * scale)));
    resize(preview.selection);
    visit(preview.layers);
    preview.snapshots.clear();
    preview.saved_selections.clear();
    return preview;
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
    qint64 metadata_bytes=QJsonDocument(state.metadata).toJson(QJsonDocument::Compact).size();
    std::function<void(const std::vector<Layer>&)> count_metadata=[&](const auto& layers){
        for(const auto& layer:layers){metadata_bytes+=QJsonDocument(layer.source_metadata).toJson(QJsonDocument::Compact).size();count_metadata(layer.children);}
    };
    count_metadata(state.layers);
    if(metadata_bytes>32*1024*1024)throw EditError(QStringLiteral("工程元数据超过 32 MiB"));
    if (state.name.trimmed().isEmpty() || !std::isfinite(state.ppi) || state.ppi <= 0 ||
        !std::isfinite(state.global_light_angle) || std::abs(state.global_light_angle) > 360)
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
    persist_state(state_);
    history_.push_back({state_, QStringLiteral("初始状态"), revision_});
}
void Project::edit(const QString& name, const std::function<void(State&)>& operation) {
    State draft = state_;
    operation(draft);
    validate_state(draft);
    persist_state(draft);
    const quint64 next = next_revision_++;
    history_.resize(position_ + 1);
    history_.push_back({draft, name, next});
    state_ = std::move(draft);
    revision_ = next;
    ++position_;
    set_history_limit(history_limit_);
}
void Project::set_history_limit(std::size_t count) {
    history_limit_ = std::clamp(count, std::size_t(2), std::size_t(1000));
    if (history_.size() <= history_limit_)
        return;
    const auto first = position_ >= history_limit_ ? position_ - history_limit_ + 1 : 0;
    const auto last = std::min(history_.size(), first + history_limit_);
    history_.erase(history_.begin() + last, history_.end());
    history_.erase(history_.begin(), history_.begin() + first);
    position_ -= first;
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
QSize layer_source_size(const Layer& layer, QSize fallback) {
    if (layer.kind == LayerKind::text || layer.kind == LayerKind::shape) {
        const auto bounds = design_bounds(layer);
        return QSize(std::max(1, int(std::ceil(bounds.right()))),
                     std::max(1, int(std::ceil(bounds.bottom()))));
    }
    return layer.pixels.isNull() ? fallback : layer.pixels.size();
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
    const QRectF rect = layer->kind == LayerKind::text || layer->kind == LayerKind::shape
                            ? design_bounds(*layer)
                        : layer->pixels.isNull() ? QRectF(QPointF(), QSizeF(state.size))
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
        const auto size = layer_source_size(layer, state.size);
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
        if (layer.kind == LayerKind::text || layer.kind == LayerKind::shape) {
            QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
            probe.fill(Qt::transparent);
            QPainter painter(&probe);
            painter.translate(-std::floor(local->x()), -std::floor(local->y()));
            paint_design(painter, layer);
            painter.end();
            return alpha * float(qAlpha(probe.pixel(0, 0))) / 255.F;
        }
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
