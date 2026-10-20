#include "huelith/editing/content_fill.h"
#include "huelith/editing/project.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <vector>

namespace huelith::editing {
QImage content_fill(const QImage& source, const QImage& selection, const QImage& allowed,
                    const ContentFillOptions& options, const std::atomic_bool* cancelled,
                    const std::function<void(double)>& progress) {
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        selection.format() != QImage::Format_Grayscale8 || selection.size() != source.size() ||
        (!allowed.isNull() &&
         (allowed.size() != source.size() || allowed.format() != QImage::Format_Grayscale8)) ||
        options.patch_radius < 1 || options.patch_radius > 12 || options.candidates < 16 ||
        options.candidates > 2048 || !std::isfinite(options.color_adaptation) ||
        options.color_adaptation < 0 || options.color_adaptation > 1 ||
        options.rotation_adaptation < 0 || options.rotation_adaptation > 4)
        throw EditError("内容填充输入或参数无效");
    const auto check = [&] {
        if (cancelled && cancelled->load())
            throw EditError("内容填充已取消");
    };
    check();
    const int w = source.width(), h = source.height(), radius = options.patch_radius;
    const auto at = [w](int x, int y) { return std::size_t(y) * w + x; };
    std::vector<unsigned char> unknown(std::size_t(w) * h);
    std::vector<int> integral(std::size_t(w + 1) * (h + 1));
    int remaining = 0;
    for (int y = 0; y < h; ++y) {
        check();
        int total = 0;
        const auto* pixels = reinterpret_cast<const float*>(source.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            unknown[at(x, y)] = selection.constScanLine(y)[x] > 0;
            remaining += unknown[at(x, y)];
            total += unknown[at(x, y)] || (!allowed.isNull() && allowed.constScanLine(y)[x] == 0) ||
                     pixels[x * 4 + 3] < .99F;
            integral[std::size_t(y + 1) * (w + 1) + x + 1] =
                total + integral[std::size_t(y) * (w + 1) + x + 1];
        }
    }
    if (!remaining)
        return source;
    const int original_count = remaining;
    struct Offset {
        int x, y;
        float fx, fy;
    };
    struct Patch {
        std::vector<Offset> samples;
        int left{}, top{}, right{}, bottom{};
    };
    const int side = 2 * radius + 1;
    std::vector<double> angles{0};
    if (options.rotation_adaptation == 1)
        angles = {0, -15, 15};
    if (options.rotation_adaptation == 2)
        angles = {0, -15, 15, -30, 30, -45, 45};
    if (options.rotation_adaptation == 3)
        angles = {0, -30, 30, -60, 60, -90, 90};
    if (options.rotation_adaptation == 4)
        angles = {0, -30, 30, -60, 60, -90, 90, -120, 120, -150, 150, 180};
    const std::vector<double> scales =
        options.scale ? std::vector<double>{1, .75, 1.25, .5, 2} : std::vector<double>{1};
    std::vector<Patch> patches;
    for (int mirror = 0; mirror < (options.mirror ? 2 : 1); ++mirror)
        for (const double scale : scales)
            for (const double angle : angles) {
                const double cosine = std::cos(angle * std::numbers::pi / 180) * scale;
                const double sine = std::sin(angle * std::numbers::pi / 180) * scale;
                Patch patch;
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx) {
                        double x = cosine * dx - sine * dy, y = sine * dx + cosine * dy;
                        if (mirror)
                            x = -x;
                        if (std::abs(x - std::round(x)) < 1e-9)
                            x = std::round(x);
                        if (std::abs(y - std::round(y)) < 1e-9)
                            y = std::round(y);
                        const int ix = int(std::floor(x)), iy = int(std::floor(y));
                        patch.samples.push_back({ix, iy, float(x - ix), float(y - iy)});
                        patch.left = std::min(patch.left, ix);
                        patch.top = std::min(patch.top, iy);
                        patch.right = std::max(patch.right, int(std::ceil(x)));
                        patch.bottom = std::max(patch.bottom, int(std::ceil(y)));
                    }
                patches.push_back(std::move(patch));
            }
    const auto unavailable = [&](int x, int y, int left, int top, int right, int bottom) {
        const int l = x + left, t = y + top, r = x + right + 1, b = y + bottom + 1;
        if (l < 0 || t < 0 || r > w || b > h)
            return true;
        return integral[std::size_t(b) * (w + 1) + r] - integral[std::size_t(t) * (w + 1) + r] -
                   integral[std::size_t(b) * (w + 1) + l] +
                   integral[std::size_t(t) * (w + 1) + l] !=
               0;
    };
    const auto read_sample = [&](QPoint point, const Offset& offset) {
        const int x = point.x() + offset.x, y = point.y() + offset.y;
        const int nx = x + (offset.fx > 0), ny = y + (offset.fy > 0);
        const auto* a = reinterpret_cast<const float*>(source.constScanLine(y)) + x * 4;
        const auto* b = reinterpret_cast<const float*>(source.constScanLine(y)) + nx * 4;
        const auto* c = reinterpret_cast<const float*>(source.constScanLine(ny)) + x * 4;
        const auto* d = reinterpret_cast<const float*>(source.constScanLine(ny)) + nx * 4;
        std::array<float, 4> result;
        for (int channel = 0; channel < 4; ++channel)
            result[channel] = std::lerp(std::lerp(a[channel], b[channel], offset.fx),
                                        std::lerp(c[channel], d[channel], offset.fx), offset.fy);
        return result;
    };
    std::vector<QPoint> donors;
    const int minimum_radius = options.scale ? (radius + 1) / 2 : radius;
    for (int y = minimum_radius; y < h - minimum_radius; ++y) {
        check();
        for (int x = minimum_radius; x < w - minimum_radius; ++x)
            if (!unavailable(x, y, -minimum_radius, -minimum_radius, minimum_radius,
                             minimum_radius))
                donors.emplace_back(x, y);
    }
    if (donors.empty())
        throw EditError("取样范围不足：请增加允许取样的区域，或减小纹理块尺寸");
    QImage output = source;
    output.detach();
    struct Front {
        int known, x, y;
        bool operator<(const Front& other) const {
            return known < other.known;
        }
    };
    std::priority_queue<Front> front;
    std::vector<unsigned char> queued(unknown.size());
    const auto enqueue = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h || !unknown[at(x, y)] || queued[at(x, y)])
            return;
        int known = 0;
        for (int yy = std::max(0, y - 1); yy <= std::min(h - 1, y + 1); ++yy)
            for (int xx = std::max(0, x - 1); xx <= std::min(w - 1, x + 1); ++xx)
                known += !unknown[at(xx, yy)];
        if (known) {
            front.push({known, x, y});
            queued[at(x, y)] = 1;
        }
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            enqueue(x, y);
    if (front.empty())
        throw EditError("填充区域没有可接续的图像边界");
    unsigned random = options.seed ? options.seed : 1;
    const auto next = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    QPoint previous_offset;
    std::size_t previous_patch{};
    int iterations = 0;
    while (remaining && !front.empty()) {
        check();
        const auto current = front.top();
        front.pop();
        queued[at(current.x, current.y)] = 0;
        if (!unknown[at(current.x, current.y)])
            continue;
        struct Known {
            int dx, dy;
            std::array<double, 3> color;
        };
        std::vector<Known> known;
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx) {
                const int x = current.x + dx, y = current.y + dy;
                if (x < 0 || y < 0 || x >= w || y >= h || unknown[at(x, y)])
                    continue;
                const auto* p = reinterpret_cast<const float*>(output.constScanLine(y)) + x * 4;
                if (p[3] < .001F)
                    continue;
                known.push_back({dx, dy, {p[0] / p[3], p[1] / p[3], p[2] / p[3]}});
            }
        if (known.empty())
            continue;
        double best = std::numeric_limits<double>::infinity();
        QPoint chosen;
        std::size_t chosen_patch{};
        std::array<double, 3> correction{};
        const auto evaluate = [&](QPoint candidate, std::size_t patch_index) {
            const auto& patch = patches[patch_index];
            if (unavailable(candidate.x(), candidate.y(), patch.left, patch.top, patch.right,
                            patch.bottom))
                return;
            std::array<double, 3> mean{};
            double squared = 0;
            for (const auto& sample : known) {
                const auto p = read_sample(
                    candidate,
                    patch.samples[std::size_t(sample.dy + radius) * side + sample.dx + radius]);
                for (int c = 0; c < 3; ++c) {
                    const double delta = sample.color[c] - p[c] / p[3];
                    mean[c] += delta;
                    squared += delta * delta;
                }
            }
            double variance = squared / known.size();
            for (auto& value : mean) {
                value /= known.size();
                variance -= value * value *
                            (2 * options.color_adaptation -
                             options.color_adaptation * options.color_adaptation);
            }
            if (variance < best) {
                best = variance;
                chosen = candidate;
                chosen_patch = patch_index;
                for (int c = 0; c < 3; ++c)
                    correction[c] = mean[c] * options.color_adaptation;
            }
        };
        evaluate(QPoint(current.x, current.y) + previous_offset, previous_patch);
        // Exhaust tiny source sets; otherwise distribute the fixed search budget
        // across transformations. A transformed donor can never cross the allowed mask.
        if (patches.size() > 1 &&
            donors.size() * patches.size() <= std::size_t(options.candidates)) {
            for (std::size_t variant = 0; variant < patches.size(); ++variant)
                for (const auto donor : donors)
                    evaluate(donor, variant);
        } else {
            const int count = std::max(options.candidates, int(patches.size()) * 4);
            for (int n = 0; n < count; ++n) {
                if ((n & 31) == 0)
                    check();
                evaluate(donors[next() % donors.size()], std::size_t(n) % patches.size());
            }
        }
        if (!std::isfinite(best))
            throw EditError("未找到可用的纹理样本");
        previous_offset = chosen - QPoint(current.x, current.y);
        previous_patch = chosen_patch;
        std::vector<QPoint> filled;
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx) {
                const int x = current.x + dx, y = current.y + dy;
                if (x < 0 || y < 0 || x >= w || y >= h || !unknown[at(x, y)])
                    continue;
                const auto p = read_sample(
                    chosen,
                    patches[chosen_patch].samples[std::size_t(dy + radius) * side + dx + radius]);
                auto* q = reinterpret_cast<float*>(output.scanLine(y)) + x * 4;
                for (int c = 0; c < 3; ++c)
                    q[c] = float(p[c] + correction[c] * p[3]);
                q[3] = p[3];
                unknown[at(x, y)] = 0;
                --remaining;
                filled.emplace_back(x, y);
            }
        for (const auto p : filled)
            for (int yy = -1; yy <= 1; ++yy)
                for (int xx = -1; xx <= 1; ++xx)
                    enqueue(p.x() + xx, p.y() + yy);
        if (++iterations % 16 == 0 && progress)
            progress(1 - double(remaining) / original_count);
    }
    if (remaining)
        throw EditError("部分区域缺少有效边界，无法完成内容填充");
    check();
    if (progress)
        progress(1);
    return output;
}
} // namespace huelith::editing
