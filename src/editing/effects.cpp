#include "huelith/editing/effects.h"
#include "huelith/editing/project.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace huelith::editing {
std::shared_ptr<const CubeLut> parse_cube(const QString& text) {
    if (text.size() > 16 * 1024 * 1024)
        throw EditError(QStringLiteral("LUT 文件超过16MiB限制"));
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
                throw EditError(QStringLiteral("LUT 含无效数值"));
            return value;
        };
        if (fields[0] == "LUT_3D_SIZE") {
            if (fields.size() != 2 || lut->size != 0)
                throw EditError(QStringLiteral("LUT尺寸声明无效"));
            const double size = number(1);
            if (size < 2 || size > 65 || size != std::floor(size))
                throw EditError(QStringLiteral("LUT支持2至65阶3D表"));
            lut->size = int(size);
        } else if (fields[0] == "DOMAIN_MIN" || fields[0] == "DOMAIN_MAX") {
            bool& seen = fields[0] == "DOMAIN_MIN" ? min_seen : max_seen;
            if (fields.size() != 4 || seen || !lut->samples.empty())
                throw EditError(QStringLiteral("LUT定义域声明无效"));
            seen = true;
            auto& values = fields[0] == "DOMAIN_MIN" ? lut->minimum : lut->maximum;
            for (int c = 0; c < 3; ++c)
                values[c] = number(c + 1);
        } else {
            if (lut->size == 0 || fields.size() != 3 ||
                lut->samples.size() >= std::size_t(lut->size) * lut->size * lut->size)
                throw EditError(QStringLiteral("需要完整的3D .cube文件，不支持混合1D声明"));
            lut->samples.push_back({float(number(0)), float(number(1)), float(number(2))});
        }
    }
    if (lut->size == 0 || lut->samples.size() != std::size_t(lut->size) * lut->size * lut->size)
        throw EditError(QStringLiteral("LUT数据不完整"));
    for (int c = 0; c < 3; ++c)
        if (lut->maximum[c] - lut->minimum[c] < 1e-6)
            throw EditError(QStringLiteral("LUT定义域必须严格递增"));
    return lut;
}
void validate_effects(const std::vector<Effect>& effects) {
    if (effects.size() > 64)
        throw EditError(QStringLiteral("单层最多64个效果"));
    QSet<QUuid> ids;
    for (const auto& effect : effects) {
        if (effect.id.isNull() || ids.contains(effect.id) || effect.name.size() > 512 ||
            !std::isfinite(effect.amount) || effect.amount < 0 || effect.amount > 1)
            throw EditError(QStringLiteral("效果标识、名称或强度无效"));
        ids.insert(effect.id);
        validate_tone(effect.tone);
        if (!effect.mask.isNull() && effect.mask.format() != QImage::Format_Grayscale8)
            throw EditError(QStringLiteral("效果范围需要灰度蒙版"));
        if (effect.lut && (effect.lut->size < 2 || effect.lut->size > 65 ||
                           effect.lut->samples.size() !=
                               std::size_t(effect.lut->size) * effect.lut->size * effect.lut->size))
            throw EditError(QStringLiteral("LUT结构无效"));
        if (effect.lut) {
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(effect.lut->minimum[c]) ||
                    !std::isfinite(effect.lut->maximum[c]) ||
                    effect.lut->maximum[c] - effect.lut->minimum[c] < 1e-6)
                    throw EditError(QStringLiteral("LUT定义域无效"));
            for (const auto& sample : effect.lut->samples)
                for (float value : sample)
                    if (!std::isfinite(value) || std::abs(value) > 65504)
                        throw EditError(QStringLiteral("LUT样本无效"));
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
        throw EditError(QStringLiteral("效果需要线性工作空间FP32图像"));
    const auto check = [cancelled] {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("效果处理已取消"));
    };
    check();
    auto result = source;
    for (const auto& effect : effects) {
        check();
        if (!effect.enabled || effect.amount == 0)
            continue;
        auto adjusted = apply_tone(result, effect.tone, cancelled);
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
                            throw EditError(QStringLiteral("LUT输入非有限"));
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
                            throw EditError(QStringLiteral("LUT结果溢出"));
                        p[c] = float(value);
                    }
                }
            }
            adjusted = encoded.convertedToColorSpace(working_space(), source.format());
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
                for (int c = 0; c < 3; ++c)
                    row[x * 4 + c] += float((other[x * 4 + c] - row[x * 4 + c]) * amount);
            }
        }
    }
    return result;
}
} // namespace huelith::editing
