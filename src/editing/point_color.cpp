#include "huelith/editing/point_color.h"
#include "huelith/editing/project.h"
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
namespace {
template <std::size_t N> QJsonArray array_json(const std::array<double, N>& values) {
    QJsonArray result;
    for (double value : values)
        result.append(value);
    return result;
}
template <std::size_t N>
void read_array(const QJsonObject& json, const char* name, std::array<double, N>& target) {
    if (!json.contains(QLatin1String(name)))
        return;
    const auto values = json.value(QLatin1String(name)).toArray();
    if (values.size() != N)
        throw EditError(QStringLiteral("点颜色参数数量无效"));
    for (std::size_t i = 0; i < N; ++i) {
        if (!values[qsizetype(i)].isDouble())
            throw EditError(QStringLiteral("点颜色参数格式无效"));
        target[i] = values[qsizetype(i)].toDouble();
    }
}
double trapezoid(double value, const std::array<double, 4>& edges, double center,
                 double expansion) {
    const double x = center + (value - center) / expansion;
    if (x < edges[0] || x > edges[3])
        return 0;
    if (x >= edges[1] && x <= edges[2])
        return 1;
    const double t = x < edges[1] ? (x - edges[0]) / std::max(1e-12, edges[1] - edges[0])
                                  : (edges[3] - x) / std::max(1e-12, edges[3] - edges[2]);
    return t * t * (3 - 2 * t);
}
std::array<double, 3> hsl_rgb(const std::array<double, 3>& hsl) {
    const double chroma = (1 - std::abs(2 * hsl[2] - 1)) * hsl[1];
    const double sector = hsl[0] * 6;
    const double secondary = chroma * (1 - std::abs(std::fmod(sector, 2.) - 1));
    const std::array<std::array<double, 3>, 6> colors{{{chroma, secondary, 0},
                                                       {secondary, chroma, 0},
                                                       {0, chroma, secondary},
                                                       {0, secondary, chroma},
                                                       {secondary, 0, chroma},
                                                       {chroma, 0, secondary}}};
    auto result = colors[std::clamp(int(sector), 0, 5)];
    for (auto& value : result)
        value += hsl[2] - chroma / 2;
    return result;
}
} // namespace
std::array<double, 3> point_color_hsl(std::array<double, 3> rgb) {
    const auto [low, high] = std::minmax_element(rgb.begin(), rgb.end());
    const double minimum = *low, maximum = *high, delta = maximum - minimum;
    const double light = (maximum + minimum) / 2;
    if (delta < 1e-12)
        return {0, 0, light};
    double hue = maximum == rgb[0]   ? (rgb[1] - rgb[2]) / delta
                 : maximum == rgb[1] ? 2 + (rgb[2] - rgb[0]) / delta
                                     : 4 + (rgb[0] - rgb[1]) / delta;
    hue = std::fmod(hue / 6 + 1, 1.);
    const double saturation = delta / std::max(1e-12, 1 - std::abs(2 * light - 1));
    return {hue, std::clamp(saturation, 0., 1.), light};
}
PointColor sample_point_color(QColor color) {
    PointColor result;
    result.sample = point_color_hsl({color.redF(), color.greenF(), color.blueF()});
    const auto range = [](double center) {
        return std::array<double, 4>{std::max(0., center - .35), std::max(0., center - .15),
                                     std::min(1., center + .15), std::min(1., center + .35)};
    };
    result.saturation_range = range(result.sample[1]);
    result.luminance_range = range(result.sample[2]);
    return result;
}
void validate_point_colors(std::span<const PointColor> colors) {
    if (colors.size() > 8)
        throw EditError(QStringLiteral("点颜色最多保留8个样本"));
    for (const auto& color : colors) {
        if (!std::isfinite(color.range) || color.range < 0 || color.range > 100)
            throw EditError(QStringLiteral("点颜色范围无效"));
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(color.sample[c]) || color.sample[c] < 0 || color.sample[c] > 1 ||
                !std::isfinite(color.shift[c]) || std::abs(color.shift[c]) > 100)
                throw EditError(QStringLiteral("点颜色样本或偏移无效"));
            const auto& edges = c == 0   ? color.hue_range
                                : c == 1 ? color.saturation_range
                                         : color.luminance_range;
            for (int i = 0; i < 4; ++i)
                if (!std::isfinite(edges[i]) || edges[i] < (c == 0 ? -.5 : 0) ||
                    edges[i] > (c == 0 ? .5 : 1) || (i > 0 && edges[i] < edges[i - 1]))
                    throw EditError(QStringLiteral("点颜色范围端点顺序或数值无效"));
        }
    }
}
QJsonArray point_colors_json(std::span<const PointColor> colors) {
    validate_point_colors(colors);
    QJsonArray result;
    for (const auto& c : colors)
        result.append(QJsonObject{{"sample", array_json(c.sample)},
                                  {"shift", array_json(c.shift)},
                                  {"range", c.range},
                                  {"hueRange", array_json(c.hue_range)},
                                  {"saturationRange", array_json(c.saturation_range)},
                                  {"luminanceRange", array_json(c.luminance_range)}});
    return result;
}
std::vector<PointColor> point_colors_from_json(const QJsonArray& json) {
    std::vector<PointColor> result;
    if (json.size() > 8)
        throw EditError(QStringLiteral("点颜色最多保留8个样本"));
    for (const auto item : json) {
        if (!item.isObject())
            throw EditError(QStringLiteral("点颜色配方格式无效"));
        const auto object = item.toObject();
        const QStringList keys{"sample",   "shift",           "range",
                               "hueRange", "saturationRange", "luminanceRange"};
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!keys.contains(it.key()))
                throw EditError(QStringLiteral("未知点颜色配方字段"));
        if (!object.contains("sample"))
            throw EditError(QStringLiteral("点颜色缺少样本"));
        PointColor color;
        read_array(object, "sample", color.sample);
        read_array(object, "shift", color.shift);
        read_array(object, "hueRange", color.hue_range);
        read_array(object, "saturationRange", color.saturation_range);
        read_array(object, "luminanceRange", color.luminance_range);
        if (object.contains("range")) {
            if (!object.value("range").isDouble())
                throw EditError(QStringLiteral("点颜色范围格式无效"));
            color.range = object.value("range").toDouble();
        }
        result.push_back(color);
    }
    validate_point_colors(result);
    return result;
}
double point_color_weight(const std::array<double, 3>& hsl, const PointColor& color) {
    const double distance = std::remainder(hsl[0] - color.sample[0], 1.);
    const double expansion = std::exp2((color.range - 50) / 50);
    return (hsl[1] < .005 && color.sample[1] > .05
                ? 0
                : trapezoid(distance, color.hue_range, 0, expansion)) *
           trapezoid(hsl[1], color.saturation_range, color.sample[1], expansion) *
           trapezoid(hsl[2], color.luminance_range, color.sample[2], expansion);
}
std::array<double, 3> apply_point_colors(std::array<double, 3> rgb,
                                         std::span<const PointColor> colors) {
    const double floor = std::min(0., *std::min_element(rgb.begin(), rgb.end()));
    const double scale = std::max(1., *std::max_element(rgb.begin(), rgb.end()) - floor);
    auto normalized = rgb;
    for (auto& value : normalized)
        value = (value - floor) / scale;
    const auto hsl = point_color_hsl(normalized);
    std::array<double, 3> shift{};
    for (const auto& color : colors) {
        const double weight = point_color_weight(hsl, color);
        for (int i = 0; i < 3; ++i)
            shift[i] += color.shift[i] * weight;
    }
    if (shift == std::array<double, 3>{})
        return rgb;
    auto adjusted = hsl;
    adjusted[0] = std::fmod(hsl[0] + std::clamp(shift[0], -100., 100.) / 600 + 1, 1.);
    for (int c = 1; c < 3; ++c) {
        const double amount = std::clamp(shift[c] / 100, -1., 1.);
        adjusted[c] = std::clamp(hsl[c] + amount * (amount > 0 ? 1 - hsl[c] : hsl[c]), 0., 1.);
    }
    auto result = hsl_rgb(adjusted);
    for (auto& value : result)
        value = value * scale + floor;
    return result;
}
} // namespace huelith::editing
