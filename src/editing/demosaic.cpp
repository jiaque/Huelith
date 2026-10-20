#include "huelith/editing/demosaic.h"
#include "huelith/editing/project.h"
#include <algorithm>

namespace huelith::editing {
void demosaic_malvar(std::span<const float> packed, const std::array<QPoint, 4>& positions,
                     QImage& output, const std::atomic_bool& cancelled) {
    const int width = output.width(), height = output.height();
    if (width < 4 || height < 4 || width % 2 || height % 2 ||
        output.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        packed.size() != std::size_t(width) * height)
        throw EditError(QStringLiteral("Bayer 插值尺寸或数据无效"));
    std::array<int, 4> phase{-1, -1, -1, -1};
    for (int c = 0; c < 4; ++c) {
        const auto p = positions[c];
        if (p.x() < 0 || p.x() > 1 || p.y() < 0 || p.y() > 1 || phase[p.y() * 2 + p.x()] != -1)
            throw EditError(QStringLiteral("Bayer 通道位置无效"));
        phase[p.y() * 2 + p.x()] = c;
    }
    if (positions[0].x() == positions[3].x() || positions[0].y() == positions[3].y())
        throw EditError(QStringLiteral("Bayer 红蓝通道必须对角排列"));
    const auto plane = std::size_t(width / 2) * (height / 2);
    const auto sample = [&](int x, int y) {
        // Whole-sample reflection retains CFA parity at all four edges.
        if (x < 0)
            x = -x;
        if (y < 0)
            y = -y;
        if (x >= width)
            x = 2 * width - 2 - x;
        if (y >= height)
            y = 2 * height - 2 - y;
        const int c = phase[(y & 1) * 2 + (x & 1)];
        return packed[std::size_t(c) * plane + std::size_t(y / 2) * (width / 2) + x / 2];
    };
    // Malvar, He, Cutler (ICASSP 2004), Figure 2: gradient-corrected 5x5 FIRs.
    // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/Demosaicing_ICASSP04.pdf
    for (int y = 0; y < height; ++y) {
        if (cancelled.load())
            throw EditError(QStringLiteral("RAW 插值已取消"));
        auto* row = reinterpret_cast<float*>(output.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const float center = sample(x, y);
            const float horizontal = sample(x - 1, y) + sample(x + 1, y);
            const float vertical = sample(x, y - 1) + sample(x, y + 1);
            const float far_h = sample(x - 2, y) + sample(x + 2, y);
            const float far_v = sample(x, y - 2) + sample(x, y + 2);
            const float diagonal = sample(x - 1, y - 1) + sample(x + 1, y - 1) +
                                   sample(x - 1, y + 1) + sample(x + 1, y + 1);
            const int c = phase[(y & 1) * 2 + (x & 1)];
            if (c == 0 || c == 3) {
                row[x * 4 + (c == 0 ? 0 : 2)] = center;
                row[x * 4 + 1] = (4 * center + 2 * (horizontal + vertical) - far_h - far_v) / 8;
                row[x * 4 + (c == 0 ? 2 : 0)] =
                    (6 * center + 2 * diagonal - 1.5F * (far_h + far_v)) / 8;
            } else {
                row[x * 4 + 1] = center;
                const bool red_horizontal = positions[0].y() == (y & 1);
                row[x * 4 + (red_horizontal ? 0 : 2)] =
                    (5 * center + 4 * horizontal - diagonal - far_h + .5F * far_v) / 8;
                row[x * 4 + (red_horizontal ? 2 : 0)] =
                    (5 * center + 4 * vertical - diagonal - far_v + .5F * far_h) / 8;
            }
            row[x * 4 + 3] = 1;
        }
    }
}
} // namespace huelith::editing
