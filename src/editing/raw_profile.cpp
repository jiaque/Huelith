#include "huelith/editing/raw_profile.h"
#include "huelith/editing/project.h"
#include "huelith/editing/white_balance.h"
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
namespace {
using Matrix = std::array<double, 9>;
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result{};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            for (int k = 0; k < 3; ++k)
                result[y * 3 + x] += a[y * 3 + k] * b[k * 3 + x];
    return result;
}
Matrix inverse(const Matrix& a) {
    const double d = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
                     a[2] * (a[3] * a[7] - a[4] * a[6]);
    if (!std::isfinite(d) || std::abs(d) < 1e-10)
        throw EditError(QStringLiteral("描述文件相机矩阵不可逆"));
    Matrix result{a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
                  a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
                  a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3]};
    for (auto& v : result)
        v /= d;
    return result;
}
Matrix matrix(const QJsonObject& object, const char* field) {
    const auto values = object.value(QLatin1String(field)).toArray();
    if (values.size() != 9)
        throw EditError(QStringLiteral("描述文件需要3×3相机矩阵"));
    Matrix result;
    for (int i = 0; i < 9; ++i) {
        if (!values[i].isDouble() || !std::isfinite(values[i].toDouble()) ||
            std::abs(values[i].toDouble()) > 100)
            throw EditError(QStringLiteral("描述文件相机矩阵数值无效"));
        result[i] = values[i].toDouble();
    }
    (void)inverse(result);
    return result;
}
void check(const std::atomic_bool* cancelled) {
    if (cancelled && cancelled->load())
        throw EditError(QStringLiteral("描述文件处理已取消"));
}
} // namespace
QJsonArray builtin_raw_profiles() {
    QJsonArray profiles;
    const QStringList ids{"color",    "monochrome", "neutral",  "vivid",
                          "portrait", "landscape",  "standard", "vintage"};
    const QStringList names{QStringLiteral("色彩"), QStringLiteral("单色"), QStringLiteral("中性"),
                            QStringLiteral("鲜艳"), QStringLiteral("人像"), QStringLiteral("风景"),
                            QStringLiteral("标准"), QStringLiteral("复古")};
    for (int i = 0; i < ids.size(); ++i) {
        ToneSettings tone;
        if (i == 1)
            tone.monochrome = true;
        if (i == 2)
            tone.contrast = -12;
        if (i == 3) {
            tone.contrast = 12;
            tone.vibrance = 20;
        }
        if (i == 4) {
            tone.contrast = -5;
            tone.saturation_bands[1] = -5;
            tone.luminance_bands[1] = 5;
        }
        if (i == 5) {
            tone.contrast = 10;
            tone.saturation_bands[3] = 12;
            tone.saturation_bands[5] = 12;
        }
        if (i == 6)
            tone.contrast = 6;
        if (i == 7) {
            tone.contrast = -8;
            tone.saturation = -18;
            tone.grading_hue[0] = 210;
            tone.grading_hue[2] = 45;
            tone.grading_saturation[0] = 8;
            tone.grading_saturation[2] = 12;
        }
        profiles.append(QJsonObject{{"id", "huelith." + ids[i]},
                                    {"name", "Huelith " + names[i]},
                                    {"version", 1},
                                    {"group", i == 7 ? "creative" : "raw"},
                                    {"adjustable", i == 7},
                                    {"amount", 100},
                                    {"tone", tone_json(tone)}});
    }
    return profiles;
}
QJsonObject validate_raw_profile(const QJsonObject& input) {
    if (input.isEmpty())
        return {};
    auto profile = input;
    const auto id = profile.value("id").toString();
    if (id.startsWith("huelith.")) {
        bool found{};
        for (const auto& value : builtin_raw_profiles())
            if (value.toObject().value("id") == id) {
                profile = value.toObject();
                found = true;
                break;
            }
        if (!found)
            throw EditError(QStringLiteral("未知内置RAW描述文件"));
        profile.insert("amount", input.value("amount").toDouble(100));
    }
    if (profile.value("version").toInt() != 1 ||
        profile.value("name").toString().trimmed().isEmpty() ||
        profile.value("name").toString().size() > 120 || id.isEmpty() || id.size() > 160)
        throw EditError(QStringLiteral("RAW描述文件标识或版本无效"));
    const double amount = profile.value("amount").toDouble(100);
    if (!std::isfinite(amount) || amount < 0 || amount > 200)
        throw EditError(QStringLiteral("描述文件数量必须为0–200"));
    if (!profile.value("adjustable").toBool())
        profile.insert("amount", 100);
    if (profile.contains("matrix1")) {
        (void)matrix(profile, "matrix1");
        if (profile.value("camera").toString().trimmed().isEmpty())
            throw EditError(QStringLiteral("相机描述文件必须指定相机型号"));
        if (profile.contains("matrix2")) {
            (void)matrix(profile, "matrix2");
            const double a = profile.value("temperature1").toDouble(),
                         b = profile.value("temperature2").toDouble();
            if (!(a >= 1500 && a <= 50000 && b > a && b <= 50000))
                throw EditError(QStringLiteral("双光源描述文件色温无效"));
        }
    }
    (void)tone_from_json(profile.value("tone").toObject());
    return profile;
}
QJsonObject read_raw_profile(const QByteArray& bytes) {
    if (bytes.size() > 1024 * 1024)
        throw EditError(QStringLiteral("RAW描述文件过大"));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError ||
        document.object().value("type") != "huelith.raw-profile")
        throw EditError(QStringLiteral("不是Huelith RAW描述文件"));
    auto profile = validate_raw_profile(document.object());
    if (profile.value("id").toString().startsWith("huelith."))
        throw EditError(QStringLiteral("自定义描述文件不能替换内置标识"));
    return profile;
}
QImage apply_raw_profile(const QImage& source, const QJsonObject& recipe,
                         const std::atomic_bool* cancelled) {
    const auto profile = validate_raw_profile(recipe.value("profile").toObject());
    if (profile.isEmpty())
        return source;
    check(cancelled);
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("RAW描述文件需要线性浮点图像"));
    QImage result = source;
    if (profile.contains("matrix1")) {
        const auto metadata = recipe.value("metadata").toObject();
        if (profile.value("camera").toString().compare(metadata.value("camera").toString(),
                                                       Qt::CaseInsensitive) != 0)
            throw EditError(QStringLiteral("描述文件与当前相机型号不匹配"));
        auto camera = matrix(profile, "matrix1");
        if (profile.contains("matrix2")) {
            const auto second = matrix(profile, "matrix2");
            auto wb = white_balance_from_json(recipe.value("whiteBalance").toObject());
            if (wb.mode == "as-shot")
                wb = white_balance_as_shot(metadata);
            const double a = profile.value("temperature1").toDouble(),
                         b = profile.value("temperature2").toDouble();
            const double t = std::clamp((1 / wb.temperature - 1 / a) / (1 / b - 1 / a), 0., 1.);
            for (int i = 0; i < 9; ++i)
                camera[i] += t * (second[i] - camera[i]);
        }
        const Matrix xyz_srgb{3.2404542, -1.5371385, -.4985314, -.969266, 1.8760108,
                              .041556,   .0556434,   -.2040259, 1.0572252};
        const Matrix xyz_rec{1.716651188, -.355670784, -.253366281, -.666684352, 1.616481237,
                             .015768546,  .017639857,  -.042770613, .942103121};
        auto normalized = multiply(camera, inverse(xyz_srgb));
        for (int y = 0; y < 3; ++y) {
            const double sum = normalized[y * 3] + normalized[y * 3 + 1] + normalized[y * 3 + 2];
            if (!std::isfinite(sum) || std::abs(sum) < 1e-10)
                throw EditError(QStringLiteral("描述文件中性轴无效"));
            for (int x = 0; x < 3; ++x)
                normalized[y * 3 + x] /= sum;
        }
        const auto gains = metadata.value("asShotGains").toArray();
        if (gains.size() != 3)
            throw EditError(QStringLiteral("RAW缺少相机白平衡元数据"));
        Matrix balance{};
        for (int c = 0; c < 3; ++c) {
            const double v = gains[c].toDouble();
            if (!std::isfinite(v) || v <= 0 || v > 100)
                throw EditError(QStringLiteral("相机白平衡无效"));
            balance[c * 3 + c] = v;
        }
        const auto correction = multiply(
            multiply(multiply(xyz_rec, inverse(xyz_srgb)), multiply(inverse(normalized), balance)),
            inverse(matrix(metadata, "cameraToWorking")));
        for (int y = 0; y < result.height(); ++y) {
            check(cancelled);
            auto* row = reinterpret_cast<float*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                auto* p = row + x * 4;
                const std::array<double, 3> old{p[0], p[1], p[2]};
                for (int c = 0; c < 3; ++c) {
                    double v{};
                    for (int k = 0; k < 3; ++k)
                        v += correction[c * 3 + k] * old[k];
                    p[c] = float(v);
                }
            }
        }
    }
    const double amount = profile.value("amount").toDouble(100) / 100.;
    const auto tone = tone_from_json(profile.value("tone").toObject());
    if (tone == ToneSettings{} || amount == 0)
        return result;
    const auto adjusted = apply_tone(result, tone, cancelled);
    if (amount == 1)
        return adjusted;
    for (int y = 0; y < result.height(); ++y) {
        check(cancelled);
        auto* row = reinterpret_cast<float*>(result.scanLine(y));
        const auto* changed = reinterpret_cast<const float*>(adjusted.constScanLine(y));
        for (int x = 0; x < result.width(); ++x)
            for (int c = 0; c < 3; ++c) {
                const int i = x * 4 + c;
                row[i] = float(row[i] + amount * (changed[i] - row[i]));
            }
    }
    return result;
}
ToneSettings automatic_raw_tone(const QImage& source, ToneSettings existing) {
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("自动调整需要线性浮点图像"));
    std::vector<double> values;
    const int stride =
        std::max(1, int(std::sqrt(double(source.width()) * source.height() / 100000.)));
    for (int y = 0; y < source.height(); y += stride) {
        const auto* row = reinterpret_cast<const float*>(source.constScanLine(y));
        for (int x = 0; x < source.width(); x += stride) {
            const auto* p = row + x * 4;
            if (p[3] < .01F)
                continue;
            const double l = (.2627 * p[0] + .6780 * p[1] + .0593 * p[2]) / p[3];
            if (std::isfinite(l) && l > 1e-8)
                values.push_back(l);
        }
    }
    if (values.empty())
        return existing;
    std::sort(values.begin(), values.end());
    const auto quantile = [&](double p) {
        return values[std::size_t(p * double(values.size() - 1))];
    };
    const double middle = quantile(.5), high = quantile(.995), low = quantile(.01);
    const double exposure =
        std::clamp(std::min(std::log2(.18 / middle), std::log2(.95 / high) + 1), -5., 5.);
    const double gain = std::exp2(exposure);
    existing.sections[0] = true;
    existing.exposure = exposure;
    existing.contrast = std::clamp((.5 - (quantile(.9) - quantile(.1)) * gain) * 30., -20., 20.);
    existing.highlights = -std::clamp((high * gain - .85) * 60., 0., 80.);
    existing.shadows = std::clamp((.08 - quantile(.1) * gain) * 400., 0., 50.);
    existing.whites = std::clamp((.95 - high * gain) * 20., -25., 25.);
    existing.blacks = -std::clamp(low * gain * 100., 0., 15.);
    return existing;
}
} // namespace huelith::editing
