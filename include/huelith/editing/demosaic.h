#pragma once
#include <QImage>
#include <QPoint>
#include <array>
#include <atomic>
#include <span>

namespace huelith::editing {
// Packed planes are R, G1, G2, B. Positions specify their phase in the Bayer 2x2 cell.
// Output is unbalanced linear camera RGB; measured samples and FP32 headroom are retained.
void demosaic_malvar(std::span<const float> packed, const std::array<QPoint, 4>& positions,
                     QImage& output, const std::atomic_bool& cancelled);
} // namespace huelith::editing
