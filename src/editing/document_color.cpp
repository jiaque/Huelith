#include <QCoreApplication>
#include "huelith/editing/document_color.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
namespace {
using Vector = std::array<double, 3>;
Vector multiply(const std::array<double, 9>& a, Vector b) {
    return {a[0] * b[0] + a[1] * b[1] + a[2] * b[2], a[3] * b[0] + a[4] * b[1] + a[5] * b[2],
            a[6] * b[0] + a[7] * b[1] + a[8] * b[2]};
}
void validate(const DocumentColor& c) {
    if (c.mode < 0 || c.mode > 3 || (c.bits != 8 && c.bits != 16 && c.bits != 32) ||
        !c.profile.isValid() || !c.profile.isValidTarget())
        throw EditError(QCoreApplication::translate("Huelith", "文档颜色配置无效"));
    if (c.mode == 3 && (c.profile.colorModel() != QColorSpace::ColorModel::Cmyk || c.bits != 8))
        throw EditError(QCoreApplication::translate("Huelith", "CMYK 需要有效的 CMYK ICC 文件，当前支持 8 位分色"));
    if (c.mode != 3 && c.profile.colorModel() != QColorSpace::ColorModel::Rgb)
        throw EditError(QCoreApplication::translate("Huelith", "当前模式需要 RGB 工作配置文件"));
}
QColorSpace decode(const QJsonValue& value) {
    const auto bytes = QByteArray::fromBase64(value.toString().toLatin1());
    if (bytes.size() > 4 * 1024 * 1024)
        throw EditError(QCoreApplication::translate("Huelith", "ICC 文件超过 4 MB"));
    const auto profile = QColorSpace::fromIccProfile(bytes);
    if (!profile.isValid() || !profile.isValidTarget())
        throw EditError(QCoreApplication::translate("Huelith", "ICC 无法完成双向颜色转换"));
    return profile;
}
QImage roundtrip(const QImage& source, const QColorSpace& profile, QImage::Format format) {
    auto encoded = source.convertedToColorSpace(profile, format);
    if (encoded.isNull())
        throw EditError(QCoreApplication::translate("Huelith", "颜色转换失败"));
    auto result =
        encoded.convertedToColorSpace(working_space(), QImage::Format_RGBA32FPx4_Premultiplied);
    if (result.isNull())
        throw EditError(QCoreApplication::translate("Huelith", "颜色转换返回失败"));
    if (format == QImage::Format_CMYK8888) {
        for (int y = 0; y < source.height(); ++y) {
            const auto* input = reinterpret_cast<const float*>(source.constScanLine(y));
            auto* row = reinterpret_cast<float*>(result.scanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                const float alpha = input[x * 4 + 3];
                for (int c = 0; c < 3; ++c)
                    row[x * 4 + c] *= alpha;
                row[x * 4 + 3] = alpha;
            }
        }
    }
    return result;
}
} // namespace
Vector working_to_lab(Vector rgb) {
    auto xyz = multiply({.6369580483, .1446169036, .1688809752, .2627002120, .6779980715,
                         .0593017165, 0, .0280726930, 1.0609850577},
                        rgb);
    xyz = multiply({1.0479298, .0229468, -.0501922, .0296278, .9904345, -.0170738, -.009243,
                    .0150552, .7518743},
                   xyz);
    const auto f = [](double v) {
        return v > 216. / 24389 ? std::cbrt(v) : (24389. / 27 * v + 16) / 116;
    };
    const double x = f(xyz[0] / .96422), y = f(xyz[1]), z = f(xyz[2] / .82521);
    return {116 * y - 16, 500 * (x - y), 200 * (y - z)};
}
Vector lab_to_working(Vector lab) {
    const double y = (lab[0] + 16) / 116;
    const auto f = [](double v) { return v > 6. / 29 ? v * v * v : (116 * v - 16) * 27 / 24389; };
    Vector xyz{.96422 * f(y + lab[1] / 500), f(y), .82521 * f(y - lab[2] / 200)};
    xyz = multiply({.9554734, -.0230985, .0632593, -.0283697, 1.0099955, .0210414, .012314,
                    -.0205077, 1.3303659},
                   xyz);
    return multiply({1.7166512, -.3556708, -.2533663, -.6666844, 1.6164812, .0157685, .0176399,
                     -.0427706, .9421031},
                    xyz);
}
DocumentColor document_color(const State& state) {
    DocumentColor c;
    const auto json = state.metadata.value("documentColor").toObject();
    if (json.isEmpty())
        return c;
    c.mode = json.value("mode").toInt(-1);
    c.bits = json.value("bits").toInt();
    c.profile = decode(json.value("icc"));
    validate(c);
    return c;
}
QStringList document_channel_names(const State& state) {
    switch (document_color(state).mode) {
    case 1:
        return {QCoreApplication::translate("Huelith", "灰度")};
    case 2:
        return {"L*", "a*", "b*"};
    case 3:
        return {QCoreApplication::translate("Huelith", "青 C"), QCoreApplication::translate("Huelith", "品红 M"), QCoreApplication::translate("Huelith", "黄 Y"),
                QCoreApplication::translate("Huelith", "黑 K")};
    default:
        return {QCoreApplication::translate("Huelith", "红"), QCoreApplication::translate("Huelith", "绿"), QCoreApplication::translate("Huelith", "蓝")};
    }
}
QImage document_channel(const QImage& working, const State& state, int component) {
    const auto config = document_color(state);
    if (component < 0 || component >= document_channel_names(state).size())
        throw EditError(QCoreApplication::translate("Huelith", "颜色通道无效"));
    QImage result(working.size(), QImage::Format_Grayscale8);
    auto input = working;
    if (config.mode == 1) {
        input = working.copy();
        for (int y = 0; y < input.height(); ++y) {
            auto* p = reinterpret_cast<float*>(input.scanLine(y));
            for (int x = 0; x < input.width(); ++x, p += 4)
                p[0] = p[1] = p[2] =
                    float(.262700212 * p[0] + .6779980715 * p[1] + .0593017165 * p[2]);
        }
    }
    const auto encoded =
        config.mode == 3 ? working.convertedToColorSpace(config.profile, QImage::Format_CMYK8888)
                         : input.convertedToColorSpace(config.profile, QImage::Format_RGBA32FPx4);
    for (int y = 0; y < working.height(); ++y)
        for (int x = 0; x < working.width(); ++x) {
            double value = 0;
            if (config.mode == 3)
                value = encoded.constScanLine(y)[x * 4 + component] / 255.;
            else if (config.mode == 2) {
                const auto* p = reinterpret_cast<const float*>(working.constScanLine(y)) + x * 4;
                const auto lab = working_to_lab(
                    p[3] > 0 ? Vector{p[0] / p[3], p[1] / p[3], p[2] / p[3]} : Vector{});
                value = component == 0 ? lab[0] / 100 : (lab[component] + 128) / 255;
            } else {
                const auto* p = reinterpret_cast<const float*>(encoded.constScanLine(y)) + x * 4;
                value = p[component];
            }
            result.scanLine(y)[x] = uchar(std::lround(std::clamp(value, 0., 1.) * 255));
        }
    return result;
}
void apply_document_channel(Layer& layer, const State& state, int component, const QImage& mask) {
    const auto config = document_color(state);
    if (component < 0 || component >= document_channel_names(state).size())
        throw EditError(QCoreApplication::translate("Huelith", "颜色通道无效"));
    auto image = config.mode == 2
                     ? layer.pixels.copy()
                     : layer.pixels.convertedToColorSpace(
                           config.profile,
                           config.mode == 3 ? QImage::Format_CMYK8888 : QImage::Format_RGBA32FPx4);
    const auto matrix = layer.transform * parent_matrix(state, layer.id);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto point = matrix.map(QPointF(x + .5, y + .5));
            const QPoint pixel(int(std::floor(point.x())), int(std::floor(point.y())));
            if (!mask.rect().contains(pixel))
                continue;
            const auto byte = mask.constScanLine(pixel.y())[pixel.x()];
            const double value = byte / 255.;
            if (config.mode == 3)
                image.scanLine(y)[x * 4 + component] = byte;
            else {
                auto* p = reinterpret_cast<float*>(image.scanLine(y)) + x * 4;
                if (p[3] <= 0)
                    continue;
                if (config.mode == 2) {
                    auto lab = working_to_lab({p[0] / p[3], p[1] / p[3], p[2] / p[3]});
                    lab[component] = component == 0 ? value * 100 : value * 255 - 128;
                    const auto rgb = lab_to_working(lab);
                    for (int k = 0; k < 3; ++k)
                        p[k] = float(rgb[k]) * p[3];
                } else if (config.mode == 1)
                    p[0] = p[1] = p[2] = float(value);
                else
                    p[component] = float(value);
            }
        }
    auto result = to_working(image);
    if (config.mode == 3)
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x) {
                auto* p = reinterpret_cast<float*>(result.scanLine(y)) + x * 4;
                const auto* old =
                    reinterpret_cast<const float*>(layer.pixels.constScanLine(y)) + x * 4;
                for (int k = 0; k < 3; ++k)
                    p[k] *= old[3];
                p[3] = old[3];
            }
    layer.pixels = result;
}
void set_document_color(State& state, const DocumentColor& c, bool assign) {
    validate(c);
    auto assignments = state.metadata.value("profileAssignments").toArray();
    if (assign) {
        const auto old = document_color(state);
        if (old.profile.colorModel() != QColorSpace::ColorModel::Rgb ||
            c.profile.colorModel() != QColorSpace::ColorModel::Rgb || old.mode != c.mode)
            throw EditError(
                QCoreApplication::translate("Huelith", "指定 ICC 只用于相同 RGB/灰度/Lab 模式；跨模式请使用转换"));
        if (assignments.size() >= 16)
            throw EditError(QCoreApplication::translate("Huelith", "此工程已连续指定 16 次 ICC，请撤销不需要的指定"));
        assignments.append(
            QJsonObject{{"from", QString::fromLatin1(old.profile.iccProfile().toBase64())},
                        {"to", QString::fromLatin1(c.profile.iccProfile().toBase64())}});
        state.metadata["profileAssignments"] = assignments;
    }
    state.metadata["documentColor"] =
        QJsonObject{{"mode", c.mode},
                    {"bits", c.bits},
                    {"icc", QString::fromLatin1(c.profile.iccProfile().toBase64())}};
}
QImage constrain_document_color(const QImage& source, const State& state) {
    auto result = source;
    const auto spots = state.metadata.value("spotChannels").toObject();
    for (auto it = spots.begin(); it != spots.end(); ++it) {
        const auto mask = state.saved_selections.value(it.key());
        if (mask.isNull())
            continue;
        const auto ink = it.value().toObject();
        const QColor color(ink.value("color").toString());
        const double density = ink.value("solidity").toDouble(-1);
        if (!color.isValid() || density < 0 || density > 1 || mask.size() != source.size())
            throw EditError(QCoreApplication::translate("Huelith", "专色通道数据无效"));
        QImage swatch(1, 1, QImage::Format_RGBA8888);
        swatch.setColorSpace(QColorSpace::SRgb);
        swatch.fill(color);
        const auto converted = to_working(swatch);
        const auto* rgba = reinterpret_cast<const float*>(converted.constBits());
        for (int y = 0; y < result.height(); ++y) {
            auto* row = reinterpret_cast<float*>(result.scanLine(y));
            const auto* coverage = mask.constScanLine(y);
            for (int x = 0; x < result.width(); ++x) {
                const float a = float(coverage[x] / 255. * density);
                auto* p = row + x * 4;
                for (int k = 0; k < 3; ++k)
                    p[k] = p[k] * (1 - a) + rgba[k] * a;
                p[3] = p[3] * (1 - a) + a;
            }
        }
    }
    if (!state.metadata.contains("documentColor"))
        return result;
    const auto c = document_color(state);
    const auto assignments = state.metadata.value("profileAssignments").toArray();
    if (assignments.size() > 16)
        throw EditError(QCoreApplication::translate("Huelith", "ICC 指定记录过多"));
    for (const auto& item : assignments) {
        const auto a = item.toObject();
        const auto from = decode(a.value("from")), to = decode(a.value("to"));
        if (from.colorModel() != QColorSpace::ColorModel::Rgb ||
            to.colorModel() != QColorSpace::ColorModel::Rgb)
            throw EditError(QCoreApplication::translate("Huelith", "ICC 指定记录的颜色模型无效"));
        result = result.convertedToColorSpace(from, QImage::Format_RGBA32FPx4);
        result.setColorSpace(to);
        result = to_working(result);
    }
    if (c.mode == 1 || c.mode == 2) {
        result = result.copy();
        const double levels = c.bits == 8 ? 255 : 65535;
        for (int y = 0; y < result.height(); ++y) {
            auto* row = reinterpret_cast<float*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                auto* p = row + x * 4;
                if (p[3] <= 0)
                    continue;
                Vector rgb{p[0] / p[3], p[1] / p[3], p[2] / p[3]};
                if (c.mode == 1) {
                    const double luminance =
                        .262700212 * rgb[0] + .6779980715 * rgb[1] + .0593017165 * rgb[2];
                    rgb = {luminance, luminance, luminance};
                } else if (c.bits != 32) {
                    auto lab = working_to_lab(rgb);
                    lab[0] = std::round(std::clamp(lab[0] / 100., 0., 1.) * levels) / levels * 100;
                    for (int k = 1; k < 3; ++k)
                        lab[k] = std::round(std::clamp((lab[k] + 128) / 255., 0., 1.) * levels) /
                                     levels * 255 -
                                 128;
                    rgb = lab_to_working(lab);
                }
                for (int k = 0; k < 3; ++k)
                    p[k] = float(rgb[k] * p[3]);
            }
        }
    }
    if (c.mode == 3)
        return roundtrip(result, c.profile, QImage::Format_CMYK8888);
    if (c.mode != 2 && c.bits != 32)
        return roundtrip(result, c.profile,
                         c.bits == 8 ? QImage::Format_RGBA8888 : QImage::Format_RGBA64);
    return result;
}
QImage proof_image(const QImage& source, const QColorSpace& proof, const QColorSpace& monitor,
                   bool gamut, QColor warning) {
    if (!proof.isValid() || !proof.isValidTarget())
        return display_image(source, monitor);
    auto simulated =
        roundtrip(source, proof,
                  proof.colorModel() == QColorSpace::ColorModel::Cmyk ? QImage::Format_CMYK8888
                                                                      : QImage::Format_RGBA64);
    auto display = display_image(simulated, monitor);
    if (gamut)
        for (int y = 0; y < source.height(); ++y) {
            const auto* a = reinterpret_cast<const float*>(source.constScanLine(y));
            const auto* b = reinterpret_cast<const float*>(simulated.constScanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                if (a[x * 4 + 3] < .01)
                    continue;
                const auto la =
                    working_to_lab({a[x * 4] / a[x * 4 + 3], a[x * 4 + 1] / a[x * 4 + 3],
                                    a[x * 4 + 2] / a[x * 4 + 3]});
                const auto lb =
                    working_to_lab({b[x * 4] / b[x * 4 + 3], b[x * 4 + 1] / b[x * 4 + 3],
                                    b[x * 4 + 2] / b[x * 4 + 3]});
                double delta = 0;
                for (int k = 0; k < 3; ++k)
                    delta += (la[k] - lb[k]) * (la[k] - lb[k]);
                if (delta > 4) {
                    warning.setAlphaF(a[x * 4 + 3]);
                    display.setPixelColor(x, y, warning);
                }
            }
        }
    return display;
}
} // namespace huelith::editing
