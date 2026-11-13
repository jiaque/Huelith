#include <QCoreApplication>
#include "huelith/editing/layer_style.h"
#include "huelith/editing/project.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace huelith::editing {
LayerStyles::LayerStyles() {
    for (int i = 0; i < 10; ++i)
        entries[i].kind = i;
    entries[0].opacity = 1;
    entries[1].blend = entries[2].blend = Blend::multiply;
    entries[3].color = entries[4].color = QColor("#ffffbe");
    entries[3].blend = entries[4].blend = Blend::screen;
    entries[5].opacity = 1;
    entries[6].size = entries[7].size = 100;
    entries[6].global_light = entries[7].global_light = false;
    entries[9].blend = Blend::multiply;
}
bool LayerStyles::active() const {
    return enabled &&
           std::any_of(entries.begin(), entries.end(), [](const auto& s) { return s.enabled; });
}
void validate_styles(const LayerStyles& value) {
    if (value.entries.size() < 6 || value.entries.size() > 32 || !std::isfinite(value.scale) ||
        value.scale < .01 || value.scale > 10)
        throw EditError(QCoreApplication::translate("Huelith", "样式数量或缩放无效"));
    if (!std::isfinite(value.global_angle) || std::abs(value.global_angle) > 360)
        throw EditError(QCoreApplication::translate("Huelith", "样式全局光角度无效"));
    for (const auto& s : value.entries)
        if (s.kind < 0 || s.kind > 9 || s.contour < 0 || s.contour > 3 || !s.color.isValid() ||
            !s.color2.isValid() || s.pattern.width() > 512 || s.pattern.height() > 512 ||
            !std::isfinite(s.opacity) || s.opacity < 0 || s.opacity > 1 || int(s.blend) < 0 ||
            int(s.blend) > 23 || !std::isfinite(s.size) || s.size < 0 || s.size > 250 ||
            !std::isfinite(s.angle) || std::abs(s.angle) > 360 || !std::isfinite(s.distance) ||
            s.distance < 0 || s.distance > 1000 || !std::isfinite(s.spread) || s.spread < 0 ||
            s.spread > 1 || s.position < 0 || s.position > 2)
            throw EditError(QCoreApplication::translate("Huelith", "图层样式参数无效"));
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
        throw EditError(QCoreApplication::translate("Huelith", "图层样式输入无效"));
    const int width = source.width(), height = source.height();
    std::vector<float> alpha(std::size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const float*>(source.constScanLine(y));
        for (int x = 0; x < width; ++x)
            alpha[std::size_t(y) * width + x] = std::clamp(row[x * 4 + 3], 0.F, 1.F);
    }
    if (kind == StyleKind::color_overlay || kind == StyleKind::gradient_overlay ||
        kind == StyleKind::pattern_overlay)
        return alpha;
    auto result = alpha;
    const double size = style.size * pixel_scale;
    if (kind == StyleKind::bevel) {
        auto height_map = distances(alpha, width, height, false);
        const double radius = std::max(1., size);
        for (auto& h : height_map) {
            h = float(std::clamp(h / radius, 0., 1.));
            if (style.contour == 1)
                h = h * h * (3 - 2 * h);
            else if (style.contour == 2)
                h *= h;
            else if (style.contour == 3)
                h = std::sqrt(h);
        }
        const double theta =
            (style.global_light ? global_angle : style.angle) * std::numbers::pi / 180;
        const double lx = -std::cos(theta), ly = std::sin(theta);
        auto sample = [&](int x, int y) {
            return x < 0 || y < 0 || x >= width || y >= height
                       ? 0.F
                       : height_map[std::size_t(y) * width + x];
        };
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const double dx = (sample(x + 1, y) - sample(x - 1, y)) * .5,
                             dy = (sample(x, y + 1) - sample(x, y - 1)) * .5;
                const double shade = std::clamp(
                    (-dx * lx - dy * ly) * std::max(1., style.distance) * radius, -1., 1.);
                result[std::size_t(y) * width + x] =
                    float(shade) * alpha[std::size_t(y) * width + x];
            }
        return result;
    }
    if (kind == StyleKind::satin) {
        blur(result, width, height, size, 0);
        const auto blurred = result;
        const double theta = style.angle * std::numbers::pi / 180;
        const int dx = qRound(std::cos(theta) * style.distance * pixel_scale),
                  dy = qRound(std::sin(theta) * style.distance * pixel_scale);
        auto sample = [&](int x, int y) {
            return x < 0 || y < 0 || x >= width || y >= height
                       ? 0.F
                       : blurred[std::size_t(y) * width + x];
        };
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                float value = std::abs(sample(x + dx, y + dy) - sample(x - dx, y - dy));
                if (style.contour == 1)
                    value = value * value * (3 - 2 * value);
                else if (style.contour == 2)
                    value *= value;
                else if (style.contour == 3)
                    value = std::sqrt(value);
                result[std::size_t(y) * width + x] = value * alpha[std::size_t(y) * width + x];
            }
        return result;
    }
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
            value = std::clamp(value, 0.F, 1.F);
            if (style.contour == 1)
                value = value * value * (3 - 2 * value);
            else if (style.contour == 2)
                value *= value;
            else if (style.contour == 3)
                value = std::sqrt(value);
            result[i] = std::clamp(value, 0.F, 1.F) * (interior ? alpha[i] : 1);
        }
    return result;
}
QJsonObject styles_json(const LayerStyles& value) {
    validate_styles(value);
    QJsonArray entries;
    for (const auto& s : value.entries) {
        QByteArray pattern;
        if (!s.pattern.isNull()) {
            QBuffer buffer(&pattern);
            buffer.open(QIODevice::WriteOnly);
            if (!s.pattern.save(&buffer, "PNG"))
                throw EditError(QCoreApplication::translate("Huelith", "图案编码失败"));
        }
        entries.append(QJsonObject{{"kind", s.kind},
                                   {"contour", s.contour},
                                   {"enabled", s.enabled},
                                   {"color", s.color.name(QColor::HexArgb)},
                                   {"color2", s.color2.name(QColor::HexArgb)},
                                   {"pattern", QString::fromLatin1(pattern.toBase64())},
                                   {"opacity", s.opacity},
                                   {"blend", int(s.blend)},
                                   {"size", s.size},
                                   {"angle", s.angle},
                                   {"distance", s.distance},
                                   {"spread", s.spread},
                                   {"position", s.position},
                                   {"globalLight", s.global_light}});
    }
    return {{"version", 3},
            {"scale", value.scale},
            {"enabled", value.enabled},
            {"globalAngle", value.global_angle},
            {"entries", entries}};
}
LayerStyles styles_from_json(const QJsonObject& json) {
    if (json.value("version").toInt() < 1 || json.value("version").toInt() > 3 ||
        json.value("entries").toArray().size() < 6 || json.value("entries").toArray().size() > 32)
        throw EditError(QCoreApplication::translate("Huelith", "图层样式版本或数量无效"));
    LayerStyles value;
    value.enabled = json.value("enabled").toBool(true);
    value.global_angle = json.value("globalAngle").toDouble(120);
    const auto entries = json.value("entries").toArray();
    value.scale = json.value("scale").toDouble(1);
    value.entries.resize(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto o = entries[i].toObject();
        auto& s = value.entries[i];
        s.kind = o.value("kind").toInt(i);
        s.contour = o.value("contour").toInt();
        s.enabled = o.value("enabled").toBool();
        s.color = QColor(o.value("color").toString("#ff000000"));
        s.color2 = QColor(o.value("color2").toString("#ffffffff"));
        const auto encoded = o.value("pattern").toString();
        if (encoded.size() > 2 * 1024 * 1024)
            throw EditError(QCoreApplication::translate("Huelith", "样式图案过大"));
        if (!encoded.isEmpty()) {
            auto bytes = QByteArray::fromBase64(encoded.toLatin1());
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG");
            const auto size = reader.size();
            if (size.isEmpty() || size.width() > 512 || size.height() > 512)
                throw EditError(QCoreApplication::translate("Huelith", "样式图案尺寸无效"));
            s.pattern = reader.read();
            if (s.pattern.isNull())
                throw EditError(QCoreApplication::translate("Huelith", "样式图案损坏"));
        }
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
