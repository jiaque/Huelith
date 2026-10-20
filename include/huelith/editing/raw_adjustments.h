#pragma once
#include "huelith/editing/tone.h"
namespace huelith::editing {
// Normalized source coordinates keep masks anchored when previews change resolution.
[[nodiscard]] QImage raw_local_mask(QSize size, const QJsonObject& local,
                                    const QJsonObject& geometry = {}, const QImage& reference = {});
[[nodiscard]] QImage apply_raw_adjustments(const QImage& source, const QJsonObject& recipe,
                                           bool crop = true,
                                           const std::atomic_bool* cancelled = nullptr,
                                           double pixel_scale = 1, int detail_preview = 0,
                                           int detail_target = -1);
[[nodiscard]] QRect raw_crop_rect(QSize size, const QJsonObject& recipe);
} // namespace huelith::editing
