#include "huelith/editing/image_storage.h"
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
                         {"vignetteStyle", &ToneSettings::vignette_style},
                         {"vignetteMidpoint", &ToneSettings::vignette_midpoint},
                         {"vignetteRoundness", &ToneSettings::vignette_roundness},
                         {"vignetteFeather", &ToneSettings::vignette_feather},
                         {"vignetteHighlights", &ToneSettings::vignette_highlights},
                         {"grainSize", &ToneSettings::grain_size},
                         {"grainRoughness", &ToneSettings::grain_roughness},
                         {"sharpening", &ToneSettings::sharpening},
                         {"blackPoint", &ToneSettings::black_point},
                         {"whitePoint", &ToneSettings::white_point},
                         {"gamma", &ToneSettings::gamma},
                         {"gradingBalance", &ToneSettings::grading_balance},
                         {"gradingBlending", &ToneSettings::grading_blending}};
double encoded(double value) {
    return value <= .0031308 ? 12.92 * value : 1.055 * std::pow(value, 1 / 2.4) - .055;
}
double decoded(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
QImage convert_linear(const QImage& source, bool to_srgb, bool legacy,
                      const std::atomic_bool* cancelled) {
    const QColorSpace destination =
        to_srgb ? QColorSpace(QColorSpace::SRgbLinear) : working_space();
    if (source.colorSpace() == destination)
        return source;
    if (legacy)
        return source.convertedToColorSpace(destination, source.format());
    return convert_linear_rgb(source, destination, cancelled);
}

class PreparedCurve {
  public:
    PreparedCurve(const std::vector<QPointF>& points, bool legacy)
        : points_(points), legacy_(legacy) {
        slopes_.resize(points.size());
        const auto secant = [&](std::size_t i) {
            return (points[i + 1].y() - points[i].y()) / (points[i + 1].x() - points[i].x());
        };
        if (points.size() == 2) {
            slopes_[0] = slopes_[1] = secant(0);
            return;
        }
        for (std::size_t i = 1; i + 1 < points.size(); ++i) {
            const double a = secant(i - 1), b = secant(i), h0 = points[i].x() - points[i - 1].x(),
                         h1 = points[i + 1].x() - points[i].x();
            slopes_[i] = a * b <= 0 ? 0 : (3 * (h0 + h1)) / ((2 * h1 + h0) / a + (h1 + 2 * h0) / b);
        }
        const auto endpoint = [](double h0, double h1, double a, double b) {
            double slope = ((2 * h0 + h1) * a - h0 * b) / (h0 + h1);
            if (slope * a <= 0)
                return 0.;
            if (a * b < 0 && std::abs(slope) > std::abs(3 * a))
                return 3 * a;
            return slope;
        };
        slopes_.front() = endpoint(points[1].x() - points[0].x(), points[2].x() - points[1].x(),
                                   secant(0), secant(1));
        const auto n = points.size();
        slopes_.back() =
            endpoint(points[n - 1].x() - points[n - 2].x(), points[n - 2].x() - points[n - 3].x(),
                     secant(n - 2), secant(n - 3));
    }
    double operator()(double value) const {
        auto upper = std::upper_bound(points_.begin(), points_.end(), value,
                                      [](double x, QPointF p) { return x < p.x(); });
        if (upper == points_.begin())
            ++upper;
        if (upper == points_.end())
            --upper;
        const auto a = *(upper - 1), b = *upper;
        const double width = b.x() - a.x(), t = (value - a.x()) / width;
        if (legacy_)
            return a.y() + t * (b.y() - a.y());
        const auto i = std::size_t(upper - points_.begin() - 1);
        if (t < 0)
            return a.y() + (value - a.x()) * slopes_[i];
        if (t > 1)
            return b.y() + (value - b.x()) * slopes_[i + 1];
        const double t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * a.y() + (t3 - 2 * t2 + t) * width * slopes_[i] +
               (-2 * t3 + 3 * t2) * b.y() + (t3 - t2) * width * slopes_[i + 1];
    }

  private:
    const std::vector<QPointF>& points_;
    std::vector<double> slopes_;
    bool legacy_;
};

double smooth(double value) {
    value = std::clamp(value, 0., 1.);
    return value * value * (3 - 2 * value);
}
double noise_hash(int x, int y) {
    unsigned hash = unsigned(x) * 374761393U + unsigned(y) * 668265263U;
    hash = (hash ^ (hash >> 13)) * 1274126177U;
    return double(hash & 65535U) / 65535 - .5;
}
double coherent_noise(double x, double y) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double u = smooth(x - ix), v = smooth(y - iy);
    return std::lerp(std::lerp(noise_hash(ix, iy), noise_hash(ix + 1, iy), u),
                     std::lerp(noise_hash(ix, iy + 1), noise_hash(ix + 1, iy + 1), u), v);
}
double vignette_coverage(double nx, double ny, int width, int height, const ToneSettings& s) {
    const double exponent = s.vignette_roundness < 0 ? 2 - s.vignette_roundness / 5 : 2;
    if (s.vignette_roundness > 0) {
        const double circular = s.vignette_roundness / 100;
        nx *= std::lerp(1., double(width) / std::min(width, height), circular);
        ny *= std::lerp(1., double(height) / std::min(width, height), circular);
    }
    const double radius =
        exponent == 2
            ? std::hypot(nx, ny)
            : std::pow(std::pow(std::abs(nx), exponent) + std::pow(std::abs(ny), exponent),
                       1 / exponent);
    const double boundary = .15 + s.vignette_midpoint / 100 * .85;
    const double feather = s.vignette_feather / 100 * .8;
    return feather > 0 ? smooth((radius - boundary + feather / 2) / feather)
                       : double(radius >= boundary);
}
} // namespace
double evaluate_point_curve(double value, const std::vector<QPointF>& points, bool legacy) {
    if (!std::isfinite(value) || points.size() < 2 || points.size() > 64)
        throw EditError(QStringLiteral("曲线参数无效"));
    for (std::size_t i = 0; i < points.size(); ++i)
        if (!std::isfinite(points[i].x()) || !std::isfinite(points[i].y()) ||
            (i && points[i].x() <= points[i - 1].x()))
            throw EditError(QStringLiteral("曲线节点无效"));
    return PreparedCurve(points, legacy)(value);
}
double evaluate_parametric_curve(double value, const ToneSettings& settings) {
    if (value <= 0 || value >= 1)
        return value;
    const auto& split = settings.curve_splits;
    const std::array<double, 4> centers{split[0] * .5, (split[0] + split[1]) * .5,
                                        (split[1] + split[2]) * .5, (split[2] + 1) * .5};
    double adjustment = settings.parametric_curve.front();
    if (value >= centers.back())
        adjustment = settings.parametric_curve.back();
    else
        for (int i = 0; i < 3; ++i)
            if (value >= centers[i] && value < centers[i + 1]) {
                const double t = smooth((value - centers[i]) / (centers[i + 1] - centers[i]));
                adjustment =
                    std::lerp(settings.parametric_curve[i], settings.parametric_curve[i + 1], t);
                break;
            }
    return value + value * (1 - value) * adjustment * .01;
}
void validate_tone(const ToneSettings& settings) {
    for (double v : settings.parametric_curve)
        if (!std::isfinite(v) || v < -100 || v > 100)
            throw EditError(QStringLiteral("参数曲线数量无效"));
    for (std::size_t i = 0; i < 3; ++i)
        if (!std::isfinite(settings.curve_splits[i]) || settings.curve_splits[i] < .01 ||
            settings.curve_splits[i] > .99 ||
            (i && settings.curve_splits[i] - settings.curve_splits[i - 1] < .009999))
            throw EditError(QStringLiteral("曲线区域边界必须递增"));
    if (settings.grading_blending < 0)
        throw EditError(QStringLiteral("分级混合参数必须在 0–100 范围内"));
    validate_calibration(settings.calibration);
    validate_detail(settings.details);
    validate_point_colors(settings.point_colors);
    if (settings.vignette_style < 0 || settings.vignette_style > 2 ||
        std::floor(settings.vignette_style) != settings.vignette_style ||
        settings.vignette_midpoint < 0 || settings.vignette_feather < 0 ||
        settings.vignette_highlights < 0 || settings.grain_size < 0 || settings.grain_roughness < 0)
        throw EditError(QStringLiteral("晕影或颗粒参数无效"));
    for (const auto& [name, member] : fields) {
        const double limit = QLatin1String(name) == "sharpening" ? 150 : 100;
        if (!std::isfinite(settings.*member) || std::abs(settings.*member) > limit)
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
    for (int i = 0; i < 4; ++i) {
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
    QJsonObject json{{"version", 8},
                     {"legacyCurveInterpolation", settings.legacy_curve_interpolation},
                     {"legacyColorTransform", settings.legacy_color_transform},
                     {"legacyFilmEffects", settings.legacy_film_effects},
                     {"details", detail_json(settings.details)},
                     {"monochrome", settings.monochrome},
                     {"calibration", calibration_json(settings.calibration)},
                     {"pointColors", point_colors_json(settings.point_colors)}};
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
    QJsonArray parametric, splits;
    for (double v : settings.parametric_curve)
        parametric.append(v);
    for (double v : settings.curve_splits)
        splits.append(v);
    json.insert("parametricCurve", parametric);
    json.insert("curveSplits", splits);
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
    if (json.value("version").toInt() < 1 || json.value("version").toInt() > 8)
        throw EditError(QStringLiteral("未知调色配方版本"));
    settings.legacy_curve_interpolation = json.value("version").toInt() < 8;
    if (json.contains("legacyCurveInterpolation")) {
        if (!json.value("legacyCurveInterpolation").isBool())
            throw EditError(QStringLiteral("曲线兼容标记无效"));
        settings.legacy_curve_interpolation = json.value("legacyCurveInterpolation").toBool();
    }
    if (json.contains("parametricCurve")) {
        const auto values = json.value("parametricCurve").toArray();
        if (values.size() != 4)
            throw EditError(QStringLiteral("参数曲线需要四个区域"));
        for (int i = 0; i < 4; ++i) {
            if (!values[i].isDouble())
                throw EditError(QStringLiteral("参数曲线格式无效"));
            settings.parametric_curve[i] = values[i].toDouble();
        }
    }
    if (json.contains("curveSplits")) {
        const auto values = json.value("curveSplits").toArray();
        if (values.size() != 3)
            throw EditError(QStringLiteral("曲线区域边界无效"));
        for (int i = 0; i < 3; ++i) {
            if (!values[i].isDouble())
                throw EditError(QStringLiteral("曲线区域格式无效"));
            settings.curve_splits[i] = values[i].toDouble();
        }
    }
    settings.legacy_film_effects = json.value("version").toInt() < 6;
    settings.legacy_color_transform = json.value("version").toInt() < 7;
    if (json.contains("legacyColorTransform")) {
        if (!json.value("legacyColorTransform").isBool())
            throw EditError(QStringLiteral("色彩转换兼容标记无效"));
        settings.legacy_color_transform = json.value("legacyColorTransform").toBool();
    }
    if (json.contains("legacyFilmEffects")) {
        if (!json.value("legacyFilmEffects").isBool())
            throw EditError(QStringLiteral("效果兼容版本格式无效"));
        settings.legacy_film_effects = json.value("legacyFilmEffects").toBool();
    }
    settings.details.legacy_sharpening = json.value("version").toInt() < 5;
    if (json.contains("details")) {
        if (!json.value("details").isObject())
            throw EditError(QStringLiteral("细节配方格式无效"));
        settings.details = detail_from_json(json.value("details").toObject());
    }
    const auto defaults = tone_json(settings);
    if (json.contains("sections")) {
        const auto sections = json.value("sections").toArray();
        const int expected = json.value("version").toInt() >= 4 ? 9 : 8;
        if (sections.size() != expected)
            throw EditError(QStringLiteral("调色分组开关数量无效"));
        for (int i = 0; i < expected; ++i) {
            if (!sections[i].isBool())
                throw EditError(QStringLiteral("调色分组开关必须为布尔值"));
            settings.sections[i] = sections[i].toBool();
        }
    }
    if (json.contains("calibration")) {
        if (!json.value("calibration").isObject())
            throw EditError(QStringLiteral("校正配方格式无效"));
        settings.calibration = calibration_from_json(json.value("calibration").toObject());
    }
    if (json.contains("pointColors")) {
        if (!json.value("pointColors").isArray())
            throw EditError(QStringLiteral("点颜色配方格式无效"));
        settings.point_colors = point_colors_from_json(json.value("pointColors").toArray());
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
        if (values.size() != 3 && values.size() != 4)
            throw EditError(QStringLiteral("需要阴影、中间调、高光和可选全局分级参数"));
        for (int c = 0; c < values.size(); ++c) {
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
                  const std::atomic_bool* cancelled, double pixel_scale, int detail_preview,
                  QRectF effect_bounds) {
    if (effect_bounds.isEmpty())
        effect_bounds = QRectF(source.rect());
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
        s.parametric_curve = {};
        s.curve = ToneSettings{}.curve;
        s.channel_curves = ToneSettings{}.channel_curves;
    }
    if (!settings.sections[4]) {
        s.point_colors.clear();
        s.hue.fill(0);
        s.saturation_bands.fill(0);
        s.luminance_bands.fill(0);
    }
    if (!settings.sections[5]) {
        s.grading_hue.fill(0);
        s.grading_saturation.fill(0);
        s.grading_luminance.fill(0);
        s.grading_balance = 0;
        s.grading_blending = 50;
    }
    if (!settings.sections[6])
        s.texture = s.clarity = s.dehaze = s.vignette = s.grain = 0;
    if (s.vignette == 0 && s.grain == 0) {
        s.legacy_film_effects = false;
        s.vignette_style = s.vignette_roundness = s.vignette_highlights = 0;
        s.vignette_midpoint = s.vignette_feather = s.grain_roughness = 50;
        s.grain_size = 25;
    }
    if (!settings.sections[7]) {
        s.sharpening = 0;
        s.details = {};
    }
    if (detail_preview == 0 && s.sharpening == 0 && s.details.luminance == 0 &&
        s.details.color == 0)
        s.details = {};
    if (!settings.sections[8])
        s.calibration = {};
    std::erase_if(s.point_colors,
                  [](const PointColor& color) { return color.shift == std::array<double, 3>{}; });
    const auto check_cancel = [cancelled] {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("图像处理已取消"));
    };
    check_cancel();
    validate_tone(s);
    auto neutral_settings = ToneSettings{};
    neutral_settings.legacy_color_transform = s.legacy_color_transform;
    neutral_settings.legacy_curve_interpolation=s.legacy_curve_interpolation;
    if(s.parametric_curve==std::array<double,4>{})s.curve_splits=ToneSettings{}.curve_splits;
    if (detail_preview == 0 && s == neutral_settings)
        return source;
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        source.colorSpace() != working_space())
        throw EditError(QStringLiteral("调色需要线性 Rec.2020 FP32 输入"));
    auto without_exposure = s;
    without_exposure.exposure = 0;
    if (detail_preview == 0 && without_exposure == neutral_settings) {
        auto result = copy_image(source);
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
    auto image = convert_linear(source, true, s.legacy_color_transform, cancelled);
    const double exposure = std::exp2(s.exposure);
    const auto calibration = calibration_transform(s.calibration);
    const std::array<double, 3> balance{std::exp2(s.temperature / 200), std::exp2(-s.tint / 200),
                                        std::exp2(-s.temperature / 200)};
    std::array<std::array<double, 3>, 4> grading_stops{};
    const bool grading = s.grading_saturation != std::array<double, 4>{} ||
                         s.grading_luminance != std::array<double, 4>{};
    for (int band = 0; band < 4; ++band) {
        const auto color = QColor::fromHsvF(s.grading_hue[band] / 360, 1, 1);
        const std::array<double, 3> rgb{color.redF(), color.greenF(), color.blueF()};
        const double luma = rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
        for (int c = 0; c < 3; ++c)
            grading_stops[band][c] = s.grading_luminance[band] / 100 +
                                     (rgb[c] - luma) * s.grading_saturation[band] / 100;
    }
    const PreparedCurve composite_curve(s.curve, s.legacy_curve_interpolation);
    const std::array<PreparedCurve, 3> channel_curves{
        PreparedCurve(s.channel_curves[0], s.legacy_curve_interpolation),
        PreparedCurve(s.channel_curves[1], s.legacy_curve_interpolation),
        PreparedCurve(s.channel_curves[2], s.legacy_curve_interpolation)};
    const bool parametric_active = std::any_of(s.parametric_curve.begin(), s.parametric_curve.end(),
                                               [](double v) { return v != 0; });
    auto color_settings = s;
    color_settings.texture = color_settings.clarity = color_settings.dehaze = 0;
    color_settings.vignette = color_settings.grain = color_settings.sharpening = 0;
    color_settings.vignette_style = color_settings.vignette_roundness =
        color_settings.vignette_highlights = 0;
    color_settings.vignette_midpoint = color_settings.vignette_feather =
        color_settings.grain_roughness = 50;
    color_settings.grain_size = 25;
    color_settings.details = {};
    color_settings.point_colors.clear();
    const bool color_active = color_settings != neutral_settings;
    const bool film_only = !color_active && s.point_colors.empty() && s.texture == 0 &&
                           s.clarity == 0 && s.dehaze == 0 && s.sharpening == 0 &&
                           s.details == DetailSettings{} && detail_preview == 0;
    if (film_only)
        image = source;
    for (int y = 0; color_active && y < image.height(); ++y) {
        check_cancel();
        auto* row = reinterpret_cast<float*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            auto* p = row + 4 * x;
            if (p[3] <= 0)
                continue;
            std::array<double, 3> rgb{};
            for (int c = 0; c < 3; ++c)
                rgb[c] = double(p[c]) / p[3] * exposure * balance[c];
            rgb = calibration.apply(rgb);
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
                if (parametric_active)
                    value = evaluate_parametric_curve(value, s);
                value = composite_curve(value);
                value = channel_curves[c](value);
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
            std::array<double, 3> grading_weights{(1 - position) * (1 - position),
                                                  2 * position * (1 - position),
                                                  position * position};
            if (grading && s.grading_blending != 50) {
                double total{};
                for (auto& grade_weight : grading_weights) {
                    grade_weight =
                        std::pow(grade_weight, std::exp2((50 - s.grading_blending) / 25));
                    total += grade_weight;
                }
                for (auto& grade_weight : grading_weights)
                    grade_weight /= total;
            }
            if (grading)
                for (int c = 0; c < 3; ++c) {
                    double stops{grading_stops[3][c]};
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
    image = apply_detail(image, s.details, std::max(0., s.sharpening), cancelled, pixel_scale,
                         detail_preview);
    if (detail_preview != 0)
        return convert_linear(image, false, s.legacy_color_transform, cancelled);
    const double legacy_sharpening = s.details.legacy_sharpening ? s.sharpening : 0;
    if (s.texture != 0 || s.clarity != 0 || s.dehaze != 0 || legacy_sharpening != 0 ||
        s.vignette != 0 || s.grain != 0) {
        const int w = image.width(), h = image.height();
        std::vector<float> luma(std::size_t(w) * h);
        for (int y = 0; y < h; ++y) {
            check_cancel();
            const auto* row = reinterpret_cast<const float*>(image.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const auto* p = row + x * 4;
                luma[std::size_t(y) * w + x] =
                    p[3] > 0 ? (film_only ? p[0] * .2627F + p[1] * .678F + p[2] * .0593F
                                          : p[0] * .2126F + p[1] * .7152F + p[2] * .0722F) /
                                   p[3]
                             : 0;
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
        const auto fine = s.texture != 0 || legacy_sharpening != 0
                              ? blur(std::max(1, std::min(w, h) / 500))
                              : std::vector<float>{};
        const auto broad = s.clarity != 0 || s.dehaze != 0 ? blur(std::max(2, std::min(w, h) / 50))
                                                           : std::vector<float>{};
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
                const double detail =
                    (fine.empty() ? 0
                                  : (luma[i] - fine[i]) * (s.texture + legacy_sharpening) / 100) +
                    (broad.empty() ? 0 : (luma[i] - broad[i]) * s.clarity / 100);
                const double nx = (x + .5) * 2 / w - 1, ny = (y + .5) * 2 / h - 1;
                const double vignette =
                    std::exp2(s.vignette / 50 * std::clamp((nx * nx + ny * ny) / 2, 0., 1.));
                const double coverage =
                    s.vignette != 0
                        ? vignette_coverage(
                              (x + .5 - effect_bounds.x()) * 2 / effect_bounds.width() - 1,
                              (y + .5 - effect_bounds.y()) * 2 / effect_bounds.height() - 1,
                              qRound(effect_bounds.width()), qRound(effect_bounds.height()), s)
                        : 0;
                const double spacing = .6 + s.grain_size / 100 * 8;
                const double gx = (x + .5) / (pixel_scale * spacing),
                             gy = (y + .5) / (pixel_scale * spacing);
                const double rough = s.grain_roughness / 100;
                const double noise =
                    s.grain == 0 ? 0
                    : s.legacy_film_effects
                        ? noise_hash(x, y) * s.grain / 5000
                        : (coherent_noise(gx, gy) * (1 - rough * .65) +
                           coherent_noise(gx * 2.37 + 17, gy * 2.37 + 31) * rough * .65) *
                              s.grain / 500 * std::min(1., std::sqrt(pixel_scale * spacing));
                const double veil = broad.empty() ? 0 : std::clamp(double(broad[i]) / air, 0., 1.);
                const double transmission = std::max(.2, 1 - s.dehaze / 125 * veil);
                for (int c = 0; c < 3; ++c) {
                    double value = double(p[c]) / p[3] + detail;
                    if (s.dehaze > 0)
                        value = (value - air) / transmission + air;
                    else if (s.dehaze < 0)
                        value = value * (1 + s.dehaze / 150) - air * s.dehaze / 150;
                    if (s.legacy_film_effects)
                        value *= vignette;
                    else if (s.vignette != 0) {
                        const double protection =
                            s.vignette < 0 && s.vignette_style != 2
                                ? 1 - s.vignette_highlights / 100 * smooth((luma[i] - .35) / .65)
                                : 1;
                        const double weight = std::abs(s.vignette) / 100 * coverage * protection;
                        if (s.vignette_style == 2)
                            value = std::lerp(value, s.vignette > 0 ? 1. : 0., weight);
                        else if (s.vignette_style == 1)
                            value *= std::exp2(s.vignette / 50 * coverage * protection);
                        else {
                            const double brightness = encoded(std::max(0., double(luma[i])));
                            const double shifted = s.vignette < 0
                                                       ? brightness * (1 - weight)
                                                       : std::lerp(brightness, 1., weight);
                            value *= luma[i] > 1e-8 ? decoded(shifted) / luma[i] : 1;
                        }
                    }
                    // Film grain belongs to perceptual density; additive scene-linear noise
                    // overwhelms dark RAW shadows and creates clipped black/white clumps.
                    value = s.legacy_film_effects ? value + noise
                            : noise == 0          ? value
                                                  : decoded(encoded(value) + noise);
                    p[c] = float(value * p[3]);
                    if (!std::isfinite(p[c]))
                        throw EditError(QStringLiteral("效果结果超出浮点范围"));
                }
            }
        }
    }
    if (!s.point_colors.empty()) {
        for (int y = 0; y < image.height(); ++y) {
            check_cancel();
            auto* row = reinterpret_cast<float*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                auto* p = row + x * 4;
                if (p[3] <= 0)
                    continue;
                const auto adjusted = apply_point_colors(
                    {encoded(p[0] / p[3]), encoded(p[1] / p[3]), encoded(p[2] / p[3])},
                    s.point_colors);
                for (int c = 0; c < 3; ++c)
                    p[c] = float(decoded(adjusted[c]) * p[3]);
            }
        }
    }
    return convert_linear(image, false, s.legacy_color_transform, cancelled);
}
} // namespace huelith::editing
