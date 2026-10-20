#pragma once
#include "huelith/editing/ai_worker.h"

namespace huelith::editing {
enum class EnhanceMode { super_resolution, denoise, depth };
struct EnhanceOptions {
    EnhanceMode mode{EnhanceMode::super_resolution};
    int scale{2};
    double strength{1};
    bool directml{true};
    bool quality{};
    QString model;
};
QString enhancement_model(const EnhanceOptions& options);
AiResult enhance_image(const QImage& image, const EnhanceOptions& options,
                       const std::atomic_bool* cancelled = nullptr, const AiProgress& progress = {});
// Worker implementation; callers in the editor must use enhance_image.
AiResult infer_enhancement(const QImage& image, const EnhanceOptions& options,
                           const AiProgress& progress);
} // namespace huelith::editing
