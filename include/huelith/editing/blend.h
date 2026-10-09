#pragma once
#include <array>
namespace huelith::editing {
enum class Blend {
    normal,
    darken,
    multiply,
    color_burn,
    linear_burn,
    lighten,
    screen,
    color_dodge,
    linear_dodge,
    overlay,
    soft_light,
    hard_light,
    vivid_light,
    linear_light,
    pin_light,
    hard_mix,
    difference,
    exclusion,
    subtract,
    divide,
    hue,
    saturation,
    color,
    luminosity
};
using Color = std::array<float, 3>;
[[nodiscard]] Color artistic_term(Color source, Color backdrop, Blend mode);
} // namespace huelith::editing
