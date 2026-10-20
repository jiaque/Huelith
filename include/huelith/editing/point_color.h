#pragma once
#include <QColor>
#include <QJsonArray>
#include <array>
#include <span>
#include <vector>

namespace huelith::editing {
struct PointColor {
    std::array<double, 3> sample{}; // HSL, each in [0,1].
    std::array<double, 3> shift{};  // Hue/saturation/luminance, -100..100.
    double range{50};
    // Outer low, inner low, inner high, outer high. Hue is relative to sample, in turns.
    std::array<double, 4> hue_range{-.08, -.03, .03, .08};
    std::array<double, 4> saturation_range{0, 0, 1, 1};
    std::array<double, 4> luminance_range{0, 0, 1, 1};
    bool operator==(const PointColor&) const = default;
};
[[nodiscard]] PointColor sample_point_color(QColor color);
void validate_point_colors(std::span<const PointColor> colors);
[[nodiscard]] QJsonArray point_colors_json(std::span<const PointColor> colors);
[[nodiscard]] std::vector<PointColor> point_colors_from_json(const QJsonArray& json);
[[nodiscard]] double point_color_weight(const std::array<double, 3>& hsl, const PointColor& color);
// Encoded sRGB with unclipped HDR headroom; operates on straight RGB, never alpha.
[[nodiscard]] std::array<double, 3> apply_point_colors(std::array<double, 3> rgb,
                                                       std::span<const PointColor> colors);
[[nodiscard]] std::array<double, 3> point_color_hsl(std::array<double, 3> rgb);
} // namespace huelith::editing
