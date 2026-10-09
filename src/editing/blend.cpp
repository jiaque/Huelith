#include "huelith/editing/blend.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace huelith::editing {
namespace {
float luminance(Color c) {
    return 0.3F * c[0] + 0.59F * c[1] + 0.11F * c[2];
}
float saturation(Color c) {
    return *std::max_element(c.begin(), c.end()) - *std::min_element(c.begin(), c.end());
}
Color set_luminance(Color c, float value) {
    const auto delta = value - luminance(c);
    for (auto& v : c)
        v += delta;
    const auto l = luminance(c);
    const auto low = *std::min_element(c.begin(), c.end());
    const auto high = *std::max_element(c.begin(), c.end());
    if (low < 0)
        for (auto& v : c)
            v = l + (v - l) * l / (l - low);
    if (high > 1)
        for (auto& v : c)
            v = l + (v - l) * (1 - l) / (high - l);
    return c;
}
Color set_saturation(Color c, float value) {
    std::array<std::size_t, 3> indices{0, 1, 2};
    std::sort(indices.begin(), indices.end(), [&](auto a, auto b) { return c[a] < c[b]; });
    const auto low = indices[0], mid = indices[1], high = indices[2];
    if (c[high] > c[low]) {
        c[mid] = (c[mid] - c[low]) * value / (c[high] - c[low]);
        c[high] = value;
    } else {
        c[mid] = 0;
        c[high] = 0;
    }
    c[low] = 0;
    return c;
}
float burn(float s, float b) {
    if (b >= 1)
        return 1;
    return s <= 0 ? 0 : 1 - std::min(1.0F, (1 - b) / s);
}
float dodge(float s, float b) {
    if (b <= 0)
        return 0;
    return s >= 1 ? 1 : std::min(1.0F, b / (1 - s));
}
float vivid(float s, float b) {
    return s <= 0.5F ? burn(2 * s, b) : dodge(2 * s - 1, b);
}
} // namespace
Color artistic_term(Color source, Color backdrop, Blend mode) {
    if (static_cast<int>(mode) < 0 || static_cast<int>(mode) > 23)
        throw std::invalid_argument("unknown artistic mode");
    for (auto* color : {&source, &backdrop})
        for (auto& v : *color) {
            if (!std::isfinite(v))
                throw std::invalid_argument("nonfinite blend color");
            v = std::clamp(v, 0.0F, 1.0F);
        }
    if (mode == Blend::hue)
        return set_luminance(set_saturation(source, saturation(backdrop)), luminance(backdrop));
    if (mode == Blend::saturation)
        return set_luminance(set_saturation(backdrop, saturation(source)), luminance(backdrop));
    if (mode == Blend::color)
        return set_luminance(source, luminance(backdrop));
    if (mode == Blend::luminosity)
        return set_luminance(backdrop, luminance(source));
    Color result{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto s = source[i], b = backdrop[i];
        float v{};
        switch (mode) {
        case Blend::normal:
            v = s;
            break;
        case Blend::darken:
            v = std::min(s, b);
            break;
        case Blend::multiply:
            v = s * b;
            break;
        case Blend::color_burn:
            v = burn(s, b);
            break;
        case Blend::linear_burn:
            v = s + b - 1;
            break;
        case Blend::lighten:
            v = std::max(s, b);
            break;
        case Blend::screen:
            v = s + b - s * b;
            break;
        case Blend::color_dodge:
            v = dodge(s, b);
            break;
        case Blend::linear_dodge:
            v = s + b;
            break;
        case Blend::overlay:
            v = b <= 0.5F ? 2 * s * b : 1 - 2 * (1 - s) * (1 - b);
            break;
        case Blend::soft_light: {
            const auto d = b <= 0.25F ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
            v = s <= 0.5F ? b - (1 - 2 * s) * b * (1 - b) : b + (2 * s - 1) * (d - b);
            break;
        }
        case Blend::hard_light:
            v = s <= 0.5F ? 2 * s * b : 1 - 2 * (1 - s) * (1 - b);
            break;
        case Blend::vivid_light:
            v = vivid(s, b);
            break;
        case Blend::linear_light:
            v = b + 2 * s - 1;
            break;
        case Blend::pin_light:
            v = s <= 0.5F ? std::min(b, 2 * s) : std::max(b, 2 * s - 1);
            break;
        case Blend::hard_mix:
            v = vivid(s, b) < 0.5F ? 0.0F : 1.0F;
            break;
        case Blend::difference:
            v = std::abs(b - s);
            break;
        case Blend::exclusion:
            v = b + s - 2 * b * s;
            break;
        case Blend::subtract:
            v = b - s;
            break;
        case Blend::divide:
            v = s == 0 ? 1.0F : b / s;
            break;
        default:
            throw std::invalid_argument("unhandled artistic mode");
        }
        result[i] = std::clamp(v, 0.0F, 1.0F);
    }
    return result;
}
} // namespace huelith::editing
