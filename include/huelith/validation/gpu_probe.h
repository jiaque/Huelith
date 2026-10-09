#pragma once
#include "huelith/validation/image_math.h"
#include <QString>

namespace huelith::validation {
struct GpuResult {
    std::vector<Pixel> pixels;
    QString adapter;
    double milliseconds{};
    bool recovered_after_removal{};
};
// Creates a fresh D3D12 device per probe; errors propagate to the validation caller.
[[nodiscard]] GpuResult gpu_composite(std::span<const Pixel> source,
                                      std::span<const Pixel> backdrop, BlendMode mode,
                                      bool half_storage, bool simulate_device_loss = false);
} // namespace huelith::validation
