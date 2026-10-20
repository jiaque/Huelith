#include "huelith/editing/image_storage.h"
#include "huelith/editing/raw_adjustments.h"
#include "huelith/editing/healing.h"
#include "huelith/editing/optics.h"
#include "huelith/editing/project.h"
#include "huelith/editing/raw_geometry.h"
#include "huelith/editing/white_balance.h"
#include "huelith/editing/raw_profile.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QPainter>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
namespace {
double number(const QJsonObject& object, const char* key, double fallback = 0) {
    const double value = object.value(QLatin1String(key)).toDouble(fallback);
    if (!std::isfinite(value) || value < -4 || value > 4)
        throw EditError(QStringLiteral("局部蒙版坐标无效"));
    return value;
}
QPointF point(const QJsonObject& object, QSize size, const char* x, const char* y) {
    return {number(object, x) * size.width(), number(object, y) * size.height()};
}
} // namespace
QImage raw_local_mask(QSize size, const QJsonObject& local, const QJsonObject& geometry,
                      const QImage& reference) {
    if (size.isEmpty() || qint64(size.width()) * size.height() > huelith::editing::maximum_image_pixels)
        throw EditError(QStringLiteral("局部蒙版尺寸无效"));
    QImage mask(size, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto components = local.value("components").toArray();
    if (components.size() > 4096)
        throw EditError(QStringLiteral("局部蒙版笔画数量超限"));
    for (const auto& value : components) {
        const auto component = value.toObject();
        if (!component.value("enabled").toBool(true))
            continue;
        const auto operation =
            component.value("operation")
                .toString(component.value("subtract").toBool() ? "subtract" : "add");
        if (operation != "add" && operation != "subtract" && operation != "intersect")
            throw EditError(QStringLiteral("未知蒙版组合操作"));
        const double density = number(component, "density", 1);
        if (density < 0 || density > 1)
            throw EditError(QStringLiteral("蒙版密度无效"));
        if (operation == "intersect" || component.value("invert").toBool() || density != 1) {
            auto single = component;
            single.remove("operation");
            single.remove("subtract");
            single.remove("invert");
            single.remove("density");
            auto coverage = raw_local_mask(size, QJsonObject{{"components", QJsonArray{single}}},
                                           geometry, reference);
            if (component.value("invert").toBool())
                coverage.invertPixels();
            QImage layer(size, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < size.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(layer.scanLine(y));
                const auto* input = coverage.constScanLine(y);
                for (int x = 0; x < size.width(); ++x) {
                    const int alpha = qRound(input[x] * density);
                    row[x] = qRgba(alpha, alpha, alpha, alpha);
                }
            }
            painter.save();
            painter.resetTransform();
            painter.setCompositionMode(
                operation == "intersect"  ? QPainter::CompositionMode_DestinationIn
                : operation == "subtract" ? QPainter::CompositionMode_DestinationOut
                                          : QPainter::CompositionMode_SourceOver);
            painter.drawImage(0, 0, layer);
            painter.restore();
            continue;
        }

        QTransform mapping;
        if (component.contains("geometryAtCreation")) {
            const auto original = geometry_transform(
                size, geometry_from_json(component.value("geometryAtCreation").toObject()));
            mapping = original.inverted() * geometry_transform(size, geometry_from_json(geometry));
        }
        painter.setWorldTransform(mapping);
        const auto mode = component.value("mode").toString("brush");
        const auto a = point(component, size, "x", "y"), b = point(component, size, "endX", "endY");
        const double feather = std::clamp(number(component, "feather", .5), 0., 1.);
        painter.setCompositionMode(operation == "subtract"
                                       ? QPainter::CompositionMode_DestinationOut
                                       : QPainter::CompositionMode_SourceOver);
        painter.setPen(Qt::NoPen);
        if (mode == "luminance" || mode == "color") {
            if (reference.isNull() || reference.size() != size)
                throw EditError(QStringLiteral("范围蒙版缺少对应的源预览"));
            const auto encoded = display_image(reference).convertToFormat(QImage::Format_ARGB32);
            const double low = number(component, "low", 0), high = number(component, "high", 1),
                         smooth = number(component, "smooth", .1);
            if (low < 0 || high > 1 || low > high || smooth < 0 || smooth > 1)
                throw EditError(QStringLiteral("明度范围边界无效"));
            const double tolerance = component.value("range").toDouble(50);
            if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 100)
                throw EditError(QStringLiteral("颜色范围无效"));
            const auto colors = component.value("colors").toArray();
            if (mode == "color" && colors.size() > 5)
                throw EditError(QStringLiteral("颜色范围最多5个样本"));
            std::vector<PointColor> samples;
            for (const auto& sample : colors) {
                const auto rgb = sample.toArray();
                if (rgb.size() != 3)
                    throw EditError(QStringLiteral("颜色样本无效"));
                for (const auto& channel : rgb)
                    if (!channel.isDouble() || channel.toDouble() < 0 || channel.toDouble() > 1)
                        throw EditError(QStringLiteral("颜色样本无效"));
                auto color = sample_point_color(
                    QColor::fromRgbF(rgb[0].toDouble(), rgb[1].toDouble(), rgb[2].toDouble()));
                color.range = tolerance;
                samples.push_back(color);
            }
            QImage range(size, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < size.height(); ++y) {
                const auto* input = reinterpret_cast<const QRgb*>(encoded.constScanLine(y));
                auto* row = reinterpret_cast<QRgb*>(range.scanLine(y));
                for (int x = 0; x < size.width(); ++x) {
                    const auto rgb = input[x];
                    const double r = qRed(rgb) / 255., g = qGreen(rgb) / 255.,
                                 blue = qBlue(rgb) / 255.;
                    double weight = 1;
                    if (mode == "luminance") {
                        const double v = .2126 * r + .7152 * g + .0722 * blue;
                        const auto ramp = [](double t) {
                            t = std::clamp(t, 0., 1.);
                            return t * t * (3 - 2 * t);
                        };
                        if (v < low)
                            weight = smooth == 0 ? 0 : ramp(1 - (low - v) / smooth);
                        else if (v > high)
                            weight = smooth == 0 ? 0 : ramp(1 - (v - high) / smooth);
                    } else {
                        weight = 0;
                        const auto hsl = point_color_hsl({r, g, blue});
                        for (const auto& sample : samples)
                            weight = std::max(weight, point_color_weight(hsl, sample));
                    }
                    const int alpha = qAlpha(rgb) == 0 ? 0 : qRound(weight * 255);
                    row[x] = qRgba(alpha, alpha, alpha, alpha);
                }
            }
            painter.save();
            painter.resetTransform();
            painter.drawImage(0, 0, range);
            painter.restore();
        } else if (mode == "bitmap") {
            const auto encoded = component.value("png").toString().toLatin1();
            if (encoded.size() > 2 * 1024 * 1024)
                throw EditError(QStringLiteral("局部蒙版数据过大"));
            auto bytes = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG");
            const auto dimensions = reader.size();
            if (dimensions.isEmpty() || dimensions.width() > 1024 || dimensions.height() > 1024)
                throw EditError(QStringLiteral("局部蒙版图像尺寸无效"));
            auto coverage = reader.read().convertToFormat(QImage::Format_Grayscale8);
            if (coverage.isNull())
                throw EditError(QStringLiteral("局部蒙版图像损坏"));
            QImage alpha(coverage.size(), QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < coverage.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(alpha.scanLine(y));
                for (int x = 0; x < coverage.width(); ++x) {
                    const int opacity = coverage.constScanLine(y)[x];
                    row[x] = qRgba(opacity, opacity, opacity, opacity);
                }
            }
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(mask.rect(), alpha);
        } else if (mode == "linear") {
            if (QLineF(a, b).length() < .5)
                continue;
            QLinearGradient gradient(a, b);
            gradient.setColorAt(0, Qt::white);
            gradient.setColorAt(1, Qt::transparent);
            painter.fillRect(mapping.inverted().mapRect(QRectF(mask.rect())), gradient);
        } else if (mode == "radial") {
            const QRectF rect = QRectF(a, b).normalized();
            if (rect.width() < 1 || rect.height() < 1)
                continue;
            painter.save();
            painter.translate(rect.center());
            painter.scale(rect.width() / 2, rect.height() / 2);
            QRadialGradient gradient(QPointF(), 1);
            gradient.setColorAt(0, Qt::white);
            gradient.setColorAt(std::min(.999, 1 - feather), Qt::white);
            gradient.setColorAt(1, Qt::transparent);
            painter.setBrush(gradient);
            painter.drawEllipse(QPointF(), 1, 1);
            painter.restore();
        } else if (mode == "brush") {
            const double flow = number(component, "flow", 1);
            if (flow < 0 || flow > 1)
                throw EditError(QStringLiteral("笔刷流量无效"));
            const QColor coverage = QColor::fromRgbF(1, 1, 1, flow);
            const double radius = std::clamp(number(component, "radius", .03), .0001, 1.) *
                                  std::min(size.width(), size.height());
            const auto points = component.value("points").toArray();
            if (points.size() > 16384)
                throw EditError(QStringLiteral("局部笔画采样数量超限"));
            std::optional<QPointF> previous;
            for (const auto& sample : points) {
                const auto p = point(sample.toObject(), size, "x", "y");
                const auto start = previous.value_or(p);
                const auto steps = std::max(
                    1, int(std::ceil(QLineF(start, p).length() / std::max(.5, radius * .2))));
                for (int step = 0; step <= steps; ++step) {
                    const auto center = start + (p - start) * (double(step) / steps);
                    QRadialGradient gradient(center, radius);
                    gradient.setColorAt(0, coverage);
                    gradient.setColorAt(std::min(.999, 1 - feather), coverage);
                    gradient.setColorAt(1, Qt::transparent);
                    painter.setBrush(gradient);
                    painter.drawEllipse(center, radius, radius);
                }
                previous = p;
            }
        } else
            throw EditError(QStringLiteral("未知局部蒙版类型"));
    }
    painter.end();
    QImage result(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            result.scanLine(y)[x] = uchar(qAlpha(mask.pixel(x, y)));
    if (local.value("invert").toBool())
        result.invertPixels();
    return result;
}
QRect raw_crop_rect(QSize size, const QJsonObject& recipe) {
    const auto constrained =
        geometry_crop_rect(size, geometry_from_json(recipe.value("geometry").toObject()));
    const auto object = recipe.value("crop").toObject();
    if (object.isEmpty())
        return constrained;
    const auto a = point(object, size, "x", "y"), b = point(object, size, "endX", "endY");
    const auto rect = QRectF(a, b).normalized().toAlignedRect().intersected(constrained);
    if (rect.isEmpty())
        throw EditError(QStringLiteral("RAW 裁剪范围为空"));
    return rect;
}
QImage apply_raw_adjustments(const QImage& source, const QJsonObject& recipe, bool crop,
                             const std::atomic_bool* cancelled, double pixel_scale,
                             int detail_preview, int detail_target) {
    const auto orientation=recipe.value("quarterTurns");
    const int turns=orientation.toInt();
    if(!orientation.isUndefined() && (!orientation.isDouble() || orientation.toDouble()!=turns || turns<0 || turns>3))
        throw EditError(QStringLiteral("RAW 方向参数无效"));
    auto settings = tone_from_json(recipe.value("tone").toObject());
    if (!recipe.contains("tone"))
        settings.exposure = recipe.value("exposure").toDouble();
    const auto balanced =
        settings.sections[1] ? apply_white_balance(source, recipe, cancelled) : source;
    const auto optical =
        apply_optics(apply_raw_profile(balanced, recipe, cancelled), optics_from_json(recipe.value("optics").toObject()), cancelled);
    const auto geometric =
        apply_geometry(apply_healing(optical, recipe.value("healing").toArray(), cancelled),
                       geometry_from_json(recipe.value("geometry").toObject()), cancelled);
    auto result =
        apply_tone(geometric, settings, cancelled, pixel_scale,
                   detail_target < 0 ? detail_preview : 0, raw_crop_rect(geometric.size(), recipe));
    if (detail_preview != 0 && detail_target < 0)
        return result;
    const auto locals = recipe.value("locals").toArray();
    if (locals.size() > 64)
        throw EditError(QStringLiteral("局部调整数量超过 64 个"));
    const QImage range_reference = result;
    for (int index = 0; index < locals.size(); ++index) {
        const auto value = locals[index];
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("图像处理已取消"));
        const auto local = value.toObject();
        if (!local.value("enabled").toBool(true))
            continue;
        const auto mask = raw_local_mask(result.size(), local, recipe.value("geometry").toObject(),
                                         range_reference);
        const auto adjusted = apply_tone(
            result, tone_from_json(local.value("tone").toObject()), cancelled, pixel_scale,
            index == detail_target ? detail_preview : 0, raw_crop_rect(result.size(), recipe));
        if (detail_preview != 0 && index == detail_target)
            return adjusted;
        for (int y = 0; y < result.height(); ++y) {
            auto* target = reinterpret_cast<float*>(result.scanLine(y));
            const auto* input = reinterpret_cast<const float*>(adjusted.constScanLine(y));
            const auto* coverage = mask.constScanLine(y);
            for (int x = 0; x < result.width(); ++x) {
                const float amount = coverage[x] / 255.F;
                for (int c = 0; c < 3; ++c)
                    target[x * 4 + c] += (input[x * 4 + c] - target[x * 4 + c]) * amount;
            }
        }
    }
    if(!crop)return result;
    result=result.copy(raw_crop_rect(result.size(),recipe));
    return turns ? result.transformed(QTransform().rotate(turns*90),Qt::FastTransformation) : result;
}
} // namespace huelith::editing
