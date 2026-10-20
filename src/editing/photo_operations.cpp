#include "huelith/editing/image_storage.h"
#include "huelith/editing/photo_operations.h"
#include "huelith/editing/healing.h"
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
    auto image = allocate_image(size, QImage::Format_RGBA32FPx4_Premultiplied);
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
    if(!layer->pixels.isDetached() && layer->pixels.sizeInBytes()>=mapped_image_threshold)
        layer->pixels=copy_image(layer->pixels);
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

QPointF clone_sample_position(const RetouchOptions& options, QPointF point) {
    const auto relative = point - options.source_anchor;
    const double angle = options.source_rotation * std::acos(-1.) / 180.;
    return options.source_anchor + options.source_offset +
           QPointF((relative.x()*std::cos(angle)+relative.y()*std::sin(angle)) / options.source_scale_x * (options.source_flip_x ? -1 : 1),
                   (-relative.x()*std::sin(angle)+relative.y()*std::cos(angle)) / options.source_scale_y * (options.source_flip_y ? -1 : 1));
}
void retouch(State& state, const QUuid& id, const QImage& coverage, const RetouchOptions& options,
             const QImage* sampled_source) {
    if (coverage.size() != state.size || coverage.format() != QImage::Format_Grayscale8 ||
        !std::isfinite(options.radius) || options.radius < 1 || options.radius > 2500 ||
        !std::isfinite(options.color_adaptation) || options.color_adaptation < 0 ||
        options.color_adaptation > 1 || !std::isfinite(options.strength) || options.strength < 0 ||
        options.strength > 1 || !std::isfinite(options.source_offset.x()) ||
        !std::isfinite(options.source_offset.y()) || options.operation < Retouch::clone ||
        options.operation > Retouch::sponge || options.tone_range<0 || options.tone_range>2 ||
        !std::isfinite(options.source_scale_x) || options.source_scale_x < .01 || options.source_scale_x > 100 ||
        !std::isfinite(options.source_scale_y) || options.source_scale_y < .01 || options.source_scale_y > 100 ||
        !std::isfinite(options.source_rotation) || std::abs(options.source_rotation) > 180 ||
        !std::isfinite(options.source_anchor.x()) || !std::isfinite(options.source_anchor.y()))
        throw EditError(QStringLiteral("修饰笔画参数无效"));
    int left=coverage.width(),top=coverage.height(),right=-1,bottom=-1;
    for(int y=0;y<coverage.height();++y) {
        const auto* row=coverage.constScanLine(y);
        for(int x=0;x<coverage.width();++x) if(row[x]) {
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=y;
        }
    }
    if(right<left)return;
    const QRect affected(QPoint(left,top),QPoint(right,bottom));
    const QImage source = sampled_source ? *sampled_source : sample_layers(state, id, options.sample);
    if(source.size()!=state.size || source.format()!=QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("取样图像与文档不匹配"));
    const auto operation = options.operation;
    QImage healing;
    auto offset = options.source_offset;
    if (operation == Retouch::heal || operation == Retouch::spot) {
        auto mask = coverage;
        if (!state.selection.isNull())
            for (int y = 0; y < mask.height(); ++y) {
                auto* row = mask.scanLine(y);
                const auto* selection = state.selection.constScanLine(y);
                for (int x = 0; x < mask.width(); ++x)
                    row[x] = uchar(int(row[x]) * selection[x] / 255);
            }
        if (operation == Retouch::spot)
            offset = automatic_patch_offset(source, mask);
        healing = patch_replacement(source, mask, offset, options.color_adaptation);
    }
    const QImage low = operation == Retouch::clone || operation == Retouch::heal ||
                               operation == Retouch::spot || operation == Retouch::dodge ||
                               operation == Retouch::burn || operation == Retouch::sponge
                           ? QImage{}
                           : low_pass(source, std::clamp(int(options.radius / 2), 1, 64));
    auto& layer = writable(state, id);
    const auto matrix = layer.transform * parent_matrix(state, id);
    bool invertible=false;
    const auto inverse=matrix.inverted(&invertible);
    const auto local_bounds=layer.warped || !invertible ? layer.pixels.rect() :
        inverse.mapRect(QRectF(affected).adjusted(-1,-1,1,1)).toAlignedRect().intersected(layer.pixels.rect());
    for (int y = local_bounds.top(); y <= local_bounds.bottom(); ++y) {
        auto* row = reinterpret_cast<float*>(layer.pixels.scanLine(y));
        for (int x = local_bounds.left(); x <= local_bounds.right(); ++x) {
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
            if (operation == Retouch::heal || operation == Retouch::spot) {
                value = pixel(healing, p.x(), p.y());
            } else if (operation == Retouch::clone) {
                const auto point = clone_sample_position(options, QPointF(p));
                const QPoint q(int(std::floor(point.x())), int(std::floor(point.y())));
                if (!source.rect().contains(q))
                    continue;
                value = pixel(source, q.x(), q.y());
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
                const float maximum=std::max({original[0],original[1],original[2]}),minimum=std::min({original[0],original[1],original[2]});
                const float saturation=maximum>1e-8F?std::clamp((maximum-minimum)/maximum,0.F,1.F):0.F;
                const float amount=(options.sponge_saturate?1.F:-1.F)*(options.sponge_vibrance?1.F-.75F*saturation:1.F);
                for (int c = 0; c < 3; ++c)
                    value[c] = original[c]+amount*(original[c]-luminance);
            } else {
                if(original[3]<=0)continue;
                const double luminance=(.2627*original[0]+.6780*original[1]+.0593*original[2])/original[3];
                const double encoded=std::pow(std::clamp(luminance,0.,1.),1/2.2);
                const auto smooth=[](double low,double high,double v){const double t=std::clamp((v-low)/(high-low),0.,1.);return t*t*(3-2*t);};
                const double m=smooth(.1,.9,encoded);
                const double weight=options.tone_range==0?1-smooth(.15,.65,encoded):options.tone_range==2?smooth(.35,.85,encoded):4*m*(1-m);
                const double gain=std::exp2((operation==Retouch::dodge?1.:-1.)*weight);
                double target=luminance*gain;
                if(options.protect_tones && luminance>=0)
                    target=luminance>1?1+(luminance-1)*gain:luminance*gain/(1+luminance*(gain-1));
                const float multiplier=luminance>1e-12?float(target/luminance):float(gain);
                for (int c = 0; c < 3; ++c)
                    value[c] *= multiplier;
            }
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] =
                    row[x * 4 + c] * (1 - k * (layer.background ? value[3] : 1.F)) + value[c] * k;
        }
    }
}

