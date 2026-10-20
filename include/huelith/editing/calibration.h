#pragma once
#include <QJsonObject>
#include <array>

namespace huelith::editing {
struct CalibrationSettings {
    int process_version{1};
    double shadow_tint{};
    std::array<double, 3> hue{};
    std::array<double, 3> saturation{};
    bool operator==(const CalibrationSettings&) const = default;
};
void validate_calibration(const CalibrationSettings& settings);
[[nodiscard]] QJsonObject calibration_json(const CalibrationSettings& settings);
[[nodiscard]] CalibrationSettings calibration_from_json(const QJsonObject& json);

// Linear sRGB primary adjustment, adapted to D65. Matrix rows preserve neutral white.
// Version 1 is Huelith's own deterministic process, not an Adobe process version.
struct CalibrationTransform {
    std::array<std::array<double, 3>, 3> matrix{};
    double shadow_tint{};
    bool active{};
    [[nodiscard]] std::array<double, 3> apply(std::array<double, 3> rgb) const;
};
[[nodiscard]] CalibrationTransform calibration_transform(const CalibrationSettings& settings);
} // namespace huelith::editing
