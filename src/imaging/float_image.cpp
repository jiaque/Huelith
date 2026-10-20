#include "huelith/imaging/float_image.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace huelith {
bool supported_photo_size(std::int32_t width, std::int32_t height) noexcept {
    return width > 0 && height > 0 &&
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <= max_photo_pixels;
}

FloatImage::FloatImage(std::int32_t width, std::int32_t height, std::vector<LinearPixel> pixels)
    : width_(width), height_(height), pixels_(std::move(pixels)) {
    if (!supported_photo_size(width, height) ||
        pixels_.size() != static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height)) {
        throw std::invalid_argument("Invalid float image dimensions or storage length");
    }
    for (const auto& pixel : pixels_) {
        if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) ||
            !std::isfinite(pixel.blue) || !std::isfinite(pixel.alpha) ||
            pixel.alpha < 0.0F || pixel.alpha > 1.0F) {
            throw std::invalid_argument("Invalid linear pixel");
        }
    }
}

std::int32_t FloatImage::width() const noexcept { return width_; }
std::int32_t FloatImage::height() const noexcept { return height_; }
std::span<const LinearPixel> FloatImage::pixels() const noexcept { return pixels_; }

LinearPixel expose(LinearPixel pixel, float multiplier) noexcept {
    pixel.red *= multiplier;
    pixel.green *= multiplier;
    pixel.blue *= multiplier;
    return pixel;
}
} // namespace huelith
