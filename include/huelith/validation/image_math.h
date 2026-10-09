#pragma once
#include <array>
#include <span>
#include <vector>

namespace huelith::validation {
using Pixel = std::array<float, 4>; // linear-light premultiplied RGBA
enum class BlendMode { normal, multiply, screen };
[[nodiscard]] Pixel composite(Pixel source, Pixel backdrop, BlendMode mode);
[[nodiscard]] std::vector<float> paint_range(std::span<const float> input, int width, int height,
                                             float x, float y, float radius, float strength,
                                             bool subtract);
struct Point {
    float x{};
    float y{};
};
[[nodiscard]] Point protected_displacement(Point position, Point displacement, float protection);
} // namespace huelith::validation