void patch_selection(State& state, const QUuid& target, QPointF drag, bool destination,
                     SampleLayers scope, double adaptation) {
    if (state.selection.isNull())
        throw EditError("请先圈选需要修补的区域");
    const QImage selected = state.selection;
    QImage coverage = selected;
    if (destination) {
        coverage.fill(0);
        const QPoint shift(qRound(drag.x()), qRound(drag.y()));
        for (int y = 0; y < selected.height(); ++y) {
            const int dy = y + shift.y();
            if (dy < 0 || dy >= coverage.height())
                continue;
            auto* row = coverage.scanLine(dy);
            for (int x = 0; x < selected.width(); ++x) {
                const int dx = x + shift.x();
                if (dx >= 0 && dx < coverage.width())
                    row[dx] = selected.constScanLine(y)[x];
            }
        }
    }
    RetouchOptions options;
    options.operation = Retouch::heal;
    options.sample = scope;
    options.source_offset = destination ? -drag : drag;
    options.color_adaptation = adaptation;
    state.selection = {};
    try {
        retouch(state, target, coverage, options);
    } catch (...) {
        state.selection = selected;
        throw;
    }
    state.selection = destination ? coverage : selected;
}

void paint_replacement(State& state, const QUuid& id, const QImage& replacement,
                       const QImage& coverage) {
    if (replacement.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        replacement.size() != state.size || coverage.size() != state.size ||
        coverage.format() != QImage::Format_Grayscale8)
        throw EditError("填充结果格式无效");
    auto& layer = writable(state, id);
    const auto matrix = layer.transform * parent_matrix(state, id);
    for (int y = 0; y < layer.pixels.height(); ++y) {
        auto* row = reinterpret_cast<float*>(layer.pixels.scanLine(y));
        for (int x = 0; x < layer.pixels.width(); ++x) {
            const auto warped =
                warp_point(layer, layer.pixels.size(), QPointF(x + .5, y + .5), false);
            if (!warped)
                continue;
            const auto mapped = matrix.map(*warped);
            const QPoint p(int(std::floor(mapped.x())), int(std::floor(mapped.y())));
            if (!coverage.rect().contains(p))
                continue;
            const float amount = coverage.constScanLine(p.y())[p.x()] / 255.F;
            if (amount == 0)
                continue;
            const auto value = pixel(replacement, p.x(), p.y());
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] =
                    row[x * 4 + c] * (1 - amount * (layer.background ? value[3] : 1.F)) +
                    value[c] * amount;
        }
    }
}

