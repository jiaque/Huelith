#pragma once
#include <QImage>
#include <atomic>
#include <functional>

namespace huelith::editing {
struct ContentFillOptions {
    int patch_radius{4};
    int candidates{192};
    double color_adaptation{.5};
    int rotation_adaptation{}; // None, low, medium, high, full.
    bool scale{};
    bool mirror{};
    unsigned seed{173};
};
// Non-neural exemplar synthesis. Donors come only from the original allowed region.
[[nodiscard]] QImage content_fill(const QImage& source, const QImage& selection,
                                  const QImage& allowed, const ContentFillOptions& options = {},
                                  const std::atomic_bool* cancelled = nullptr,
                                  const std::function<void(double)>& progress = {});
} // namespace huelith::editing
