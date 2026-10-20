#include "huelith/editing/detail.h"
#include "huelith/editing/project.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace huelith::editing {
namespace {
using Field = std::pair<const char*, double DetailSettings::*>;
constexpr Field fields[]{{"radius", &DetailSettings::radius},
                         {"detail", &DetailSettings::detail},
                         {"masking", &DetailSettings::masking},
                         {"luminance", &DetailSettings::luminance},
                         {"luminanceDetail", &DetailSettings::luminance_detail},
                         {"luminanceContrast", &DetailSettings::luminance_contrast},
                         {"color", &DetailSettings::color},
                         {"colorDetail", &DetailSettings::color_detail},
                         {"colorSmoothness", &DetailSettings::color_smoothness}};
} // namespace
void validate_detail(const DetailSettings& s) {
    for (const auto& [name, member] : fields) {
        Q_UNUSED(name);
        if (!std::isfinite(s.*member) || s.*member < 0 || s.*member > 100)
            throw EditError(QStringLiteral("细节参数无效"));
    }
    if (s.radius < .5 || s.radius > 3)
        throw EditError(QStringLiteral("锐化半径必须在 0.5 至 3.0 像素之间"));
}
QJsonObject detail_json(const DetailSettings& s) {
    validate_detail(s);
    QJsonObject result{{"version", 1}, {"legacySharpening", s.legacy_sharpening}};
    for (const auto& [name, member] : fields)
        result.insert(QLatin1String(name), s.*member);
    return result;
}
DetailSettings detail_from_json(const QJsonObject& json) {
    DetailSettings s;
    if (json.isEmpty())
        return s;
    if (json.value("version").toInt() != 1)
        throw EditError(QStringLiteral("未知细节配方版本"));
    const auto defaults = detail_json(s);
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!defaults.contains(it.key()))
            throw EditError(QStringLiteral("未知细节参数"));
    for (const auto& [name, member] : fields) {
        const auto key = QLatin1String(name);
        if (json.contains(key)) {
            if (!json.value(key).isDouble())
                throw EditError(QStringLiteral("细节参数必须为数值"));
            s.*member = json.value(key).toDouble();
        }
    }
    if (json.contains("legacySharpening")) {
        if (!json.value("legacySharpening").isBool())
            throw EditError(QStringLiteral("旧锐化标记无效"));
        s.legacy_sharpening = json.value("legacySharpening").toBool();
    }
    validate_detail(s);
    return s;
}
QImage apply_detail(const QImage& input, const DetailSettings& s, double amount,
                    const std::atomic_bool* cancelled, double pixel_scale, int preview) {
    validate_detail(s);
    if (!std::isfinite(amount) || amount < 0 || amount > 150 || !std::isfinite(pixel_scale) ||
        pixel_scale <= 0 || pixel_scale > 1)
        throw EditError(QStringLiteral("细节处理参数无效"));
    const auto check = [cancelled] {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("细节处理已取消"));
    };
    check();
    if (preview == 0 && (amount == 0 || s.legacy_sharpening) && s.luminance == 0 && s.color == 0)
        return input;
    if (input.format() != QImage::Format_RGBA32FPx4_Premultiplied || input.isNull())
        throw EditError(QStringLiteral("细节处理需要 FP32 图像"));
    const int w = input.width(), h = input.height();
    const auto size = std::size_t(w) * h;
    std::vector<float> alpha(size), luminance(size), red(size), blue(size);
    for (int y = 0; y < h; ++y) {
        check();
        const auto* p = reinterpret_cast<const float*>(input.constScanLine(y));
        for (int x = 0; x < w; ++x, p += 4) {
            const auto i = std::size_t(y) * w + x;
            alpha[i] = p[3];
            if (p[3] > 0) {
                luminance[i] = (.2126F * p[0] + .7152F * p[1] + .0722F * p[2]) / p[3];
                red[i] = p[0] / p[3] - luminance[i];
                blue[i] = p[2] / p[3] - luminance[i];
            }
        }
    }
    // Alpha-weighted separable bilateral filtering: no hidden transparent pixel
    // contributes to visible colors. Luminance guides chroma to protect boundaries.
    const auto filter = [&](const std::vector<float>& plane, double radius, double range,
                            bool guided) {
        std::vector<float> intermediate(size), result(size);
        const double sigma = std::max(.15, radius * pixel_scale);
        const int reach = int(std::ceil(2 * sigma));
        std::vector<double> kernel(std::size_t(reach + 1));
        for (int k = 0; k <= reach; ++k)
            kernel[k] = std::exp(-k * k / (2 * sigma * sigma));
        for (int pass = 0; pass < 2; ++pass) {
            const auto& src = pass == 0 ? plane : intermediate;
            auto& dst = pass == 0 ? intermediate : result;
            for (int y = 0; y < h; ++y) {
                check();
                for (int x = 0; x < w; ++x) {
                    const auto i = std::size_t(y) * w + x;
                    double sum{}, weight{};
                    for (int k = -reach; k <= reach; ++k) {
                        const int xx = std::clamp(x + (pass == 0 ? k : 0), 0, w - 1);
                        const int yy = std::clamp(y + (pass == 1 ? k : 0), 0, h - 1);
                        const auto j = std::size_t(yy) * w + xx;
                        double a = kernel[std::abs(k)] * alpha[j];
                        if (guided) {
                            const double delta = plane[j] - plane[i];
                            const double guide = luminance[j] - luminance[i];
                            const double threshold =
                                range * std::sqrt(.02 + std::abs(luminance[i]));
                            a /= 1 + (delta * delta + guide * guide) / (threshold * threshold);
                        }
                        sum += src[j] * a;
                        weight += a;
                    }
                    dst[i] = weight > 0 ? float(sum / weight) : src[i];
                }
            }
        }
        return result;
    };
    auto light = luminance;
    if (s.luminance > 0) {
        auto smooth = filter(luminance, 1.5, .015 + (100 - s.luminance_detail) * .002, true);
        for (std::size_t i = 0; i < size; ++i) {
            const double delta = luminance[i] - smooth[i];
            const double retained =
                s.luminance_contrast / 100 *
                std::clamp(std::abs(delta) / (.02 + std::abs(luminance[i]) * .1), 0., 1.);
            light[i] = float(luminance[i] - delta * s.luminance / 100 * (1 - retained));
        }
    }
    if (s.color > 0) {
        for (auto* plane : {&red, &blue}) {
            const auto smooth = filter(*plane, 1 + s.color_smoothness / 40,
                                       .02 + (100 - s.color_detail) * .004, true);
            for (std::size_t i = 0; i < size; ++i)
                (*plane)[i] += float((smooth[i] - (*plane)[i]) * s.color / 100);
        }
    }
    std::vector<float> mask_preview;
    if (preview == 1)
        mask_preview.resize(size);
    if ((amount > 0 && !s.legacy_sharpening) || preview == 1) {
        const auto smooth = filter(light, s.radius, 1, false);
        for (int y = 0; y < h; ++y) {
            check();
            for (int x = 0; x < w; ++x) {
                const auto i = std::size_t(y) * w + x;
                const double edge =
                    std::max(std::abs(luminance[std::size_t(y) * w + std::min(w - 1, x + 1)] -
                                      luminance[std::size_t(y) * w + std::max(0, x - 1)]),
                             std::abs(luminance[std::size_t(std::min(h - 1, y + 1)) * w + x] -
                                      luminance[std::size_t(std::max(0, y - 1)) * w + x]));
                const double threshold = s.masking / 100 * (.02 + .2 * std::abs(luminance[i]));
                const double mask =
                    threshold == 0 ? 1 : std::clamp((edge - threshold) / threshold, 0., 1.);
                if (preview == 1)
                    mask_preview[i] = float(mask);
                const double delta = light[i] - smooth[i];
                const double texture =
                    s.detail / 100 +
                    (1 - s.detail / 100) *
                        std::clamp(std::abs(delta) / (.005 + .03 * std::abs(luminance[i])), 0., 1.);
                light[i] += float(amount / 100 * delta * mask * texture);
            }
        }
    }
    auto result = input.copy();
    for (int y = 0; y < h; ++y) {
        check();
        auto* p = reinterpret_cast<float*>(result.scanLine(y));
        for (int x = 0; x < w; ++x, p += 4) {
            const auto i = std::size_t(y) * w + x;
            if (alpha[i] <= 0)
                continue;
            if (preview != 0) {
                const float value = preview == 1 ? mask_preview[i] : light[i];
                p[0] = p[1] = p[2] = value * alpha[i];
                continue;
            }
            p[0] = (light[i] + red[i]) * alpha[i];
            p[2] = (light[i] + blue[i]) * alpha[i];
            p[1] = (light[i] - (.2126F * red[i] + .0722F * blue[i]) / .7152F) * alpha[i];
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(p[c]))
                    throw EditError(QStringLiteral("细节结果超出浮点范围"));
        }
    }
    return result;
}
} // namespace huelith::editing
