#include "huelith/editing/raw_adjustments.h"
#include "huelith/editing/project.h"
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
QImage raw_local_mask(QSize size, const QJsonObject& local) {
    if (size.isEmpty() || qint64(size.width()) * size.height() > 64'000'000)
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
        const auto mode = component.value("mode").toString("brush");
        const auto a = point(component, size, "x", "y"), b = point(component, size, "endX", "endY");
        const double feather = std::clamp(number(component, "feather", .5), 0., 1.);
        painter.setCompositionMode(component.value("subtract").toBool()
                                       ? QPainter::CompositionMode_DestinationOut
                                       : QPainter::CompositionMode_SourceOver);
        painter.setPen(Qt::NoPen);
        if (mode == "linear") {
            if (QLineF(a, b).length() < .5)
                continue;
            QLinearGradient gradient(a, b);
            gradient.setColorAt(0, Qt::white);
            gradient.setColorAt(1, Qt::transparent);
            painter.fillRect(mask.rect(), gradient);
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
                    gradient.setColorAt(0, Qt::white);
                    gradient.setColorAt(std::min(.999, 1 - feather), Qt::white);
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
    const auto object = recipe.value("crop").toObject();
    if (object.isEmpty())
        return QRect(QPoint(), size);
    const auto a = point(object, size, "x", "y"), b = point(object, size, "endX", "endY");
    const auto rect = QRectF(a, b).normalized().toAlignedRect().intersected(QRect(QPoint(), size));
    if (rect.isEmpty())
        throw EditError(QStringLiteral("RAW 裁剪范围为空"));
    return rect;
}
QImage apply_raw_adjustments(const QImage& source, const QJsonObject& recipe, bool crop,
                             const std::atomic_bool* cancelled) {
    auto settings = tone_from_json(recipe.value("tone").toObject());
    if (!recipe.contains("tone"))
        settings.exposure = recipe.value("exposure").toDouble();
    auto result = apply_tone(source, settings, cancelled);
    const auto locals = recipe.value("locals").toArray();
    if (locals.size() > 64)
        throw EditError(QStringLiteral("局部调整数量超过 64 个"));
    for (const auto& value : locals) {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("图像处理已取消"));
        const auto local = value.toObject();
        if (!local.value("enabled").toBool(true))
            continue;
        const auto mask = raw_local_mask(result.size(), local);
        const auto adjusted =
            apply_tone(result, tone_from_json(local.value("tone").toObject()), cancelled);
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
    return crop ? result.copy(raw_crop_rect(result.size(), recipe)) : result;
}
} // namespace huelith::editing
