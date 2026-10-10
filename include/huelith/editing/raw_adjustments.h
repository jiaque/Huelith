#pragma once
#include "huelith/editing/tone.h"
namespace huelith::editing {
// Normalized source coordinates keep masks anchored when previews change resolution.
[[nodiscard]] QImage raw_local_mask(QSize size, const QJsonObject& local);
[[nodiscard]] QImage apply_raw_adjustments(const QImage& source, const QJsonObject& recipe,
                                           bool crop = true);
[[nodiscard]] QRect raw_crop_rect(QSize size, const QJsonObject& recipe);
} // namespace huelith::editing