void crop_canvas(State& state, const QRect& bounds, double angle, bool delete_pixels) {
    if (bounds.isEmpty() || qint64(bounds.width()) * bounds.height() > huelith::editing::maximum_image_pixels || !std::isfinite(angle) || std::abs(angle)>180)
        throw EditError(QStringLiteral("裁剪尺寸无效"));
    QTransform rotation;rotation.translate(state.size.width()/2.,state.size.height()/2.);rotation.rotate(angle);rotation.translate(-state.size.width()/2.,-state.size.height()/2.);
    const auto delta = rotation*QTransform::fromTranslate(-bounds.x(), -bounds.y());
    for (auto& layer : state.layers) {
        if (layer.background && delete_pixels) {
            QImage cropped(bounds.size(), layer.pixels.format());
            cropped.setColorSpace(layer.pixels.colorSpace());
            cropped.fill(Qt::white);
            QPainter painter(&cropped);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setTransform(delta);
            painter.drawImage(0,0,layer.pixels);
            painter.end();
            layer.pixels = cropped;
            continue;
        }
        if (layer.background) {
            layer.background = false;
            layer.locked = false;
        }
        layer.transform *= delta;
        layer.mask_transform *= delta;
    }
    if (delete_pixels) {
        const QRectF retained(QPointF(), bounds.size());
        const std::function<void(std::vector<Layer>&)> erase_outside = [&](auto& layers) {
            for (auto& layer : layers) {
                if (!layer.children.empty()) erase_outside(layer.children);
                if (layer.background || !layer.raw_source.isEmpty() || layer.pixels.isNull() ||
                    (layer.kind != LayerKind::pixels && layer.kind != LayerKind::repair)) continue;
                const auto matrix = layer.transform * parent_matrix(state, layer.id);
                // Keep the editable transform/effect/mask coordinate system. Only actual
                // raster samples outside the cropped canvas are discarded.
                layer.pixels=copy_image(layer.pixels);
                for (int y = 0; y < layer.pixels.height(); ++y) {
                    auto* row = reinterpret_cast<float*>(layer.pixels.scanLine(y));
                    for (int x = 0; x < layer.pixels.width(); ++x) {
                        const auto warped = warp_point(layer, layer.pixels.size(), QPointF(x+.5, y+.5), false);
                        if (!warped || !retained.contains(matrix.map(*warped)))
                            std::fill_n(row + x*4, 4, 0.F);
                    }
                }
            }
        };
        erase_outside(state.layers);
    }
    const auto crop_range = [&](QImage& image) {
        if (image.isNull())
            return;
        if(angle!=0){
            QImage output(bounds.size(),QImage::Format_ARGB32_Premultiplied);output.fill(Qt::black);
            QPainter painter(&output);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.setTransform(delta);painter.drawImage(0,0,image);painter.end();image=output.convertToFormat(QImage::Format_Grayscale8);return;
        }
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
