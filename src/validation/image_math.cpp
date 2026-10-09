#include "huelith/validation/image_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace huelith::validation {
Pixel composite(Pixel source, Pixel backdrop, BlendMode mode) {
    const auto valid = [](const Pixel& p) {
        return std::all_of(p.begin(), p.end(), [](float v) { return std::isfinite(v); }) &&
               p[3] >= 0 && p[3] <= 1;
    };
    if (!valid(source) || !valid(backdrop))
        throw std::invalid_argument("invalid premultiplied pixel");
    if (mode != BlendMode::normal && mode != BlendMode::multiply && mode != BlendMode::screen)
        throw std::invalid_argument("unknown blend mode");
    Pixel result{};
    const auto sa = source[3];
    const auto ba = backdrop[3];
    result[3] = sa + ba * (1.0F - sa);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto s = sa > 0 ? source[channel] : 0.0F;
        const auto b = ba > 0 ? backdrop[channel] : 0.0F;
        switch (mode) {
        case BlendMode::normal:
            result[channel] = s + b * (1.0F - sa);
            break;
        case BlendMode::multiply:
            result[channel] = s * (1.0F - ba) + b * (1.0F - sa) + s * b;
            break;
        case BlendMode::screen:
            result[channel] = s + b - s * b;
            break;
        }
    }
    return result;
}
std::vector<float> paint_range(std::span<const float> input, int width, int height, float x,
                               float y, float radius, float strength, bool subtract) {
    if (width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) != input.size() ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || radius <= 0 ||
        !std::isfinite(strength) || strength < 0 || strength > 1 ||
        !std::all_of(input.begin(), input.end(),
                     [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }))
        throw std::invalid_argument("invalid range brush input");
    std::vector<float> result(input.begin(), input.end());
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const auto dx = static_cast<float>(column) + 0.5F - x;
            const auto dy = static_cast<float>(row) + 0.5F - y;
            const auto distance = std::hypot(dx, dy);
            const auto coverage = strength * std::clamp(1.0F - distance / radius, 0.0F, 1.0F);
            auto& value = result[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(column)];
            value = subtract ? value * (1.0F - coverage) : value + (1.0F - value) * coverage;
        }
    }
    return result;
}
Point protected_displacement(Point position, Point displacement, float protection) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(displacement.x) || !std::isfinite(displacement.y) ||
        !std::isfinite(protection) || protection < 0 || protection > 1)
        throw std::invalid_argument("invalid protected displacement");
    return {position.x + displacement.x * (1.0F - protection),
            position.y + displacement.y * (1.0F - protection)};
}
} // namespace huelith::validation
