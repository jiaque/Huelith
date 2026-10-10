#include "huelith/editing/tone.h"
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace huelith::editing {
namespace {
using Field = std::pair<const char*, double ToneSettings::*>;
constexpr Field fields[]{{"exposure", &ToneSettings::exposure},
                         {"contrast", &ToneSettings::contrast},
                         {"highlights", &ToneSettings::highlights},
                         {"shadows", &ToneSettings::shadows},
                         {"whites", &ToneSettings::whites},
                         {"blacks", &ToneSettings::blacks},
                         {"temperature", &ToneSettings::temperature},
                         {"tint", &ToneSettings::tint},
                         {"saturation", &ToneSettings::saturation},
                         {"vibrance", &ToneSettings::vibrance},
                         {"texture", &ToneSettings::texture},
                         {"clarity", &ToneSettings::clarity},
                         {"dehaze", &ToneSettings::dehaze},
                         {"vignette", &ToneSettings::vignette},
                         {"grain", &ToneSettings::grain},
                         {"sharpening", &ToneSettings::sharpening},
                         {"blackPoint", &ToneSettings::black_point},
                         {"whitePoint", &ToneSettings::white_point},
                         {"gamma", &ToneSettings::gamma},
                         {"gradingBalance", &ToneSettings::grading_balance}};
double encoded(double value) {
    return value <= .0031308 ? 12.92 * value : 1.055 * std::pow(value, 1 / 2.4) - .055;
}
double decoded(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
double curve(double value, const std::vector<QPointF>& points) {
    auto upper = std::upper_bound(points.begin(), points.end(), value,
                                  [](double x, QPointF point) { return x < point.x(); });
    if (upper == points.begin())
        ++upper;
    if (upper == points.end())
        --upper;
    const auto a = *(upper - 1), b = *upper;
    return a.y() + (value - a.x()) * (b.y() - a.y()) / (b.x() - a.x());
}
} // namespace
void validate_tone(const ToneSettings& settings) {
    for (const auto& [name, member] : fields) {
        Q_UNUSED(name);
        if (!std::isfinite(settings.*member) || std::abs(settings.*member) > 100)
            throw EditError(QStringLiteral("调色参数无效"));
    }
    if (std::abs(settings.exposure) > 16 || settings.black_point < 0 || settings.white_point > 1 ||
        settings.white_point - settings.black_point < .001 || settings.gamma < .1 ||
        settings.gamma > 10 || settings.curve.size() < 2 || settings.curve.size() > 64)
        throw EditError(QStringLiteral("色阶或曲线参数无效"));
    const auto validate_curve = [](const std::vector<QPointF>& points) {
        if (points.size() < 2 || points.size() > 64)
            throw EditError(QStringLiteral("曲线控制点数量无效"));
        double previous = -1;
        for (const auto point : points) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || point.x() < 0 ||
                point.x() > 1 || point.y() < 0 || point.y() > 1 || point.x() - previous < 1e-6)
                throw EditError(QStringLiteral("曲线控制点必须按输入值严格递增"));
            previous = point.x();
        }
        if (points.front().x() != 0 || points.back().x() != 1)
            throw EditError(QStringLiteral("曲线需要首尾输入端点"));
    };
    validate_curve(settings.curve);
    for (const auto& channel : settings.channel_curves)
        validate_curve(channel);
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(settings.grading_hue[i]) || settings.grading_hue[i] < 0 ||
            settings.grading_hue[i] > 360 || !std::isfinite(settings.grading_saturation[i]) ||
            settings.grading_saturation[i] < 0 || settings.grading_saturation[i] > 100 ||
            !std::isfinite(settings.grading_luminance[i]) ||
            std::abs(settings.grading_luminance[i]) > 100)
            throw EditError(QStringLiteral("色彩分级参数无效"));
    }
    for (const auto& band : {settings.hue, settings.saturation_bands, settings.luminance_bands})
        for (const double value : band)
            if (!std::isfinite(value) || std::abs(value) > 100)
                throw EditError(QStringLiteral("HSL 分色参数无效"));
}
QJsonObject tone_json(const ToneSettings& settings) {
    validate_tone(settings);
    QJsonObject json{{"version", 3}, {"monochrome", settings.monochrome}};
    QJsonArray sections;
    for (bool enabled : settings.sections)
        sections.append(enabled);
    json.insert("sections", sections);
    for (const auto& [name, member] : fields)
        json.insert(QLatin1String(name), settings.*member);
    QJsonArray points;
    for (const auto point : settings.curve)
        points.append(QJsonArray{point.x(), point.y()});
    json.insert("curve", points);
    QJsonArray channels;
    for (const auto& channel : settings.channel_curves) {
        QJsonArray nodes;
        for (const auto point : channel)
            nodes.append(QJsonArray{point.x(), point.y()});
        channels.append(nodes);
    }
    json.insert("channelCurves", channels);
    for (const auto& [name, band] : {std::pair{"gradingHue", settings.grading_hue},
                                     std::pair{"gradingSaturation", settings.grading_saturation},
                                     std::pair{"gradingLuminance", settings.grading_luminance}}) {
        QJsonArray values;
        for (const auto value : band)
            values.append(value);
        json.insert(QLatin1String(name), values);
    }
    for (const auto& [name, band] :
         {std::pair{"hue", settings.hue}, std::pair{"saturationBands", settings.saturation_bands},
          std::pair{"luminanceBands", settings.luminance_bands}}) {
        QJsonArray values;
        for (const double value : band)
            values.append(value);
        json.insert(QLatin1String(name), values);
    }
    return json;
}
ToneSettings tone_from_json(const QJsonObject& json) {
    ToneSettings settings;
    if (json.isEmpty())
        return settings;
    if (json.value("version").toInt() < 1 || json.value("version").toInt() > 3)
        throw EditError(QStringLiteral("未知调色配方版本"));
    const auto defaults = tone_json(settings);
    if (json.contains("sections")) {
        const auto sections = json.value("sections").toArray();
        if (sections.size() != 8)
            throw EditError(QStringLiteral("调色分组开关数量无效"));
        for (int i = 0; i < 8; ++i) {
            if (!sections[i].isBool())
                throw EditError(QStringLiteral("调色分组开关必须为布尔值"));
            settings.sections[i] = sections[i].toBool();
        }
    }
    if (json.contains("monochrome")) {
        if (!json.value("monochrome").isBool())
            throw EditError(QStringLiteral("黑白开关格式无效"));
        settings.monochrome = json.value("monochrome").toBool();
    }
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!defaults.contains(it.key()))
            throw EditError(QStringLiteral("未知调色配方字段"));
    for (const auto& [name, member] : fields) {
        if (!json.contains(QLatin1String(name)))
            continue;
        if (!json.value(QLatin1String(name)).isDouble())
            throw EditError(QStringLiteral("调色数值格式无效"));
        settings.*member = json.value(QLatin1String(name)).toDouble();
    }
    const auto read_curve = [](const QJsonValue& data) {
        if (!data.isArray())
            throw EditError(QStringLiteral("曲线格式无效"));
        std::vector<QPointF> points;
        for (const auto value : data.toArray()) {
            const auto pair = value.toArray();
            if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble())
                throw EditError(QStringLiteral("曲线格式无效"));
            points.emplace_back(pair[0].toDouble(), pair[1].toDouble());
        }
        return points;
    };
    if (json.contains("curve"))
        settings.curve = read_curve(json.value("curve"));
    if (json.contains("channelCurves")) {
        const auto channels = json.value("channelCurves").toArray();
        if (channels.size() != 3)
            throw EditError(QStringLiteral("需要三个通道曲线"));
        for (int c = 0; c < 3; ++c)
            settings.channel_curves[c] = read_curve(channels[c]);
    }
    for (auto [name, band] : {std::pair{"gradingHue", &settings.grading_hue},
                              std::pair{"gradingSaturation", &settings.grading_saturation},
                              std::pair{"gradingLuminance", &settings.grading_luminance}}) {
        if (!json.contains(QLatin1String(name)))
            continue;
        const auto values = json.value(QLatin1String(name)).toArray();
        if (values.size() != 3)
            throw EditError(QStringLiteral("需要阴影、中间调、高光三组分级参数"));
        for (int c = 0; c < 3; ++c) {
            if (!values[c].isDouble())
                throw EditError(QStringLiteral("色彩分级数值无效"));
            (*band)[c] = values[c].toDouble();
        }
    }
    for (auto [name, band] :
         {std::pair{"hue", &settings.hue}, std::pair{"saturationBands", &settings.saturation_bands},
          std::pair{"luminanceBands", &settings.luminance_bands}}) {
        if (!json.contains(QLatin1String(name)))
            continue;
        const auto values = json.value(QLatin1String(name)).toArray();
        if (values.size() != 8)
            throw EditError(QStringLiteral("HSL 分色数量无效"));
        for (int i = 0; i < 8; ++i) {
            if (!values[i].isDouble())
                throw EditError(QStringLiteral("HSL 参数格式无效"));
            (*band)[i] = values[i].toDouble();
        }
    }
    validate_tone(settings);
    return settings;
}
QImage apply_tone(const QImage& source, const ToneSettings& settings,
                  const std::atomic_bool* cancelled) {
    validate_tone(settings);
    auto s = settings;
    s.sections.fill(true);
    if (!settings.sections[0])
        s.exposure = s.contrast = s.highlights = s.shadows = s.whites = s.blacks = 0;
    if (!settings.sections[1]) {
        s.temperature = s.tint = s.saturation = s.vibrance = 0;
        s.monochrome = false;
    }
    if (!settings.sections[2]) {
        s.black_point = 0;
        s.white_point = s.gamma = 1;
    }
    if (!settings.sections[3]) {
        s.curve = ToneSettings{}.curve;
        s.channel_curves = ToneSettings{}.channel_curves;
    }
    if (!settings.sections[4]) {
        s.hue.fill(0);
        s.saturation_bands.fill(0);
        s.luminance_bands.fill(0);
    }
    if (!settings.sections[5]) {
        s.grading_hue.fill(0);
        s.grading_saturation.fill(0);
        s.grading_luminance.fill(0);
        s.grading_balance = 0;
    }
    if (!settings.sections[6])
        s.texture = s.clarity = s.dehaze = s.vignette = s.grain = 0;
    if (!settings.sections[7])
        s.sharpening = 0;
    const auto check_cancel = [cancelled] {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("图像处理已取消"));
    };
    check_cancel();
    validate_tone(s);
    if (s == ToneSettings{})
        return source;
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        source.colorSpace() != working_space())
        throw EditError(QStringLiteral("调色需要线性 Rec.2020 FP32 输入"));
    auto without_exposure = s;
    without_exposure.exposure = 0;
    if (without_exposure == ToneSettings{}) {
        auto result = source.copy();
        const float gain = float(std::exp2(s.exposure));
        for (int y = 0; y < result.height(); ++y) {
            check_cancel();
            auto* row = reinterpret_cast<float*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x)
                for (int c = 0; c < 3; ++c) {
                    row[x * 4 + c] *= gain;
                    if (!std::isfinite(row[x * 4 + c]))
                        throw EditError(QStringLiteral("曝光结果超出浮点范围"));
                }
        }
        return result;
    }
    // Use float linear sRGB for hue semantics; extend transfer functions outside the SDR interval.
    const QColorSpace linear_srgb(QColorSpace::Primaries::SRgb,
                                  QColorSpace::TransferFunction::Linear);
    auto image = source.convertedToColorSpace(linear_srgb, source.format());
    const double exposure = std::exp2(s.exposure);
    const std::array<double, 3> balance{std::exp2(s.temperature / 200), std::exp2(-s.tint / 200),
                                        std::exp2(-s.temperature / 200)};
    std::array<std::array<double, 3>, 3> grading_stops{};
    const bool grading = s.grading_saturation != std::array<double, 3>{} ||
                         s.grading_luminance != std::array<double, 3>{};
    for (int band = 0; band < 3; ++band) {
        const auto color = QColor::fromHsvF(s.grading_hue[band] / 360, 1, 1);
        const std::array<double, 3> rgb{color.redF(), color.greenF(), color.blueF()};
        const double luma = rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
        for (int c = 0; c < 3; ++c)
            grading_stops[band][c] = s.grading_luminance[band] / 100 +
                                     (rgb[c] - luma) * s.grading_saturation[band] / 100;
    }
    for (int y = 0; y < image.height(); ++y) {
        check_cancel();
        auto* row = reinterpret_cast<float*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            auto* p = row + 4 * x;
            if (p[3] <= 0)
                continue;
            std::array<double, 3> rgb{};
            for (int c = 0; c < 3; ++c)
                rgb[c] = double(p[c]) / p[3] * exposure * balance[c];
            const double luma = rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
            const double mapped = std::max(luma, 0.) / (std::max(luma, 0.) + .18);
            const double weight = s.shadows / 100 * std::pow(1 - mapped, 2) +
                                  s.highlights / 100 * std::pow(mapped, 2) +
                                  s.whites / 100 * std::pow(mapped, 6) +
                                  s.blacks / 100 * std::pow(1 - mapped, 6);
            for (int c = 0; c < 3; ++c) {
                auto& value = rgb[c];
                value = encoded(value * std::exp2(weight));
                value = .5 + (value - .5) * std::exp2(s.contrast / 100);
                value = (value - s.black_point) / (s.white_point - s.black_point);
                value = std::copysign(std::pow(std::abs(value), 1 / s.gamma), value);
                value = curve(value, s.curve);
                value = curve(value, s.channel_curves[c]);
            }
            const double minimum = *std::min_element(rgb.begin(), rgb.end());
            const double maximum = *std::max_element(rgb.begin(), rgb.end());
            const double delta = maximum - minimum;
            double hue{};
            if (delta > 1e-10) {
                hue = maximum == rgb[0]   ? (rgb[1] - rgb[2]) / delta
                      : maximum == rgb[1] ? 2 + (rgb[2] - rgb[0]) / delta
                                          : 4 + (rgb[0] - rgb[1]) / delta;
                hue = std::fmod(hue * 60 + 360, 360.);
            }
            constexpr std::array<double, 8> centers{0, 30, 60, 120, 180, 240, 270, 300};
            double h{}, sat{}, light{}, weights{};
            for (int i = 0; i < 8; ++i) {
                const double distance =
                    std::min(std::abs(hue - centers[i]), 360 - std::abs(hue - centers[i]));
                const double w = std::max(0., 1 - distance / 60);
                weights += w;
                h += w * s.hue[i] * .6;
                sat += w * s.saturation_bands[i];
                light += w * s.luminance_bands[i];
            }
            h = weights > 0 ? h / weights : 0;
            sat = weights > 0 ? sat / weights : 0;
            light = weights > 0 ? light / weights : 0;
            const double neutral = rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
            const double factor = std::max(
                0., 1 + (s.saturation + sat + s.vibrance * (1 - std::clamp(delta, 0., 1.))) / 100);
            if (delta > 1e-10 && h != 0) {
                const double sector = std::fmod(hue + h + 360, 360.) / 60;
                const double chroma = delta;
                const double secondary = chroma * (1 - std::abs(std::fmod(sector, 2.) - 1));
                const std::array<std::array<double, 3>, 6> colors{{{chroma, secondary, 0},
                                                                   {secondary, chroma, 0},
                                                                   {0, chroma, secondary},
                                                                   {0, secondary, chroma},
                                                                   {secondary, 0, chroma},
                                                                   {chroma, 0, secondary}}};
                for (int c = 0; c < 3; ++c)
                    rgb[c] = colors[int(sector)][c] + minimum;
            }
            for (int c = 0; c < 3; ++c)
                rgb[c] = decoded(neutral + (rgb[c] - neutral) * factor) * std::exp2(light / 100);
            const double final_luma = rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
            if (s.monochrome)
                rgb.fill(final_luma);
            const double position = std::clamp(mapped + s.grading_balance / 200, 0., 1.);
            const std::array<double, 3> grading_weights{(1 - position) * (1 - position),
                                                        2 * position * (1 - position),
                                                        position * position};
            if (grading)
                for (int c = 0; c < 3; ++c) {
                    double stops{};
                    for (int band = 0; band < 3; ++band)
                        stops += grading_weights[band] * grading_stops[band][c];
                    rgb[c] *= std::exp2(stops);
                }
            for (int c = 0; c < 3; ++c) {
                const double value = rgb[c] * p[3];
                if (!std::isfinite(value) ||
                    std::abs(value) > double(std::numeric_limits<float>::max()))
                    throw EditError(QStringLiteral("调色结果超出浮点范围"));
                p[c] = float(value);
            }
        }
    }
    // Spatial effects share the same float pipeline in preview and final output.
    // Radius is relative to image dimensions so proxy previews preserve visual scale.
    if (s.texture != 0 || s.clarity != 0 || s.dehaze != 0 || s.sharpening != 0 || s.vignette != 0 ||
        s.grain != 0) {
        const int w = image.width(), h = image.height();
        std::vector<float> luma(std::size_t(w) * h);
        for (int y = 0; y < h; ++y) {
            check_cancel();
            const auto* row = reinterpret_cast<const float*>(image.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const auto* p = row + x * 4;
                luma[std::size_t(y) * w + x] =
                    p[3] > 0 ? (p[0] * .2126F + p[1] * .7152F + p[2] * .0722F) / p[3] : 0;
            }
        }
        const auto blur = [&](int radius) {
            std::vector<float> horizontal(luma.size()), result(luma.size());
            for (int y = 0; y < h; ++y) {
                check_cancel();
                double sum{};
                for (int x = 0; x < w; ++x) {
                    if (x == 0)
                        for (int k = 0; k <= std::min(radius, w - 1); ++k)
                            sum += luma[std::size_t(y) * w + k];
                    else {
                        if (x + radius < w)
                            sum += luma[std::size_t(y) * w + x + radius];
                        if (x - radius - 1 >= 0)
                            sum -= luma[std::size_t(y) * w + x - radius - 1];
                    }
                    horizontal[std::size_t(y) * w + x] =
                        float(sum / (std::min(w - 1, x + radius) - std::max(0, x - radius) + 1));
                }
            }
            for (int x = 0; x < w; ++x) {
                check_cancel();
                double sum{};
                for (int y = 0; y < h; ++y) {
                    if (y == 0)
                        for (int k = 0; k <= std::min(radius, h - 1); ++k)
                            sum += horizontal[std::size_t(k) * w + x];
                    else {
                        if (y + radius < h)
                            sum += horizontal[std::size_t(y + radius) * w + x];
                        if (y - radius - 1 >= 0)
                            sum -= horizontal[std::size_t(y - radius - 1) * w + x];
                    }
                    result[std::size_t(y) * w + x] =
                        float(sum / (std::min(h - 1, y + radius) - std::max(0, y - radius) + 1));
                }
            }
            return result;
        };
        const auto fine = blur(std::max(1, std::min(w, h) / 500));
        const auto broad = blur(std::max(2, std::min(w, h) / 50));
        // Atmospheric veil estimate from a bounded thumbnail; not Adobe's proprietary algorithm.
        const auto sample = image.scaled(192, 192, Qt::KeepAspectRatio);
        double air = .1;
        for (int y = 0; y < sample.height(); ++y) {
            const auto* p = reinterpret_cast<const float*>(sample.constScanLine(y));
            for (int x = 0; x < sample.width(); ++x)
                if (p[x * 4 + 3] > 0)
                    air = std::max(air, double(std::min({p[x * 4], p[x * 4 + 1], p[x * 4 + 2]}) /
                                               p[x * 4 + 3]));
        }
        for (int y = 0; y < h; ++y) {
            check_cancel();
            auto* row = reinterpret_cast<float*>(image.scanLine(y));
            for (int x = 0; x < w; ++x) {
                auto* p = row + x * 4;
                if (p[3] <= 0)
                    continue;
                const auto i = std::size_t(y) * w + x;
                const double detail = (luma[i] - fine[i]) * (s.texture + s.sharpening) / 100 +
                                      (luma[i] - broad[i]) * s.clarity / 100;
                const double nx = (x + .5) * 2 / w - 1, ny = (y + .5) * 2 / h - 1;
                const double vignette =
                    std::exp2(s.vignette / 50 * std::clamp((nx * nx + ny * ny) / 2, 0., 1.));
                // Deterministic grain, no mutable random generator shared between renders.
                unsigned hash = unsigned(x) * 374761393U + unsigned(y) * 668265263U;
                hash = (hash ^ (hash >> 13)) * 1274126177U;
                const double noise = (double(hash & 65535U) / 65535 - .5) * s.grain / 5000;
                const double veil = std::clamp(double(broad[i]) / air, 0., 1.);
                const double transmission = std::max(.2, 1 - s.dehaze / 125 * veil);
                for (int c = 0; c < 3; ++c) {
                    double value = double(p[c]) / p[3] + detail;
                    if (s.dehaze > 0)
                        value = (value - air) / transmission + air;
                    else if (s.dehaze < 0)
                        value = value * (1 + s.dehaze / 150) - air * s.dehaze / 150;
                    p[c] = float((value * vignette + noise) * p[3]);
                    if (!std::isfinite(p[c]))
                        throw EditError(QStringLiteral("效果结果超出浮点范围"));
                }
            }
        }
    }
    return image.convertedToColorSpace(working_space(), source.format());
}
} // namespace huelith::editing
