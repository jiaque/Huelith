#include "huelith/editing/optics.h"
#include "huelith/editing/point_color.h"
#include "huelith/editing/project.h"
#include <QColorSpace>
#include <QDir>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <lensfun/lensfun.h>
#include <memory>

namespace huelith::editing {
namespace {
void check_cancel(const std::atomic_bool* cancelled) {
    if (cancelled && cancelled->load())
        throw EditError(QStringLiteral("光学校正已取消"));
}
void bounded(double value, double low, double high) {
    if (!std::isfinite(value) || value < low || value > high)
        throw EditError(QStringLiteral("光学校正参数超出范围"));
}
float sample(const QImage& image, double x, double y, int channel) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > image.width() - 1 ||
        y > image.height() - 1)
        return 0;
    const int ix = int(x), iy = int(y), right = std::min(ix + 1, image.width() - 1),
              bottom = std::min(iy + 1, image.height() - 1);
    const double dx = x - ix, dy = y - iy;
    const auto* a = reinterpret_cast<const float*>(image.constScanLine(iy));
    const auto* b = reinterpret_cast<const float*>(image.constScanLine(bottom));
    return float((1 - dy) * ((1 - dx) * a[ix * 4 + channel] + dx * a[right * 4 + channel]) +
                 dy * ((1 - dx) * b[ix * 4 + channel] + dx * b[right * 4 + channel]));
}
QString normalized(QString text) {
    return text.toLower().simplified().remove(' ');
}
double hue_weight(double hue, const std::array<double, 2>& range, bool purple) {
    const double value = (hue * 360 - (purple ? 180 : 60)) / (purple ? 1.8 : 1.2);
    if (value < range[0] || value > range[1])
        return 0;
    return std::clamp(std::min(value - range[0], range[1] - value) / 5., 0., 1.);
}
// A bounded image-based fallback aligns channel edge positions when no TCA profile exists.
double estimate_ca(const QImage& source, int channel, const std::atomic_bool* cancelled) {
    const auto small = source.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const double cx = (small.width() - 1) / 2., cy = (small.height() - 1) / 2.;
    double best = 1, best_cost = 1e30;
    for (int step = -12; step <= 12; ++step) {
        check_cancel(cancelled);
        const double scale = 1 + step * .0005;
        double cost = 0, count = 0;
        for (int y = 2; y < small.height() - 2; y += 2)
            for (int x = 2; x < small.width() - 2; x += 2) {
                const double gx = sample(small, x + 1, y, 1) - sample(small, x - 1, y, 1);
                const double gy = sample(small, x, y + 1, 1) - sample(small, x, y - 1, 1);
                const double g = std::hypot(gx, gy);
                if (g < .015)
                    continue;
                const double px = cx + (x - cx) * scale, py = cy + (y - cy) * scale;
                const double rx =
                    sample(small, px + 1, py, channel) - sample(small, px - 1, py, channel);
                const double ry =
                    sample(small, px, py + 1, channel) - sample(small, px, py - 1, channel);
                const double r = std::hypot(rx, ry);
                // Compare edge orientation and normalized strength; reject colored flat surfaces.
                cost += 1 - (rx * gx + ry * gy) / std::max(1e-8, r * g) +
                        .1 * std::abs(r - g) / std::max(.01, r + g);
                ++count;
            }
        cost = count > 32 ? cost / count : 1e30;
        if (cost < best_cost - 1e-7 ||
            (std::abs(cost - best_cost) < 1e-7 && std::abs(scale - 1) < std::abs(best - 1))) {
            best_cost = cost;
            best = scale;
        }
    }
    return best;
}
} // namespace
std::vector<LensProfile> lens_profiles(const QString& directory) {
    lfDatabase db;
    if (!db.LoadDirectory(QDir::toNativeSeparators(directory).toUtf8().constData()))
        throw EditError(QStringLiteral("镜头数据库不可用"));
    std::vector<LensProfile> result;
    const auto* lenses = db.GetLenses();
    for (int i = 0; lenses && lenses[i]; ++i) {
        const auto* lens = lenses[i];
        if (lens->Type != LF_RECTILINEAR)
            continue;
        const lfLens* selection[]{lens, nullptr};
        char* xml = lfDatabase::Save(nullptr, nullptr, selection);
        if (!xml)
            continue;
        result.push_back({QString::fromUtf8(lens->Maker), QString::fromUtf8(lens->Model),
                          QString::fromUtf8(xml), lens->CropFactor,
                          lens->CalibDistortion && lens->CalibDistortion[0],
                          lens->CalibVignetting && lens->CalibVignetting[0],
                          lens->CalibTCA && lens->CalibTCA[0]});
        lf_free(xml);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.maker == b.maker ? a.model < b.model : a.maker < b.maker;
    });
    return result;
}
double camera_crop_factor(const QString& directory, const QJsonObject& metadata) {
    lfDatabase db;
    if (!db.LoadDirectory(QDir::toNativeSeparators(directory).toUtf8().constData()))
        return 0;
    const auto maker = metadata.value("cameraMake").toString().toUtf8();
    const auto model = metadata.value("camera").toString().toUtf8();
    if (model.isEmpty())
        return 0;
    const auto cameras = db.GetCameras();
    const auto camera_maker = normalized(QString::fromUtf8(maker));
    const auto camera_model = normalized(QString::fromUtf8(model));
    double crop{};
    for (int i = 0; cameras && cameras[i]; ++i) {
        const auto actual_maker = normalized(QString::fromUtf8(cameras[i]->Maker));
        const auto actual_model = normalized(QString::fromUtf8(cameras[i]->Model));
        if ((!camera_maker.isEmpty() && camera_maker != actual_maker) ||
            (camera_model != actual_model && actual_maker + camera_model != actual_model))
            continue;
        if (crop != 0 && std::abs(crop - cameras[i]->CropFactor) > .001)
            return 0;
        crop = cameras[i]->CropFactor;
    }
    return crop;
}
int match_lens_profile(const std::vector<LensProfile>& profiles, const QJsonObject& metadata) {
    const auto name = normalized(metadata.value("lens").toString());
    if (name.isEmpty())
        return -1;
    int found = -1;
    for (int i = 0; i < int(profiles.size()); ++i) {
        const auto model = normalized(profiles[std::size_t(i)].model);
        const auto maker = normalized(profiles[std::size_t(i)].maker);
        if (name == model || maker + name == model || name == maker + model) {
            if (found >= 0)
                return -1; // Ambiguous mount/calibration requires an explicit choice.
            found = i;
        }
    }
    return found;
}
void validate_optics(const OpticsSettings& s) {
    bounded(s.crop_factor, .1, 20);
    bounded(s.focal, .1, 3000);
    bounded(s.aperture, .1, 128);
    bounded(s.distance, .01, 1e6);
    bounded(s.profile_distortion, 0, 200);
    bounded(s.profile_vignette, 0, 200);
    bounded(s.distortion, -100, 100);
    bounded(s.vignette, -100, 100);
    bounded(s.midpoint, 0, 100);
    bounded(s.purple, 0, 20);
    bounded(s.green, 0, 20);
    for (const auto range : {s.purple_hue, s.green_hue}) {
        bounded(range[0], 0, 90);
        bounded(range[1], 10, 100);
        if (range[1] - range[0] < 10)
            throw EditError(QStringLiteral("去边色相范围至少相隔10"));
    }
    if (s.profile_xml.size() > 2'000'000)
        throw EditError(QStringLiteral("镜头配置过大"));
    if (s.profile_enabled && s.profile_xml.isEmpty())
        throw EditError(QStringLiteral("请先选择镜头配置"));
}
QJsonObject optics_json(const OpticsSettings& s) {
    validate_optics(s);
    return {{"version", 1},
            {"enabled", s.enabled},
            {"profileEnabled", s.profile_enabled},
            {"removeCA", s.remove_ca},
            {"profileXML", s.profile_xml},
            {"profileName", s.profile_name},
            {"cropFactor", s.crop_factor},
            {"focal", s.focal},
            {"aperture", s.aperture},
            {"distance", s.distance},
            {"profileDistortion", s.profile_distortion},
            {"profileVignette", s.profile_vignette},
            {"distortion", s.distortion},
            {"vignette", s.vignette},
            {"midpoint", s.midpoint},
            {"purple", s.purple},
            {"green", s.green},
            {"purpleHue", QJsonArray{s.purple_hue[0], s.purple_hue[1]}},
            {"greenHue", QJsonArray{s.green_hue[0], s.green_hue[1]}}};
}
OpticsSettings optics_from_json(const QJsonObject& json) {
    OpticsSettings s;
    if (json.isEmpty())
        return s;
    if (json.value("version").toInt() != 1)
        throw EditError(QStringLiteral("未知光学配方版本"));
    const auto defaults = optics_json(s);
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!defaults.contains(it.key()))
            throw EditError(QStringLiteral("未知光学参数"));
    for (auto [key, value] : {std::pair{"cropFactor", &s.crop_factor},
                              {"focal", &s.focal},
                              {"aperture", &s.aperture},
                              {"distance", &s.distance},
                              {"profileDistortion", &s.profile_distortion},
                              {"profileVignette", &s.profile_vignette},
                              {"distortion", &s.distortion},
                              {"vignette", &s.vignette},
                              {"midpoint", &s.midpoint},
                              {"purple", &s.purple},
                              {"green", &s.green}}) {
        if (json.contains(QLatin1String(key))) {
            if (!json.value(QLatin1String(key)).isDouble())
                throw EditError(QStringLiteral("光学数值格式无效"));
            *value = json.value(QLatin1String(key)).toDouble();
        }
    }
    for (auto [key, value] : {std::pair{"enabled", &s.enabled},
                              {"profileEnabled", &s.profile_enabled},
                              {"removeCA", &s.remove_ca}})
        if (json.contains(QLatin1String(key))) {
            if (!json.value(QLatin1String(key)).isBool())
                throw EditError(QStringLiteral("光学开关格式无效"));
            *value = json.value(QLatin1String(key)).toBool();
        }
    for (auto [key, value] :
         {std::pair{"profileXML", &s.profile_xml}, {"profileName", &s.profile_name}})
        if (json.contains(QLatin1String(key))) {
            if (!json.value(QLatin1String(key)).isString())
                throw EditError(QStringLiteral("镜头配置格式无效"));
            *value = json.value(QLatin1String(key)).toString();
        }
    for (auto [key, value] : {std::pair{"purpleHue", &s.purple_hue}, {"greenHue", &s.green_hue}})
        if (json.contains(QLatin1String(key))) {
            const auto a = json.value(QLatin1String(key)).toArray();
            if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble())
                throw EditError(QStringLiteral("去边范围无效"));
            *value = {a[0].toDouble(), a[1].toDouble()};
        }
    validate_optics(s);
    return s;
}
QImage apply_optics(const QImage& source, const OpticsSettings& s,
                    const std::atomic_bool* cancelled) {
    validate_optics(s);
    check_cancel(cancelled);
    if (!s.enabled || (!s.profile_enabled && !s.remove_ca && s.distortion == 0 && s.vignette == 0 &&
                       s.purple == 0 && s.green == 0))
        return source;
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        source.colorSpace() != working_space())
        throw EditError(QStringLiteral("光学校正需要线性FP32图像"));
    // Geometry and achromatic gain commute with the working-space matrix. Avoid
    // unnecessary color conversions, including their finite precision error.
    const bool chromatic = s.remove_ca;
    auto input =
        chromatic ? source.convertedToColorSpace(QColorSpace(QColorSpace::SRgbLinear)) : source;
    lfDatabase db;
    std::unique_ptr<lfModifier> modifier;
    int flags{};
    if (!s.profile_xml.isEmpty() && (s.profile_enabled || s.remove_ca)) {
        const auto xml = s.profile_xml.toUtf8();
        if (db.Load("embedded profile", xml.constData(), std::size_t(xml.size())) != LF_NO_ERROR ||
            !db.GetLenses() || !db.GetLenses()[0])
            throw EditError(QStringLiteral("镜头配置无法解析"));
        const auto* lens = db.GetLenses()[0];
        modifier = std::make_unique<lfModifier>(lens, float(s.crop_factor), source.width(),
                                                source.height());
        flags = modifier->Initialize(
            lens, LF_PF_F32, float(s.focal), float(s.aperture), float(s.distance), 1,
            LF_RECTILINEAR,
            (s.profile_enabled ? LF_MODIFY_DISTORTION | LF_MODIFY_VIGNETTING : 0) |
                (s.remove_ca ? LF_MODIFY_TCA : 0),
            false);
    }
    if (modifier && (flags & LF_MODIFY_VIGNETTING) && s.profile_vignette != 0) {
        auto corrected = input.copy();
        for (int y = 0; y < input.height(); ++y) {
            check_cancel(cancelled);
            modifier->ApplyColorModification(corrected.scanLine(y), 0, float(y), input.width(), 1,
                                             LF_CR_4(RED, GREEN, BLUE, UNKNOWN),
                                             int(corrected.bytesPerLine()));
            const auto* a = reinterpret_cast<const float*>(corrected.constScanLine(y));
            auto* b = reinterpret_cast<float*>(input.scanLine(y));
            for (int x = 0; x < input.width(); ++x)
                for (int c = 0; c < 3; ++c)
                    b[x * 4 + c] += float(s.profile_vignette / 100) * (a[x * 4 + c] - b[x * 4 + c]);
        }
    }
    const double red =
        s.remove_ca && !(flags & LF_MODIFY_TCA) ? estimate_ca(input, 0, cancelled) : 1;
    const double blue =
        s.remove_ca && !(flags & LF_MODIFY_TCA) ? estimate_ca(input, 2, cancelled) : 1;
    auto result = input.copy();
    const double cx = (input.width() - 1) / 2., cy = (input.height() - 1) / 2.;
    const double radius = std::max(1., std::hypot(cx, cy));
    std::vector<float> coords(std::size_t(input.width()) * 6);
    for (int y = 0; y < input.height(); ++y) {
        check_cancel(cancelled);
        bool mapped = modifier && modifier->ApplySubpixelGeometryDistortion(
                                      0, float(y), input.width(), 1, coords.data());
        auto* output = reinterpret_cast<float*>(result.scanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            const double nx = (x - cx) / radius, ny = (y - cy) / radius, r2 = nx * nx + ny * ny;
            const double radial = 1 + s.distortion / 250 * (1 - r2);
            const double mx = cx + (x - cx) * radial, my = cy + (y - cy) * radial;
            const double edge = std::clamp(
                (std::sqrt(r2) - s.midpoint / 200) / std::max(.01, 1 - s.midpoint / 200), 0., 1.);
            const double gain = std::exp2(s.vignette / 50 * edge * edge);
            for (int c = 0; c < 4; ++c) {
                const int coordinate = c == 3 ? 1 : c;
                double px = mx, py = my;
                if (mapped) {
                    const double amount = s.profile_enabled ? s.profile_distortion / 100 : 0;
                    // Separate common lens geometry from channel-specific TCA.
                    const double gx = coords[std::size_t(x) * 6 + 2],
                                 gy = coords[std::size_t(x) * 6 + 3];
                    px += (gx - x) * amount + coords[std::size_t(x) * 6 + coordinate * 2] - gx;
                    py += (gy - y) * amount + coords[std::size_t(x) * 6 + coordinate * 2 + 1] - gy;
                }
                const double channel_scale = c == 0 ? red : c == 2 ? blue : 1;
                px = cx + (px - cx) * channel_scale;
                py = cy + (py - cy) * channel_scale;
                output[x * 4 + c] = sample(input, px, py, c) * float(c == 3 ? 1 : gain);
            }
            if (output[x * 4 + 3] <= 0)
                output[x * 4] = output[x * 4 + 1] = output[x * 4 + 2] = 0;
        }
    }
    if (s.purple > 0 || s.green > 0) {
        const auto original = result;
        auto encoded = result.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
        const auto unmodified = encoded;
        for (int y = 0; y < encoded.height(); ++y) {
            check_cancel(cancelled);
            auto* row = reinterpret_cast<float*>(encoded.scanLine(y));
            for (int x = 0; x < encoded.width(); ++x) {
                const double alpha = row[x * 4 + 3];
                if (alpha < 1e-8)
                    continue;
                const std::array<double, 3> rgb{row[x * 4] / alpha, row[x * 4 + 1] / alpha,
                                                row[x * 4 + 2] / alpha};
                const auto hsl = point_color_hsl(rgb);
                const double strength =
                    std::max(s.purple / 20 * hue_weight(hsl[0], s.purple_hue, true),
                             s.green / 20 * hue_weight(hsl[0], s.green_hue, false));
                if (strength == 0)
                    continue;
                double minimum = 1e30, maximum = -1e30;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const double l =
                            sample(unmodified, std::clamp(x + dx, 0, encoded.width() - 1),
                                   std::clamp(y + dy, 0, encoded.height() - 1), 1);
                        minimum = std::min(minimum, l);
                        maximum = std::max(maximum, l);
                    }
                const double weight = strength * std::clamp((maximum - minimum) * 5, 0., 1.);
                const double gray = rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
                for (int c = 0; c < 3; ++c)
                    row[x * 4 + c] = float((rgb[c] + (gray - rgb[c]) * weight) * alpha);
            }
        }
        const auto corrected = encoded.convertedToColorSpace(original.colorSpace());
        const auto baseline = unmodified.convertedToColorSpace(original.colorSpace());
        result = original.copy();
        for (int y = 0; y < result.height(); ++y) {
            check_cancel(cancelled);
            auto* output = reinterpret_cast<float*>(result.scanLine(y));
            const auto* a = reinterpret_cast<const float*>(corrected.constScanLine(y));
            const auto* b = reinterpret_cast<const float*>(baseline.constScanLine(y));
            // Apply only the actual correction, preserving untouched colors exactly.
            for (int x = 0; x < result.width(); ++x)
                for (int c = 0; c < 3; ++c)
                    output[x * 4 + c] += a[x * 4 + c] - b[x * 4 + c];
        }
    }
    return chromatic ? result.convertedToColorSpace(working_space()) : result;
}
} // namespace huelith::editing
