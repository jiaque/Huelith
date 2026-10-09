#pragma once
#include "huelith/validation/image_math.h"

namespace huelith::validation {
enum class ArtisticMode {
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
// Artistic blend term is explicitly bounded encoded sRGB; normal remains linear.
[[nodiscard]] Color artistic_term(Color source, Color backdrop, ArtisticMode mode);
[[nodiscard]] Pixel artistic_composite(Pixel linear_srgb_source, Pixel linear_srgb_backdrop,
                                       ArtisticMode mode);
[[nodiscard]] Pixel group_influence(Pixel before, Pixel after, float opacity, float mask);
} // namespace huelith::validation
