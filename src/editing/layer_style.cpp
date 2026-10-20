#include "huelith/editing/layer_style.h"
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace huelith::editing {
LayerStyles::LayerStyles() {
    entries[0].opacity = 1;
    entries[1].blend = entries[2].blend = Blend::multiply;
    entries[3].color = entries[4].color = QColor("#ffffbe");
    entries[3].blend = entries[4].blend = Blend::screen;
    entries[5].opacity = 1;
}
bool LayerStyles::active() const {
    return enabled &&
           std::any_of(entries.begin(), entries.end(), [](const auto& s) { return s.enabled; });
}
void validate_styles(const LayerStyles& value) {
    if (!std::isfinite(value.global_angle) || std::abs(value.global_angle) > 360)
        throw EditError(QStringLiteral("样式全局光角度无效"));
    for (const auto& s : value.entries)
        if (!s.color.isValid() || !std::isfinite(s.opacity) || s.opacity < 0 || s.opacity > 1 ||
            int(s.blend) < 0 || int(s.blend) > 23 || !std::isfinite(s.size) || s.size < 0 ||
            s.size > 250 || !std::isfinite(s.angle) || std::abs(s.angle) > 360 ||
            !std::isfinite(s.distance) || s.distance < 0 || s.distance > 1000 ||
            !std::isfinite(s.spread) || s.spread < 0 || s.spread > 1 || s.position < 0 ||
            s.position > 2)
            throw EditError(QStringLiteral("图层样式参数无效"));
}
namespace {
// Exact squared Euclidean distance transform (lower envelope of parabolas), O(width*height).
std::vector<float> distances(const std::vector<float>& alpha, int width, int height, bool inside) {
    const int maximum = std::max(width, height);
    std::vector<float> result(alpha.size()), input(maximum), output(maximum);
    std::vector<int> sites(maximum);
    std::vector<double> limits(std::size_t(maximum) + 1);
    auto line = [&](int count) {
        int k = 0;
        sites[0] = 0;
        limits[0] = -1e30;
        limits[1] = 1e30;
        for (int q = 1; q < count; ++q) {
            double crossing{};
            do {
                const int site = sites[k];
                crossing =
                    (double(input[q]) + double(q) * q - double(input[site]) - double(site) * site) /
                    (2. * (q - site));
                if (crossing > limits[k])
                    break;
                --k;
            } while (k >= 0);
            ++k;
            sites[k] = q;
            limits[k] = crossing;
            limits[k + 1] = 1e30;
        }
        k = 0;
        for (int q = 0; q < count; ++q) {
            while (limits[k + 1] < q)
                ++k;
            const double delta = q - sites[k];
            output[q] = float(delta * delta + input[sites[k]]);
        }
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            input[x] = (alpha[std::size_t(y) * width + x] >= .5F) == inside ? 0.F : 1e12F;
        line(width);
        for (int x = 0; x < width; ++x)
            result[std::size_t(y) * width + x] = output[x];
    }
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y)
            input[y] = result[std::size_t(y) * width + x];
        line(height);
        for (int y = 0; y < height; ++y) {
            const float distance = std::sqrt(output[y]);
            result[std::size_t(y) * width + x] = inside ? distance :
                std::min(distance, float(std::min({x + 1, y + 1, width - x, height - y})));
        }
    }
    return result;
}
void blur(std::vector<float>& data, int width, int height, double size, float outside) {
    const int radius = std::clamp(qRound(size / 2), 0, 250);
    if (!radius)
        return;
    std::vector<float> temp(data.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < height; ++y) {
            double sum = (radius + std::max(0, radius - width + 1)) * double(outside);
            for (int x = 0; x <= std::min(radius, width - 1); ++x)
                sum += data[std::size_t(y) * width + x];
            for (int x = 0; x < width; ++x) {
                temp[std::size_t(y) * width + x] = float(sum / (2 * radius + 1));
                if (x - radius >= 0)
                    sum -= data[std::size_t(y) * width + x - radius];
                else sum -= outside;
                if (x + radius + 1 < width)
                    sum += data[std::size_t(y) * width + x + radius + 1];
                else sum += outside;
            }
        }
        for (int x = 0; x < width; ++x) {
            double sum = (radius + std::max(0, radius - height + 1)) * double(outside);
            for (int y = 0; y <= std::min(radius, height - 1); ++y)
                sum += temp[std::size_t(y) * width + x];
            for (int y = 0; y < height; ++y) {
                data[std::size_t(y) * width + x] = float(sum / (2 * radius + 1));
                if (y - radius >= 0)
                    sum -= temp[std::size_t(y - radius) * width + x];
                else sum -= outside;
                if (y + radius + 1 < height)
                    sum += temp[std::size_t(y + radius + 1) * width + x];
                else sum += outside;
            }
        }
    }
}
} // namespace
std::vector<float> style_coverage(const QImage& source, StyleKind kind, const LayerStyle& style,
                                  double global_angle, double pixel_scale) {
    LayerStyles settings;
    settings.global_angle = global_angle;
    settings.entries[0] = style;
    validate_styles(settings);
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied || !std::isfinite(pixel_scale) ||
        pixel_scale <= 0 || pixel_scale > 64)
        throw EditError(QStringLiteral("图层样式输入无效"));
    const int width = source.width(), height = source.height();
    std::vector<float> alpha(std::size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const float*>(source.constScanLine(y));
        for (int x = 0; x < width; ++x)
            alpha[std::size_t(y) * width + x] = std::clamp(row[x * 4 + 3], 0.F, 1.F);
    }
    if (kind == StyleKind::color_overlay)
        return alpha;
    auto result = alpha;
    const double size = style.size * pixel_scale;
    if (kind == StyleKind::stroke) {
        const auto outer = distances(alpha, width, height, true);
        const auto inner = distances(alpha, width, height, false);
        for (std::size_t i = 0; i < alpha.size(); ++i) {
            const float outside =
                (1 - alpha[i]) *
                float(std::clamp(size * (style.position == 2 ? .5 : 1) + .5 - outer[i], 0., 1.));
            const float inside =
                alpha[i] *
                float(std::clamp(size * (style.position == 2 ? .5 : 1) + .5 - inner[i], 0., 1.));
            result[i] = style.position == 0   ? outside
                        : style.position == 1 ? inside
                                              : outside + inside;
        }
        return result;
    }
    const bool interior = kind == StyleKind::inner_shadow || kind == StyleKind::inner_glow;
    if (interior)
        for (auto& value : result)
            value = 1 - value;
    if (style.spread > 0) {
        const auto distance = distances(result, width, height, true);
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = std::max(result[i],
                                 float(std::clamp(size * style.spread + .5 - distance[i], 0., 1.)));
    }
    blur(result, width, height, size * (1 - style.spread), interior ? 1.F : 0.F);
    const auto blurred = result;
    const bool shadow = kind == StyleKind::drop_shadow || kind == StyleKind::inner_shadow;
    const double angle = (style.global_light ? global_angle : style.angle) * std::numbers::pi / 180;
    const double dx = shadow ? -std::cos(angle) * style.distance * pixel_scale : 0;
    const double dy = shadow ? std::sin(angle) * style.distance * pixel_scale : 0;
    auto sample = [&](int x, int y) {
        return x < 0 || y < 0 || x >= width || y >= height ? (interior ? 1.F : 0.F)
                                                           : blurred[std::size_t(y) * width + x];
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double sx = x - dx, sy = y - dy;
            const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
            const float fx = float(sx - x0), fy = float(sy - y0);
            float value = std::lerp(std::lerp(sample(x0, y0), sample(x0 + 1, y0), fx),
                                    std::lerp(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), fx), fy);
            const auto i = std::size_t(y) * width + x;
            if (kind == StyleKind::inner_glow && style.position == 1)
                value = 1 - value;
            result[i] = std::clamp(value, 0.F, 1.F) * (interior ? alpha[i] : 1);
        }
    return result;
}
QJsonObject styles_json(const LayerStyles& value) {
    validate_styles(value);
    QJsonArray entries;
    for (const auto& s : value.entries)
        entries.append(QJsonObject{{"enabled", s.enabled},
                                   {"color", s.color.name(QColor::HexArgb)},
                                   {"opacity", s.opacity},
                                   {"blend", int(s.blend)},
                                   {"size", s.size},
                                   {"angle", s.angle},
                                   {"distance", s.distance},
                                   {"spread", s.spread},
                                   {"position", s.position},
                                   {"globalLight", s.global_light}});
    return {{"version", 1},
            {"enabled", value.enabled},
            {"globalAngle", value.global_angle},
            {"entries", entries}};
}
LayerStyles styles_from_json(const QJsonObject& json) {
    if (json.value("version").toInt() != 1 || json.value("entries").toArray().size() != 6)
        throw EditError(QStringLiteral("图层样式版本或数量无效"));
    LayerStyles value;
    value.enabled = json.value("enabled").toBool(true);
    value.global_angle = json.value("globalAngle").toDouble(120);
    const auto entries = json.value("entries").toArray();
    for (int i = 0; i < 6; ++i) {
        const auto o = entries[i].toObject();
        auto& s = value.entries[i];
        s.enabled = o.value("enabled").toBool();
        s.color = QColor(o.value("color").toString("#ff000000"));
        s.opacity = o.value("opacity").toDouble(.75);
        s.blend = static_cast<Blend>(o.value("blend").toInt());
        s.size = o.value("size").toDouble(5);
        s.angle = o.value("angle").toDouble(120);
        s.distance = o.value("distance").toDouble(5);
        s.spread = o.value("spread").toDouble();
        s.position = o.value("position").toInt();
        s.global_light = o.value("globalLight").toBool(true);
    }
    validate_styles(value);
    return value;
}
} // namespace huelith::editing
