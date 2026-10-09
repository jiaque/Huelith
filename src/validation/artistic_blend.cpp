#include "huelith/validation/artistic_blend.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace huelith::validation {
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
float encode(float x) {
    return x <= 0.0031308F ? 12.92F * x : 1.055F * std::pow(x, 1.0F / 2.4F) - 0.055F;
}
float decode(float x) {
    return x <= 0.04045F ? x / 12.92F : std::pow((x + 0.055F) / 1.055F, 2.4F);
}
} // namespace
Color artistic_term(Color source, Color backdrop, ArtisticMode mode) {
    if (static_cast<int>(mode) < 0 || static_cast<int>(mode) > 23)
        throw std::invalid_argument("unknown artistic mode");
    for (auto* color : {&source, &backdrop})
        for (auto& v : *color) {
            if (!std::isfinite(v))
                throw std::invalid_argument("nonfinite blend color");
            v = std::clamp(v, 0.0F, 1.0F);
        }
    if (mode == ArtisticMode::hue)
        return set_luminance(set_saturation(source, saturation(backdrop)), luminance(backdrop));
    if (mode == ArtisticMode::saturation)
        return set_luminance(set_saturation(backdrop, saturation(source)), luminance(backdrop));
    if (mode == ArtisticMode::color)
        return set_luminance(source, luminance(backdrop));
    if (mode == ArtisticMode::luminosity)
        return set_luminance(backdrop, luminance(source));
    Color result{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto s = source[i], b = backdrop[i];
        float v{};
        switch (mode) {
        case ArtisticMode::normal:
            v = s;
            break;
        case ArtisticMode::darken:
            v = std::min(s, b);
            break;
        case ArtisticMode::multiply:
            v = s * b;
            break;
        case ArtisticMode::color_burn:
            v = burn(s, b);
            break;
        case ArtisticMode::linear_burn:
            v = s + b - 1;
            break;
        case ArtisticMode::lighten:
            v = std::max(s, b);
            break;
        case ArtisticMode::screen:
            v = s + b - s * b;
            break;
        case ArtisticMode::color_dodge:
            v = dodge(s, b);
            break;
        case ArtisticMode::linear_dodge:
            v = s + b;
            break;
        case ArtisticMode::overlay:
            v = b <= 0.5F ? 2 * s * b : 1 - 2 * (1 - s) * (1 - b);
            break;
        case ArtisticMode::soft_light: {
            const auto d = b <= 0.25F ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
            v = s <= 0.5F ? b - (1 - 2 * s) * b * (1 - b) : b + (2 * s - 1) * (d - b);
            break;
        }
        case ArtisticMode::hard_light:
            v = s <= 0.5F ? 2 * s * b : 1 - 2 * (1 - s) * (1 - b);
            break;
        case ArtisticMode::vivid_light:
            v = vivid(s, b);
            break;
        case ArtisticMode::linear_light:
            v = b + 2 * s - 1;
            break;
        case ArtisticMode::pin_light:
            v = s <= 0.5F ? std::min(b, 2 * s) : std::max(b, 2 * s - 1);
            break;
        case ArtisticMode::hard_mix:
            v = vivid(s, b) < 0.5F ? 0.0F : 1.0F;
            break;
        case ArtisticMode::difference:
            v = std::abs(b - s);
            break;
        case ArtisticMode::exclusion:
            v = b + s - 2 * b * s;
            break;
        case ArtisticMode::subtract:
            v = b - s;
            break;
        case ArtisticMode::divide:
            v = s == 0 ? 1.0F : b / s;
            break;
        default:
            throw std::invalid_argument("unhandled artistic mode");
        }
        result[i] = std::clamp(v, 0.0F, 1.0F);
    }
    return result;
}
Pixel artistic_composite(Pixel source, Pixel backdrop, ArtisticMode mode) {
    const auto normal =
        composite(source, backdrop, BlendMode::normal); // validates finite values and alpha
    if (mode == ArtisticMode::normal)
        return normal;
    Color s{}, b{};
    for (std::size_t i = 0; i < 3; ++i) {
        s[i] = source[3] > 0 ? encode(std::clamp(source[i] / source[3], 0.0F, 1.0F)) : 0;
        b[i] = backdrop[3] > 0 ? encode(std::clamp(backdrop[i] / backdrop[3], 0.0F, 1.0F)) : 0;
    }
    const auto term = artistic_term(s, b, mode);
    Pixel result{};
    result[3] = normal[3];
    for (std::size_t i = 0; i < 3; ++i)
        result[i] = (source[3] > 0 ? source[i] : 0) * (1 - backdrop[3]) +
                    (backdrop[3] > 0 ? backdrop[i] : 0) * (1 - source[3]) +
                    source[3] * backdrop[3] * decode(term[i]);
    return result;
}
Pixel group_influence(Pixel before, Pixel after, float opacity, float mask) {
    if (!std::isfinite(opacity) || !std::isfinite(mask) || opacity < 0 || opacity > 1 || mask < 0 ||
        mask > 1)
        throw std::invalid_argument("invalid group coverage");
    static_cast<void>(composite(before, after, BlendMode::normal));
    Pixel result{};
    for (std::size_t i = 0; i < 4; ++i)
        result[i] = before[i] + (after[i] - before[i]) * opacity * mask;
    return result;
}
} // namespace huelith::validation
