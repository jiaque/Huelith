#include "huelith/editing/white_balance.h"
#include "huelith/editing/project.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
namespace {
using Matrix = std::array<double, 9>;
using Vector = std::array<double, 3>;
// Robertson isotemperature table, adapted from the Adobe DNG SDK.
// Copyright 2006 Adobe Systems Incorporated. All Rights Reserved.
// NOTICE: Adobe permits use, modification and distribution in accordance with
// the accompanying Adobe license, retained in licenses/Adobe-DNG-SDK.txt.
struct TemperatureLine {
    double reciprocal, u, v, slope;
};
constexpr TemperatureLine temperatures[] = {
#include "temperature_table.inc"
};
Matrix matrix(const QJsonObject& metadata, const char* name) {
    const auto values = metadata.value(name).toArray();
    if (values.size() != 9)
        throw EditError(QStringLiteral("RAW 白平衡相机矩阵缺失，请重新显影源文件"));
    Matrix result;
    for (int i = 0; i < 9; ++i) {
        if (!values[i].isDouble() || !std::isfinite(values[i].toDouble()))
            throw EditError(QStringLiteral("RAW 白平衡相机矩阵无效"));
        result[i] = values[i].toDouble();
    }
    return result;
}
Matrix inverse(const Matrix& m) {
    const double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (!std::isfinite(d) || std::abs(d) < 1e-12)
        throw EditError(QStringLiteral("RAW 白平衡相机矩阵不可逆"));
    Matrix r{m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4],
             m[5] * m[6] - m[3] * m[8], m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
             m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3]};
    for (auto& v : r)
        v /= d;
    return r;
}
Vector transform(const Matrix& m, const Vector& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}
Vector shot_gains(const QJsonObject& metadata) {
    const auto values = metadata.value("asShotGains").toArray();
    if (values.size() != 3)
        throw EditError(QStringLiteral("RAW 拍摄白平衡缺失"));
    Vector gains;
    for (int i = 0; i < 3; ++i) {
        gains[i] = values[i].toDouble(-1);
        if (!std::isfinite(gains[i]) || gains[i] <= 0)
            throw EditError(QStringLiteral("RAW 拍摄白平衡无效"));
    }
    return gains;
}
WhiteBalance from_neutral(const QJsonObject& metadata, const Vector& neutral) {
    const auto xyz = transform(inverse(matrix(metadata, "xyzToCamera")), neutral);
    const double sum = xyz[0] + xyz[1] + xyz[2];
    if (!std::isfinite(sum) || sum <= 1e-12)
        throw EditError(QStringLiteral("无法从该区域估计白平衡"));
    return white_balance_temperature({xyz[0] / sum, xyz[1] / sum});
}
} // namespace
QPointF white_balance_xy(double temperature, double tint) {
    if (!std::isfinite(temperature) || temperature < 2000 || temperature > 50000 ||
        !std::isfinite(tint) || std::abs(tint) > 150)
        throw EditError(QStringLiteral("白平衡参数超出范围"));
    const double reciprocal = 1e6 / temperature;
    int i = 0;
    while (i < 29 && reciprocal >= temperatures[i + 1].reciprocal)
        ++i;
    const auto& a = temperatures[i];
    const auto& b = temperatures[i + 1];
    const double f = (b.reciprocal - reciprocal) / (b.reciprocal - a.reciprocal);
    double u = f * a.u + (1 - f) * b.u, v = f * a.v + (1 - f) * b.v;
    const double la = std::hypot(1., a.slope), lb = std::hypot(1., b.slope);
    double du = f / la + (1 - f) / lb, dv = f * a.slope / la + (1 - f) * b.slope / lb;
    const double length = std::hypot(du, dv);
    u -= du / length * tint / 3000;
    v -= dv / length * tint / 3000;
    return {1.5 * u / (u - 4 * v + 2), v / (u - 4 * v + 2)};
}
WhiteBalance white_balance_temperature(QPointF xy) {
    if (!std::isfinite(xy.x()) || !std::isfinite(xy.y()) || xy.x() <= 0 || xy.y() <= 0 ||
        std::abs(1.5 - xy.x() + 6 * xy.y()) < 1e-12)
        throw EditError(QStringLiteral("白点色度无效"));
    const double u = 2 * xy.x() / (1.5 - xy.x() + 6 * xy.y()),
                 v = 3 * xy.y() / (1.5 - xy.x() + 6 * xy.y());
    double last_distance = 0, last_du = 0, last_dv = 0;
    for (int i = 1; i <= 30; ++i) {
        const auto& a = temperatures[i - 1];
        const auto& b = temperatures[i];
        const double length = std::hypot(1., b.slope), du = 1 / length, dv = b.slope / length;
        double distance = -(u - b.u) * dv + (v - b.v) * du;
        if (distance <= 0 || i == 30) {
            distance = -std::min(0., distance);
            const double f = i == 1 ? 0 : distance / (last_distance + distance);
            const double reciprocal = f * a.reciprocal + (1 - f) * b.reciprocal;
            const double uu = u - (f * a.u + (1 - f) * b.u), vv = v - (f * a.v + (1 - f) * b.v);
            const double tu = (1 - f) * du + f * last_du, tv = (1 - f) * dv + f * last_dv;
            return {"custom", std::clamp(1e6 / reciprocal, 2000., 50000.),
                    std::clamp(-3000 * (uu * tu + vv * tv) / std::hypot(tu, tv), -150., 150.)};
        }
        last_distance = distance;
        last_du = du;
        last_dv = dv;
    }
    throw EditError(QStringLiteral("无法估计白平衡"));
}
WhiteBalance white_balance_from_json(const QJsonObject& o) {
    if (o.isEmpty())
        return {};
    if (o.value("version").toInt() != 1)
        throw EditError(QStringLiteral("白平衡配方版本无效"));
    const QStringList modes{"as-shot", "auto",     "custom",      "daylight", "cloudy",
                            "shade",   "tungsten", "fluorescent", "flash"};
    WhiteBalance s{o.value("mode").toString(), o.value("temperature").toDouble(-1),
                   o.value("tint").toDouble(999)};
    if (!modes.contains(s.mode))
        throw EditError(QStringLiteral("白平衡模式无效"));
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.key() != "version" && it.key() != "mode" && it.key() != "temperature" &&
            it.key() != "tint")
            throw EditError(QStringLiteral("未知白平衡参数"));
    (void)white_balance_xy(s.temperature, s.tint);
    return s;
}
QJsonObject white_balance_json(const WhiteBalance& s) {
    QJsonObject result{
        {"version", 1}, {"mode", s.mode}, {"temperature", s.temperature}, {"tint", s.tint}};
    (void)white_balance_from_json(result);
    return result;
}
WhiteBalance white_balance_as_shot(const QJsonObject& metadata) {
    auto gains = shot_gains(metadata);
    for (auto& v : gains)
        v = 1 / v;
    auto s = from_neutral(metadata, gains);
    s.mode = "as-shot";
    return s;
}
WhiteBalance white_balance_sample(const QImage& image, const QJsonObject& metadata, QRect region) {
    if (image.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("白平衡需要线性 RAW 预览"));
    const auto to_camera = inverse(matrix(metadata, "cameraToWorking"));
    region = region.intersected(image.rect());
    Vector sum{};
    int count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        const auto* row = reinterpret_cast<const float*>(image.constScanLine(y));
        for (int x = region.left(); x <= region.right(); ++x) {
            const auto* p = row + x * 4;
            if (p[3] <= .01F)
                continue;
            const auto cam = transform(to_camera, {p[0] / p[3], p[1] / p[3], p[2] / p[3]});
            if (*std::min_element(cam.begin(), cam.end()) < .001 ||
                *std::max_element(cam.begin(), cam.end()) > .98)
                continue;
            for (int c = 0; c < 3; ++c)
                sum[c] += cam[c];
            ++count;
        }
    }
    if (!count)
        throw EditError(QStringLiteral("请在未过曝的中性灰区域取样"));
    return from_neutral(metadata, sum);
}
QImage apply_white_balance(const QImage& source, const QJsonObject& recipe,
                           const std::atomic_bool* cancelled) {
    const auto settings = white_balance_from_json(recipe.value("whiteBalance").toObject());
    if (settings.mode == "as-shot")
        return source;
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("白平衡需要线性 RAW 图像"));
    const auto metadata = recipe.value("metadata").toObject();
    const auto camera = matrix(metadata, "cameraToWorking"), back = inverse(camera);
    const auto xy = white_balance_xy(settings.temperature, settings.tint);
    auto neutral = transform(matrix(metadata, "xyzToCamera"),
                             {xy.x() / xy.y(), 1, (1 - xy.x() - xy.y()) / xy.y()});
    const auto shot = shot_gains(metadata);
    const double maximum = *std::max_element(neutral.begin(), neutral.end());
    if (!std::isfinite(maximum) || maximum <= 0)
        throw EditError(QStringLiteral("相机白点无效"));
    // DNG SDK bounds normalized camera neutral values to positive sensor responses.
    // Extreme tint settings can otherwise cross outside the camera gamut.
    for (auto& value : neutral)
        value = std::clamp(value / maximum, .001, 1.);
    Vector ratio;
    for (int c = 0; c < 3; ++c) {
        ratio[c] = neutral[1] / neutral[c] / shot[c];
    }
    Matrix correction{};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            for (int k = 0; k < 3; ++k)
                correction[y * 3 + x] += camera[y * 3 + k] * ratio[k] * back[k * 3 + x];
    QImage result = source;
    for (int y = 0; y < result.height(); ++y) {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("白平衡已取消"));
        auto* row = reinterpret_cast<float*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            auto* p = row + x * 4;
            const auto adjusted = transform(correction, {p[0], p[1], p[2]});
            for (int c = 0; c < 3; ++c)
                p[c] = float(adjusted[c]);
        }
    }
    return result;
}
} // namespace huelith::editing
