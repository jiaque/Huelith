#include "huelith/editing/image_storage.h"
#include "huelith/editing/selection_refine.h"
#include "huelith/editing/project.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace huelith::editing {
namespace {
using Plane = std::vector<float>;
void check(const std::atomic_bool* cancelled) {
    if (cancelled && cancelled->load())
        throw EditError(QStringLiteral("选区细化已取消"));
}
Plane mean(const Plane& input, int w, int h, int radius, const std::atomic_bool* cancelled) {
    if (radius == 0)
        return input;
    Plane temporary(input.size()), output(input.size());
    for (int y = 0; y < h; ++y) {
        check(cancelled);
        const auto row = std::size_t(y) * w;
        double sum{};
        for (int k = -radius; k <= radius; ++k)
            sum += input[row + std::clamp(k, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            temporary[row + x] = float(sum / (2 * radius + 1));
            sum += input[row + std::clamp(x + radius + 1, 0, w - 1)] -
                   input[row + std::clamp(x - radius, 0, w - 1)];
        }
    }
    for (int x = 0; x < w; ++x) {
        check(cancelled);
        double sum{};
        for (int k = -radius; k <= radius; ++k)
            sum += temporary[std::size_t(std::clamp(k, 0, h - 1)) * w + x];
        for (int y = 0; y < h; ++y) {
            output[std::size_t(y) * w + x] = float(sum / (2 * radius + 1));
            sum += temporary[std::size_t(std::clamp(y + radius + 1, 0, h - 1)) * w + x] -
                   temporary[std::size_t(std::clamp(y - radius, 0, h - 1)) * w + x];
        }
    }
    return output;
}
Plane product(const Plane& a, const Plane& b) {
    Plane result(a.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        result[i] = a[i] * b[i];
    return result;
}
Plane extremum(Plane input, int w, int h, int radius, bool maximum,
               const std::atomic_bool* cancelled) {
    if (radius == 0)
        return input;
    for (int axis = 0; axis < 2; ++axis) {
        Plane output(input.size());
        const int length = axis == 0 ? w : h, lines = axis == 0 ? h : w;
        for (int line = 0; line < lines; ++line) {
            check(cancelled);
            const auto at = [&](int p) {
                return axis == 0 ? std::size_t(line) * w + p : std::size_t(p) * w + line;
            };
            std::deque<std::pair<int, float>> queue;
            for (int p = -radius; p < length + radius; ++p) {
                const float value = input[at(std::clamp(p, 0, length - 1))];
                while (!queue.empty() &&
                       (maximum ? queue.back().second <= value : queue.back().second >= value))
                    queue.pop_back();
                queue.emplace_back(p, value);
                while (queue.front().first < p - radius * 2)
                    queue.pop_front();
                if (p >= radius)
                    output[at(p - radius)] = queue.front().second;
            }
        }
        input = std::move(output);
    }
    return input;
}
Plane guided(const std::array<Plane, 3>& guide, const Plane& mask, int w, int h, int radius,
             const std::atomic_bool* cancelled) {
    // RGB guided filter (He et al., ECCV 2010), regularized local linear model.
    std::array<Plane, 3> means;
    for (int c = 0; c < 3; ++c)
        means[c] = mean(guide[c], w, h, radius, cancelled);
    const auto mp = mean(mask, w, h, radius, cancelled);
    std::array<Plane, 6> covariance;
    constexpr std::array<std::array<int, 2>, 6> pairs{
        {{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 2}}};
    for (int c = 0; c < 6; ++c) {
        const auto [a, b] = pairs[c];
        covariance[c] = mean(product(guide[a], guide[b]), w, h, radius, cancelled);
        for (std::size_t i = 0; i < mask.size(); ++i)
            covariance[c][i] -= means[a][i] * means[b][i];
    }
    std::array<Plane, 4> coefficients;
    for (int c = 0; c < 3; ++c) {
        coefficients[c] = mean(product(guide[c], mask), w, h, radius, cancelled);
        for (std::size_t i = 0; i < mask.size(); ++i)
            coefficients[c][i] -= means[c][i] * mp[i];
    }
    coefficients[3].resize(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (i % std::size_t(w) == 0)
            check(cancelled);
        constexpr double epsilon = .001;
        const double a = covariance[0][i] + epsilon, b = covariance[1][i], c = covariance[2][i];
        const double d = covariance[3][i] + epsilon, e = covariance[4][i],
                     f = covariance[5][i] + epsilon;
        const double aa = d * f - e * e, ab = c * e - b * f, ac = b * e - c * d;
        const double dd = a * f - c * c, de = b * c - a * e, ff = a * d - b * b;
        const double determinant = std::max(1e-12, a * aa + b * ab + c * ac);
        const double x = coefficients[0][i], y = coefficients[1][i], z = coefficients[2][i];
        coefficients[0][i] = float((aa * x + ab * y + ac * z) / determinant);
        coefficients[1][i] = float((ab * x + dd * y + de * z) / determinant);
        coefficients[2][i] = float((ac * x + de * y + ff * z) / determinant);
        coefficients[3][i] = mp[i];
        for (int channel = 0; channel < 3; ++channel)
            coefficients[3][i] -= coefficients[channel][i] * means[channel][i];
    }
    for (auto& coefficient : coefficients)
        coefficient = mean(coefficient, w, h, radius, cancelled);
    Plane output(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        double value = coefficients[3][i];
        for (int c = 0; c < 3; ++c)
            value += coefficients[c][i] * guide[c][i];
        output[i] = float(std::clamp(value, 0., 1.));
    }
    return output;
}
Plane coverage(const QImage& image, QRect area) {
    Plane result(std::size_t(area.width()) * area.height());
    for (int y = 0; y < area.height(); ++y) {
        const auto* row = image.constScanLine(area.y() + y) + area.x();
        for (int x = 0; x < area.width(); ++x)
            result[std::size_t(y) * area.width() + x] = row[x] / 255.F;
    }
    return result;
}
QImage refine_edges(const QImage& rgb, const QImage& original, int radius, bool smart,
                    const QImage& region, const std::atomic_bool* cancelled) {
    QImage result(original.size(), QImage::Format_Grayscale8);
    constexpr int tile = 512;
    for (int top = 0; top < original.height(); top += tile)
        for (int left = 0; left < original.width(); left += tile) {
            check(cancelled);
            const QRect core = QRect(left, top, tile, tile).intersected(original.rect());
            const QRect expanded = core.adjusted(-2 * radius, -2 * radius, 2 * radius, 2 * radius)
                                       .intersected(original.rect());
            const int w = expanded.width(), h = expanded.height();
            auto mask = coverage(original, expanded);
            std::array<Plane, 3> guide;
            for (auto& plane : guide)
                plane.resize(mask.size());
            for (int y = 0; y < h; ++y) {
                const auto* row = rgb.constScanLine(expanded.top() + y) + expanded.left() * 4;
                for (int x = 0; x < w; ++x)
                    for (int c = 0; c < 3; ++c)
                        guide[c][std::size_t(y) * w + x] = row[x * 4 + c] / 255.F;
            }
            auto refined = guided(guide, mask, w, h, radius, cancelled);
            if (smart && radius > 2) {
                const auto sharp = guided(guide, mask, w, h, std::max(1, radius / 4), cancelled);
                // High-contrast edges use a narrower band; low contrast texture
                // retains the broader refinement. The setting changes actual pixels.
                const auto average = mean(mask, w, h, 1, cancelled);
                for (std::size_t i = 0; i < mask.size(); ++i) {
                    const float edge = std::clamp(std::abs(mask[i] - average[i]) * 5.F, 0.F, 1.F);
                    refined[i] = std::lerp(refined[i], sharp[i], edge);
                }
            }
            for (int y = core.top(); y <= core.bottom(); ++y) {
                auto* row = result.scanLine(y);
                const auto* old = original.constScanLine(y);
                const auto* area = region.isNull() ? nullptr : region.constScanLine(y);
                for (int x = core.left(); x <= core.right(); ++x) {
                    const auto index = std::size_t(y - expanded.top()) * w + x - expanded.left();
                    const float weight = area ? area[x] / 255.F : 1.F;
                    row[x] = uchar(std::clamp(
                        qRound(std::lerp(old[x] / 255.F, refined[index], weight) * 255.F), 0, 255));
                }
            }
        }
    return result;
}
} // namespace

QImage selection_edge_band(const QImage& mask, int radius, const std::atomic_bool* cancelled) {
    if (mask.isNull() || mask.format() != QImage::Format_Grayscale8 || radius < 0 || radius > 500)
        throw EditError(QStringLiteral("边缘区域参数无效"));
    const auto values = coverage(mask, mask.rect());
    const auto low = extremum(values, mask.width(), mask.height(), radius, false, cancelled);
    const auto high = extremum(values, mask.width(), mask.height(), radius, true, cancelled);
    QImage output(mask.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < mask.height(); ++y) {
        check(cancelled);
        auto* row = output.scanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            const auto i = std::size_t(y) * mask.width() + x;
            row[x] = high[i] - low[i] > 1.F / 255.F ? 255 : 0;
        }
    }
    return output;
}

RefinedSelection refine_selection(const QImage& image, const QImage& mask,
                                  const SelectionRefineSettings& s, const QImage& refine_area,
                                  const std::atomic_bool* cancelled, double pixel_scale) {
    check(cancelled);
    if (image.isNull() || mask.size() != image.size() ||
        mask.format() != QImage::Format_Grayscale8 ||
        qint64(image.width()) * image.height() > huelith::editing::maximum_image_pixels ||
        (!refine_area.isNull() &&
         (refine_area.size() != mask.size() || refine_area.format() != QImage::Format_Grayscale8)))
        throw EditError(QStringLiteral("选区细化的图像或蒙版无效"));
    const auto in_range = [](double value, double low, double high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    if (!in_range(s.radius, 0, 250) || !in_range(s.smooth, 0, 100) ||
        !in_range(s.feather, 0, 1000) || !in_range(s.contrast, 0, 1) || !in_range(s.shift, -1, 1) ||
        !in_range(s.decontaminate, 0, 1) || !in_range(pixel_scale, .0001, 1))
        throw EditError(QStringLiteral("选区细化参数超出范围"));
    auto result = mask;
    if (s.radius > 0 || !refine_area.isNull()) {
        auto rgb = image.format() == QImage::Format_RGBA32FPx4_Premultiplied ? display_image(image)
                                                                             : image;
        rgb = rgb.convertToFormat(QImage::Format_RGBA8888);
        const int radius = std::max(1, qRound((s.radius > 0 ? s.radius : 10) * pixel_scale));
        result = refine_edges(rgb, result, radius, s.smart_radius, refine_area, cancelled);
    }
    const int w = image.width(), h = image.height();
    if (s.smooth > 0 || s.feather > 0 || s.contrast > 0 || s.shift != 0) {
        auto values = coverage(result, result.rect());
        if (s.smooth > 0) {
            values =
                mean(values, w, h, std::max(1, qRound(s.smooth * .25 * pixel_scale)), cancelled);
            for (auto& value : values) {
                const float t = std::clamp((value - .25F) * 2.F, 0.F, 1.F);
                value = t * t * (3 - 2 * t);
            }
        }
        if (s.feather > 0) {
            // Three box filters approximate a Gaussian without O(radius) work
            // per pixel. Preserve fractional feather values by interpolation.
            const double width = std::sqrt(4 * std::pow(s.feather * pixel_scale, 2) + 1);
            const int radius = int((width - 1) * .5);
            const float fraction = float((width - 1) * .5 - radius);
            for (int pass = 0; pass < 3; ++pass) {
                auto low = mean(values, w, h, radius, cancelled);
                if (fraction > 0) {
                    const auto high = mean(values, w, h, radius + 1, cancelled);
                    for (std::size_t i = 0; i < values.size(); ++i)
                        low[i] = std::lerp(low[i], high[i], fraction);
                }
                values = std::move(low);
            }
        }
        const double gain = 1. / std::max(.01, 1 - s.contrast);
        for (auto& value : values)
            value = float(std::clamp((value - .5) * gain + .5, 0., 1.));
        if (s.shift != 0) {
            const double distance =
                std::abs(s.shift) * std::max(1., (s.radius + s.feather * 2) * pixel_scale);
            const int radius = int(distance);
            auto low = extremum(values, w, h, radius, s.shift > 0, cancelled);
            if (distance > radius) {
                const auto high = extremum(values, w, h, radius + 1, s.shift > 0, cancelled);
                for (std::size_t i = 0; i < values.size(); ++i)
                    low[i] = std::lerp(low[i], high[i], float(distance - radius));
            }
            values = std::move(low);
        }
        for (int y = 0; y < h; ++y) {
            check(cancelled);
            auto* row = result.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const double value = values[std::size_t(y) * w + x];
                row[x] = uchar(qRound(value * 255));
            }
        }
    }
    if (s.invert)
        result.invertPixels();
    QImage recolored;
    if (s.decontaminate > 0) {
        const auto linear =
            image.format() == QImage::Format_RGBA32FPx4_Premultiplied ? image : to_working(image);
        recolored = linear;
        // Process independent tiles with halo so memory does not scale with the
        // number of full-resolution RGB statistic planes.
        const int radius = std::max(2, qRound(std::max(10., s.radius * 2) * pixel_scale));
        for (int top = 0; top < h; top += 512)
            for (int left = 0; left < w; left += 512) {
                check(cancelled);
                const QRect core = QRect(left, top, 512, 512).intersected(result.rect());
                const QRect region =
                    core.adjusted(-radius, -radius, radius, radius).intersected(result.rect());
                const int rw = region.width(), rh = region.height();
                Plane weight(std::size_t(rw) * rh);
                std::array<Plane, 3> sums;
                for (auto& plane : sums)
                    plane.resize(weight.size());
                for (int y = 0; y < rh; ++y) {
                    const auto* row =
                        reinterpret_cast<const float*>(linear.constScanLine(region.y() + y));
                    const auto* m = result.constScanLine(region.y() + y);
                    for (int x = 0; x < rw; ++x) {
                        const auto* pixel = row + (region.x() + x) * 4;
                        const auto index = std::size_t(y) * rw + x;
                        weight[index] = m[region.x() + x] >= 250 && pixel[3] > .001F ? 1.F : 0.F;
                        for (int c = 0; c < 3; ++c)
                            sums[c][index] =
                                weight[index] * (pixel[3] > .001F ? pixel[c] / pixel[3] : 0.F);
                    }
                }
                weight = mean(weight, rw, rh, radius, cancelled);
                for (auto& plane : sums)
                    plane = mean(plane, rw, rh, radius, cancelled);
                for (int y = core.top(); y <= core.bottom(); ++y) {
                    auto* row = reinterpret_cast<float*>(recolored.scanLine(y));
                    const auto* m = result.constScanLine(y);
                    for (int x = core.left(); x <= core.right(); ++x) {
                        const auto i = std::size_t(y - region.top()) * rw + x - region.left();
                        if (weight[i] < 1e-6F || m[x] == 0 || m[x] >= 250)
                            continue;
                        const float strength = float(s.decontaminate * (1 - m[x] / 255.));
                        for (int c = 0; c < 3; ++c)
                            row[x * 4 + c] = std::lerp(
                                row[x * 4 + c], sums[c][i] / weight[i] * row[x * 4 + 3], strength);
                    }
                }
            }
    }
    return {std::move(result), std::move(recolored)};
}
} // namespace huelith::editing
