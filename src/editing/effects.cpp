#include <QCoreApplication>
#include "huelith/editing/effects.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/liquify.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/project.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace huelith::editing {
namespace {
QImage gaussian_blur(const QImage& source, double sigma, const std::atomic_bool* cancelled) {
    auto result=source;
    const auto check=[cancelled]{if(cancelled && cancelled->load())throw EditError(QCoreApplication::translate("Huelith", "模糊已取消"));};
    // Small kernels use a truncated Gaussian. Large radii use six extended boxes,
    // each with exact sigma^2/6 variance, for radius-independent running-sum cost.
    const bool exact=sigma<=4;
    const int passes=exact?1:6;
    const double variance=sigma*sigma/passes;
    const int radius=exact?int(std::ceil(3.5*sigma)):int(std::floor((std::sqrt(1+12*variance)-1)/2));
    const double base_variance=double(radius)*(radius+1)/3;
    const double edge=exact?0:(2*radius+1)*(variance-base_variance)/(2*((radius+1.)*(radius+1.)-variance));
    std::vector<double> kernel;
    if(exact){double total=0;for(int i=-radius;i<=radius;++i){const double v=std::exp(-.5*i*i/(sigma*sigma));kernel.push_back(v);total+=v;}for(auto& v:kernel)v/=total;}
    for(int pass=0;pass<passes;++pass)for(int axis=0;axis<2;++axis){
        check();auto output=allocate_image(source.size(),source.format());output.setColorSpace(source.colorSpace());
        const int length=axis==0?source.width():source.height(),lines=axis==0?source.height():source.width();
        for(int line=0;line<lines;++line){
            if((line&15)==0)check();
            const auto pixel=[&](int index){index=std::clamp(index,0,length-1);return reinterpret_cast<const float*>(result.constScanLine(axis==0?line:index))+4*(axis==0?index:line);};
            std::array<double,4> sum{};
            if(!exact)for(int i=-radius;i<=radius;++i)for(int c=0;c<4;++c)sum[c]+=pixel(i)[c];
            for(int i=0;i<length;++i){
                auto* target=reinterpret_cast<float*>(output.scanLine(axis==0?line:i))+4*(axis==0?i:line);
                for(int c=0;c<4;++c){
                    if(exact){double value=0;for(int k=-radius;k<=radius;++k)value+=pixel(i+k)[c]*kernel[std::size_t(k+radius)];target[c]=float(value);}
                    else target[c]=float((sum[c]+edge*(pixel(i-radius-1)[c]+pixel(i+radius+1)[c]))/(2*radius+1+2*edge));
                }
                if(!exact)for(int c=0;c<4;++c)sum[c]+=double(pixel(i+radius+1)[c])-pixel(i-radius)[c];
            }
        }
        result=std::move(output);
    }
    return result;
}
}
static QImage conventional_filter(const QImage& source, const Effect& effect,
                                  const std::atomic_bool* cancelled) {
    if (!effect.filter)
        return source;
    auto output = source;
    const double radius = effect.filter_radius,
                 angle = effect.filter_angle * std::numbers::pi / 180;
    const auto blurred = (effect.filter == 1 || effect.filter == 11)
                             ? gaussian_blur(source, std::max(.1, radius), cancelled)
                             : QImage{};
    const auto pixel = [&](int x, int y) {
        return reinterpret_cast<const float*>(
                   source.constScanLine(std::clamp(y, 0, source.height() - 1))) +
               4 * std::clamp(x, 0, source.width() - 1);
    };
    const auto noise = [&](int x, int y) {
        quint32 v =
            quint32(x) * 374761393U + quint32(y) * 668265263U + effect.filter_seed * 2246822519U;
        v = (v ^ (v >> 13)) * 1274126177U;
        return double(v ^ (v >> 16)) / 4294967295.;
    };
    for (int y = 0; y < source.height(); ++y) {
        if (cancelled && cancelled->load())
            throw EditError(QCoreApplication::translate("Huelith", "滤镜已取消"));
        auto* row = reinterpret_cast<float*>(output.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            auto* p = row + 4 * x;
            const auto* input = pixel(x, y);
            if (effect.filter == 1) {
                const auto* b = reinterpret_cast<const float*>(blurred.constScanLine(y)) + 4 * x;
                for (int c = 0; c < 3; ++c)
                    p[c] = input[c] + (input[c] - b[c]);
            } else if (effect.filter == 2) {
                const int samples = std::clamp(int(std::ceil(radius)) * 2 + 1, 3, 129);
                std::fill_n(p, 4, 0.F);
                for (int i = 0; i < samples; ++i) {
                    const double d = radius * (2. * i / (samples - 1) - 1);
                    const auto* sample =
                        pixel(qRound(x + d * std::cos(angle)), qRound(y + d * std::sin(angle)));
                    for (int c = 0; c < 4; ++c)
                        p[c] += sample[c] / samples;
                }
            } else if (effect.filter == 3) {
                const int r = std::clamp(qRound(radius), 1, 8);
                std::vector<float> values;
                values.reserve((r * 2 + 1) * (r * 2 + 1));
                for (int c = 0; c < 3; ++c) {
                    values.clear();
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            const auto* sample = pixel(x + dx, y + dy);
                            if (sample[3] > 0)
                                values.push_back(sample[c] / sample[3]);
                        }
                    if (!values.empty()) {
                        auto mid = values.begin() + values.size() / 2;
                        std::nth_element(values.begin(), mid, values.end());
                        p[c] = *mid * input[3];
                    }
                }
            } else if (effect.filter == 9 || effect.filter == 16) {
                const int r = std::clamp(qRound(radius), 1, 8);
                if (effect.filter == 9) {
                    std::array<int, 16> counts{};
                    std::array<std::array<float, 3>, 16> colors{};
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            const auto* q = pixel(x + dx, y + dy);
                            if (q[3] <= 0)
                                continue;
                            const int bin = std::clamp(int((q[0] + q[1] + q[2]) / q[3] * 5), 0, 15);
                            ++counts[bin];
                            for (int c = 0; c < 3; ++c)
                                colors[bin][c] += q[c] / q[3];
                        }
                    const auto bin = std::size_t(std::max_element(counts.begin(), counts.end()) -
                                                 counts.begin());
                    if (counts[bin])
                        for (int c = 0; c < 3; ++c)
                            p[c] = colors[bin][c] / counts[bin] * input[3];
                } else if (input[3] > 0) {
                    std::array<double, 3> sum{};
                    double weights = 0;
                    const double sigma = std::max(.01, std::abs(effect.filter_angle) / 360.);
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            const auto* q = pixel(x + dx, y + dy);
                            if (q[3] <= 0)
                                continue;
                            double difference = 0;
                            for (int c = 0; c < 3; ++c)
                                difference += std::pow(q[c] / q[3] - input[c] / input[3], 2);
                            const double w = std::exp(-(dx * dx + dy * dy) / (2. * r * r) -
                                                      difference / (2 * sigma * sigma));
                            weights += w;
                            for (int c = 0; c < 3; ++c)
                                sum[c] += q[c] / q[3] * w;
                        }
                    if (weights > 0)
                        for (int c = 0; c < 3; ++c)
                            p[c] = float(sum[c] / weights) * input[3];
                }
            } else if (effect.filter == 10) {
                const auto luminance = [&](int px, int py) {
                    const auto* q = pixel(px, py);
                    return q[3] > 0 ? (q[0] + q[1] + q[2]) / q[3] / 3 : 0.F;
                };
                const double gx = luminance(x + 1, y - 1) + 2 * luminance(x + 1, y) +
                                  luminance(x + 1, y + 1) - luminance(x - 1, y - 1) -
                                  2 * luminance(x - 1, y) - luminance(x - 1, y + 1);
                const double gy = luminance(x - 1, y + 1) + 2 * luminance(x, y + 1) +
                                  luminance(x + 1, y + 1) - luminance(x - 1, y - 1) -
                                  2 * luminance(x, y - 1) - luminance(x + 1, y - 1);
                for (int c = 0; c < 3; ++c)
                    p[c] = float(std::clamp(std::hypot(gx, gy) * radius / 5, 0., 1.)) * input[3];
            } else if (effect.filter == 11) {
                const auto* q = reinterpret_cast<const float*>(blurred.constScanLine(y)) + x * 4;
                for (int c = 0; c < 3; ++c)
                    p[c] = input[3] > 0 ? float(std::clamp(.5 + input[c] / input[3] -
                                                               (q[3] > 0 ? q[c] / q[3] : 0),
                                                           0., 1.)) *
                                              input[3]
                                        : 0;
            } else if (effect.filter >= 12 && effect.filter <= 15) {
                const double cx = (source.width() - 1) * .5, cy = (source.height() - 1) * .5;
                double sx = x, sy = y;
                if (effect.filter == 12) {
                    std::fill(p, p + 4, 0.F);
                    const int samples = 32;
                    for (int i = 0; i < samples; ++i) {
                        const double k = 1 - (double(i) / (samples - 1) - .5) * radius / 256.;
                        const auto* q = pixel(qRound(cx + (x - cx) * k), qRound(cy + (y - cy) * k));
                        for (int c = 0; c < 4; ++c)
                            p[c] += q[c] / samples;
                    }
                    continue;
                } else if (effect.filter == 13)
                    sx += std::sin(y / std::max(1., radius) * 2 * std::numbers::pi) *
                          effect.filter_angle;
                else if (effect.filter == 14) {
                    const double extent = std::min(source.width(), source.height()) * .5,
                                 d = std::hypot(x - cx, y - cy) / std::max(1., extent);
                    if (d < 1) {
                        const double k = 1 + effect.filter_angle / 360. * std::pow(1 - d * d, 2);
                        sx = cx + (x - cx) * k;
                        sy = cy + (y - cy) * k;
                    }
                } else {
                    const int block = std::max(1, qRound(radius));
                    sx = (x / block) * block + block * .5;
                    sy = (y / block) * block + block * .5;
                }
                const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                for (int c = 0; c < 4; ++c)
                    p[c] = float(std::lerp(
                        std::lerp(double(pixel(ix, iy)[c]), double(pixel(ix + 1, iy)[c]), sx - ix),
                        std::lerp(double(pixel(ix, iy + 1)[c]), double(pixel(ix + 1, iy + 1)[c]),
                                  sx - ix),
                        sy - iy));
            } else if (effect.filter == 4) {
                const float n = float((noise(x, y) - .5) * radius / 100) * input[3];
                for (int c = 0; c < 3; ++c)
                    p[c] = std::max(0.F, input[c] + n);
            } else if (effect.filter == 5) {
                const double cx = (source.width() - 1) * .5, cy = (source.height() - 1) * .5,
                             r = std::min(source.width(), source.height()) * .5;
                const double dx = x - cx, dy = y - cy, d = std::hypot(dx, dy);
                if (d < r) {
                    const double a = std::atan2(dy, dx) - angle * std::pow(1 - d / r, 2),
                                 sx = cx + d * std::cos(a), sy = cy + d * std::sin(a);
                    const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                    for (int c = 0; c < 4; ++c)
                        p[c] = float(std::lerp(std::lerp(double(pixel(ix, iy)[c]),
                                                         double(pixel(ix + 1, iy)[c]), sx - ix),
                                               std::lerp(double(pixel(ix, iy + 1)[c]),
                                                         double(pixel(ix + 1, iy + 1)[c]), sx - ix),
                                               sy - iy));
                }
            } else if (effect.filter == 6) {
                const int dx = qRound(std::cos(angle) * std::max(1., radius)),
                          dy = qRound(std::sin(angle) * std::max(1., radius));
                const auto* a = pixel(x + dx, y + dy);
                const auto* b = pixel(x - dx, y - dy);
                for (int c = 0; c < 3; ++c)
                    p[c] = float(std::clamp(.5 + (a[3] > 0 ? a[c] / a[3] : 0) -
                                                (b[3] > 0 ? b[c] / b[3] : 0),
                                            0., 1.)) *
                           input[3];
            } else if (effect.filter == 7 && input[3] > 0) {
                const double levels = std::clamp(std::round(radius), 2., 256.);
                for (int c = 0; c < 3; ++c)
                    p[c] = float(std::round(input[c] / input[3] * (levels - 1)) / (levels - 1)) *
                           input[3];
            } else if (effect.filter == 8) {
                double value = 0, weight = 0;
                for (int octave = 0; octave < 5; ++octave) {
                    const double scale = std::max(1., radius * 8 / std::pow(2., octave));
                    const double sx = x / scale, sy = y / scale;
                    const int ix = int(sx), iy = int(sy);
                    const double fx = sx - ix, fy = sy - iy, u = fx * fx * (3 - 2 * fx),
                                 v = fy * fy * (3 - 2 * fy), w = std::pow(.5, octave);
                    value += std::lerp(std::lerp(noise(ix, iy), noise(ix + 1, iy), u),
                                       std::lerp(noise(ix, iy + 1), noise(ix + 1, iy + 1), u), v) *
                             w;
                    weight += w;
                }
                for (int c = 0; c < 3; ++c)
                    p[c] = float(value / weight) * input[3];
            }
        }
    }
    return output;
}
std::shared_ptr<const CubeLut> parse_cube(const QString& text) {
    if (text.size() > 16 * 1024 * 1024)
        throw EditError(QCoreApplication::translate("Huelith", "LUT 文件超过16MiB限制"));
    auto lut = std::make_shared<CubeLut>();
    lut->source = text;
    bool min_seen{}, max_seen{};
    const QRegularExpression spaces(QStringLiteral("\\s+"));
    for (const auto& raw_line : text.split('\n')) {
        const auto line = raw_line.section('#', 0, 0).trimmed();
        if (line.isEmpty() || line.startsWith("TITLE "))
            continue;
        const auto fields = line.split(spaces, Qt::SkipEmptyParts);
        const auto number = [&](int index) {
            bool ok{};
            const double value = fields.value(index).toDouble(&ok);
            if (!ok || !std::isfinite(value) || std::abs(value) > 65504)
                throw EditError(QCoreApplication::translate("Huelith", "LUT 含无效数值"));
            return value;
        };
        if (fields[0] == "LUT_3D_SIZE") {
            if (fields.size() != 2 || lut->size != 0)
                throw EditError(QCoreApplication::translate("Huelith", "LUT尺寸声明无效"));
            const double size = number(1);
            if (size < 2 || size > 65 || size != std::floor(size))
                throw EditError(QCoreApplication::translate("Huelith", "LUT支持2至65阶3D表"));
            lut->size = int(size);
        } else if (fields[0] == "DOMAIN_MIN" || fields[0] == "DOMAIN_MAX") {
            bool& seen = fields[0] == "DOMAIN_MIN" ? min_seen : max_seen;
            if (fields.size() != 4 || seen || !lut->samples.empty())
                throw EditError(QCoreApplication::translate("Huelith", "LUT定义域声明无效"));
            seen = true;
            auto& values = fields[0] == "DOMAIN_MIN" ? lut->minimum : lut->maximum;
            for (int c = 0; c < 3; ++c)
                values[c] = number(c + 1);
        } else {
            if (lut->size == 0 || fields.size() != 3 ||
                lut->samples.size() >= std::size_t(lut->size) * lut->size * lut->size)
                throw EditError(QCoreApplication::translate("Huelith", "需要完整的3D .cube文件，不支持混合1D声明"));
            lut->samples.push_back({float(number(0)), float(number(1)), float(number(2))});
        }
    }
    if (lut->size == 0 || lut->samples.size() != std::size_t(lut->size) * lut->size * lut->size)
        throw EditError(QCoreApplication::translate("Huelith", "LUT数据不完整"));
    for (int c = 0; c < 3; ++c)
        if (lut->maximum[c] - lut->minimum[c] < 1e-6)
            throw EditError(QCoreApplication::translate("Huelith", "LUT定义域必须严格递增"));
    return lut;
}
void validate_effects(const std::vector<Effect>& effects) {
    if (effects.size() > 64)
        throw EditError(QCoreApplication::translate("Huelith", "单层最多64个效果"));
    QSet<QUuid> ids;
    for (const auto& effect : effects) {
        if (effect.filter < 0 || effect.filter > 16 || !std::isfinite(effect.filter_radius) ||
            effect.filter_radius < .1 || effect.filter_radius > 256 ||
            !std::isfinite(effect.filter_angle) || std::abs(effect.filter_angle) > 360 ||
            int(effect.blend) < 0 || int(effect.blend) > 23)
            throw EditError(QCoreApplication::translate("Huelith", "滤镜参数无效"));
        if (effect.id.isNull() || ids.contains(effect.id) || effect.name.size() > 512 ||
            !std::isfinite(effect.amount) || effect.amount < 0 || effect.amount > 1 ||
            !std::isfinite(effect.blur_radius) || effect.blur_radius<0 || effect.blur_radius>1000)
            throw EditError(QCoreApplication::translate("Huelith", "效果标识、名称或强度无效"));
        ids.insert(effect.id);
        validate_tone(effect.tone);
        validate_liquify_mesh(effect.liquify);
        if(!effect.portrait.isEmpty() && QJsonDocument(effect.portrait).toJson(QJsonDocument::Compact).size()>64*1024*1024)
            throw EditError(QCoreApplication::translate("Huelith", "单个人像效果数据超过 64 MiB，请拆分到不同效果"));
        if (!effect.mask.isNull() && effect.mask.format() != QImage::Format_Grayscale8)
            throw EditError(QCoreApplication::translate("Huelith", "效果范围需要灰度蒙版"));
        if (effect.lut && (effect.lut->size < 2 || effect.lut->size > 65 ||
                           effect.lut->samples.size() !=
                               std::size_t(effect.lut->size) * effect.lut->size * effect.lut->size))
            throw EditError(QCoreApplication::translate("Huelith", "LUT结构无效"));
        if (effect.lut) {
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(effect.lut->minimum[c]) ||
                    !std::isfinite(effect.lut->maximum[c]) ||
                    effect.lut->maximum[c] - effect.lut->minimum[c] < 1e-6)
                    throw EditError(QCoreApplication::translate("Huelith", "LUT定义域无效"));
            for (const auto& sample : effect.lut->samples)
                for (float value : sample)
                    if (!std::isfinite(value) || std::abs(value) > 65504)
                        throw EditError(QCoreApplication::translate("Huelith", "LUT样本无效"));
        }
    }
}
QImage apply_effects(const QImage& source, const std::vector<Effect>& effects,
                     const std::atomic_bool* cancelled) {
    validate_effects(effects);
    if (effects.empty())
        return source;
    if (source.isNull() || source.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        source.colorSpace() != working_space())
        throw EditError(QCoreApplication::translate("Huelith", "效果需要线性工作空间FP32图像"));
    const auto check = [cancelled] {
        if (cancelled && cancelled->load())
            throw EditError(QCoreApplication::translate("Huelith", "效果处理已取消"));
    };
    check();
    auto result = source;
    for (const auto& effect : effects) {
        check();
        if (!effect.enabled || effect.amount == 0)
            continue;
        // Portrait strength scales its displacement field. Never crossfade two
        // displaced faces, which would create duplicate contours at partial strength.
        if(!effect.portrait.isEmpty())result=apply_portrait(result,effect.portrait,effect.amount,effect.mask,cancelled);
        auto adjusted = apply_tone(result, effect.tone, cancelled);
        if(effect.blur_radius>0)
            adjusted=gaussian_blur(adjusted,effect.blur_radius,cancelled);
        adjusted = conventional_filter(adjusted, effect, cancelled);
        if (!effect.liquify.isNull())
            adjusted = apply_liquify(adjusted, effect.liquify, cancelled);
        if (effect.lut) {
            auto encoded = adjusted.convertedToColorSpace(QColorSpace::SRgb, source.format());
            const auto& lut = *effect.lut;
            for (int y = 0; y < encoded.height(); ++y) {
                check();
                auto* row = reinterpret_cast<float*>(encoded.scanLine(y));
                for (int x = 0; x < encoded.width(); ++x) {
                    auto* p = row + 4 * x;
                    if (p[3] <= 0)
                        continue;
                    std::array<int, 3> index{};
                    std::array<double, 3> fraction{};
                    for (int c = 0; c < 3; ++c) {
                        const double position = (double(p[c]) / p[3] - lut.minimum[c]) /
                                                (lut.maximum[c] - lut.minimum[c]) * (lut.size - 1);
                        if (!std::isfinite(position))
                            throw EditError(QCoreApplication::translate("Huelith", "LUT输入非有限"));
                        index[c] = int(std::clamp(std::floor(position), 0., double(lut.size - 2)));
                        // Linear edge extrapolation preserves HDR instead of silently clipping to
                        // SDR.
                        fraction[c] = position - index[c];
                    }
                    std::array<double, 3> color{};
                    for (int b = 0; b < 2; ++b)
                        for (int g = 0; g < 2; ++g)
                            for (int r = 0; r < 2; ++r) {
                                const auto& value =
                                    lut.samples[std::size_t(index[2] + b) * lut.size * lut.size +
                                                (index[1] + g) * lut.size + index[0] + r];
                                const double weight = (r ? fraction[0] : 1 - fraction[0]) *
                                                      (g ? fraction[1] : 1 - fraction[1]) *
                                                      (b ? fraction[2] : 1 - fraction[2]);
                                for (int c = 0; c < 3; ++c)
                                    color[c] += value[c] * weight;
                            }
                    for (int c = 0; c < 3; ++c) {
                        const double value = color[c] * p[3];
                        if (!std::isfinite(value) ||
                            std::abs(value) > std::numeric_limits<float>::max())
                            throw EditError(QCoreApplication::translate("Huelith", "LUT结果溢出"));
                        p[c] = float(value);
                    }
                }
            }
            adjusted = encoded.convertedToColorSpace(working_space(), source.format());
        }
        if (effect.blend != Blend::normal) {
            for (int y = 0; y < adjusted.height(); ++y) {
                auto* p = reinterpret_cast<float*>(adjusted.scanLine(y));
                const auto* b = reinterpret_cast<const float*>(result.constScanLine(y));
                for (int x = 0; x < adjusted.width(); ++x, p += 4, b += 4)
                    if (p[3] > 0 && b[3] > 0) {
                        const auto color =
                            artistic_term({p[0] / p[3], p[1] / p[3], p[2] / p[3]},
                                          {b[0] / b[3], b[1] / b[3], b[2] / b[3]}, effect.blend);
                        for (int c = 0; c < 3; ++c)
                            p[c] = color[c] * p[3];
                    }
            }
        }
        if (effect.mask.isNull() && effect.amount == 1) {
            result = std::move(adjusted);
            continue;
        }
        const auto mask = effect.mask.isNull()
                              ? QImage{}
                              : effect.mask.scaled(result.size(), Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
        for (int y = 0; y < result.height(); ++y) {
            check();
            auto* row = reinterpret_cast<float*>(result.scanLine(y));
            const auto* other = reinterpret_cast<const float*>(adjusted.constScanLine(y));
            const auto* coverage = mask.isNull() ? nullptr : mask.constScanLine(y);
            for (int x = 0; x < result.width(); ++x) {
                const double amount = effect.amount * (coverage ? coverage[x] / 255. : 1);
                for (int c = 0; c < 4; ++c)
                    row[x * 4 + c] += float((other[x * 4 + c] - row[x * 4 + c]) * amount);
            }
        }
    }
    return result;
}
} // namespace huelith::editing
