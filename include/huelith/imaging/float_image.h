#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace huelith {
// Linear sRGB, premultiplied RGBA. RGB is intentionally not restricted to [0, 1].
struct LinearPixel {
    float red{};
    float green{};
    float blue{};
    float alpha{};
};

class FloatImage final {
public:
    FloatImage(std::int32_t width, std::int32_t height, std::vector<LinearPixel> pixels);
    [[nodiscard]] std::int32_t width() const noexcept;
    [[nodiscard]] std::int32_t height() const noexcept;
    [[nodiscard]] std::span<const LinearPixel> pixels() const noexcept;
private:
    std::int32_t width_{};
    std::int32_t height_{};
    std::vector<LinearPixel> pixels_;
};

inline constexpr std::uint64_t max_photo_pixels{512'000'000};
[[nodiscard]] bool supported_photo_size(std::int32_t width, std::int32_t height) noexcept;
[[nodiscard]] LinearPixel expose(LinearPixel pixel, float multiplier) noexcept;
} // namespace huelith
