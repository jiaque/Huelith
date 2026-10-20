#include "huelith/editing/calibration.h"
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace huelith::editing {
namespace {
using Matrix = std::array<std::array<double, 3>, 3>;
using Vector = std::array<double, 3>;
constexpr Matrix identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
constexpr Matrix srgb_xyz{{{.4124564, .3575761, .1804375},
                           {.2126729, .7151522, .0721750},
                           {.0193339, .1191920, .9503041}}};
Vector multiply(const Matrix& matrix, const Vector& value) {
    Vector result{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            result[row] += matrix[row][column] * value[column];
    return result;
}
Matrix inverse(const Matrix& m) {
    Matrix result{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            result[column][row] =
                m[(row + 1) % 3][(column + 1) % 3] * m[(row + 2) % 3][(column + 2) % 3] -
                m[(row + 1) % 3][(column + 2) % 3] * m[(row + 2) % 3][(column + 1) % 3];
    const double determinant =
        m[0][0] * result[0][0] + m[0][1] * result[1][0] + m[0][2] * result[2][0];
    if (std::abs(determinant) < 1e-10)
        throw EditError(QStringLiteral("校正原色矩阵不可逆"));
    for (auto& row : result)
        for (auto& value : row)
            value /= determinant;
    return result;
}
} // namespace

void validate_calibration(const CalibrationSettings& settings) {
    if (settings.process_version != 1)
        throw EditError(QStringLiteral("未知 RAW 校正处理版本"));
    const auto valid = [](double value) { return std::isfinite(value) && std::abs(value) <= 100; };
    if (!valid(settings.shadow_tint) ||
        !std::all_of(settings.hue.begin(), settings.hue.end(), valid) ||
        !std::all_of(settings.saturation.begin(), settings.saturation.end(), valid))
        throw EditError(QStringLiteral("校正参数必须在 -100 到 100 之间"));
}
QJsonObject calibration_json(const CalibrationSettings& settings) {
    validate_calibration(settings);
    QJsonArray hue, saturation;
    for (int i = 0; i < 3; ++i) {
        hue.append(settings.hue[i]);
        saturation.append(settings.saturation[i]);
    }
    return {{"processVersion", settings.process_version},
            {"shadowTint", settings.shadow_tint},
            {"hue", hue},
            {"saturation", saturation}};
}
CalibrationSettings calibration_from_json(const QJsonObject& json) {
    CalibrationSettings settings;
    const auto defaults = calibration_json(settings);
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!defaults.contains(it.key()))
            throw EditError(QStringLiteral("未知校正配方字段"));
    if (json.contains("processVersion")) {
        if (!json.value("processVersion").isDouble() ||
            json.value("processVersion").toDouble() != 1)
            throw EditError(QStringLiteral("未知 RAW 校正处理版本"));
    }
    if (json.contains("shadowTint")) {
        if (!json.value("shadowTint").isDouble())
            throw EditError(QStringLiteral("阴影色调格式无效"));
        settings.shadow_tint = json.value("shadowTint").toDouble();
    }
    for (const auto& [name, target] :
         {std::pair{"hue", &settings.hue}, std::pair{"saturation", &settings.saturation}}) {
        if (!json.contains(QLatin1String(name)))
            continue;
        const auto values = json.value(QLatin1String(name)).toArray();
        if (values.size() != 3)
            throw EditError(QStringLiteral("校正需要红、绿、蓝三原色参数"));
        for (int i = 0; i < 3; ++i) {
            if (!values[i].isDouble())
                throw EditError(QStringLiteral("原色校正数值无效"));
            (*target)[i] = values[i].toDouble();
        }
    }
    validate_calibration(settings);
    return settings;
}
CalibrationTransform calibration_transform(const CalibrationSettings& settings) {
    validate_calibration(settings);
    CalibrationTransform result{identity, settings.shadow_tint, settings != CalibrationSettings{}};
    if (settings.hue == Vector{} && settings.saturation == Vector{})
        return result;
    const auto white = multiply(srgb_xyz, Vector{1, 1, 1});
    const double sum = white[0] + white[1] + white[2];
    const double wx = white[0] / sum, wy = white[1] / sum;
    Matrix primaries{};
    for (int column = 0; column < 3; ++column) {
        const double total = srgb_xyz[0][column] + srgb_xyz[1][column] + srgb_xyz[2][column];
        const double x = srgb_xyz[0][column] / total - wx;
        const double y = srgb_xyz[1][column] / total - wy;
        const double angle = settings.hue[column] * .15 * std::numbers::pi / 180;
        const double radius = std::exp2(settings.saturation[column] / 200);
        const double nx = wx + radius * (x * std::cos(angle) - y * std::sin(angle));
        const double ny = wy + radius * (x * std::sin(angle) + y * std::cos(angle));
        // Homogeneous XYZ chromaticities avoid division near a primary's y=0 boundary.
        primaries[0][column] = nx;
        primaries[1][column] = ny;
        primaries[2][column] = 1 - nx - ny;
    }
    const auto scale = multiply(inverse(primaries), white);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            primaries[row][column] *= scale[column];
    const auto from_xyz = inverse(srgb_xyz);
    result.matrix = {};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            for (int k = 0; k < 3; ++k)
                result.matrix[row][column] += from_xyz[row][k] * primaries[k][column];
    return result;
}
std::array<double, 3> CalibrationTransform::apply(std::array<double, 3> rgb) const {
    if (!active)
        return rgb;
    rgb = multiply(matrix, rgb);
    if (shadow_tint == 0)
        return rgb;
    const double luma = rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
    const double weight = std::pow(.18 / (.18 + std::max(0., luma)), 2);
    const double amount = shadow_tint / 100 * weight;
    const Vector gains{std::exp2(amount * .5), std::exp2(-amount * .5), std::exp2(amount * .5)};
    const double normalization = gains[0] * .2126 + gains[1] * .7152 + gains[2] * .0722;
    for (int c = 0; c < 3; ++c)
        rgb[c] *= gains[c] / normalization;
    return rgb;
}
} // namespace huelith::editing
