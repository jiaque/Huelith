#include "huelith/editing/raw_geometry.h"
#include "huelith/editing/project.h"
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace huelith::editing {
void validate_geometry(const RawGeometry& s) {
    for (double v : {s.vertical, s.horizontal, s.aspect, s.offset_x, s.offset_y})
        if (!std::isfinite(v) || std::abs(v) > 100)
            throw EditError(QStringLiteral("几何参数超出范围"));
    if (!std::isfinite(s.rotation) || std::abs(s.rotation) > 180 || !std::isfinite(s.scale) ||
        s.scale < 10 || s.scale > 400)
        throw EditError(QStringLiteral("旋转或缩放参数无效"));
}
QJsonObject geometry_json(const RawGeometry& s) {
    validate_geometry(s);
    return {{"version", 1},
            {"enabled", s.enabled},
            {"constrainCrop", s.constrain_crop},
            {"flipHorizontal", s.flip_horizontal},
            {"flipVertical", s.flip_vertical},
            {"vertical", s.vertical},
            {"horizontal", s.horizontal},
            {"rotation", s.rotation},
            {"aspect", s.aspect},
            {"scale", s.scale},
            {"offsetX", s.offset_x},
            {"offsetY", s.offset_y}};
}
RawGeometry geometry_from_json(const QJsonObject& j) {
    RawGeometry s;
    if (j.isEmpty())
        return s;
    if (j.value("version").toInt() != 1)
        throw EditError(QStringLiteral("未知几何配方版本"));
    const auto keys = geometry_json(s);
    for (auto it = j.begin(); it != j.end(); ++it)
        if (!keys.contains(it.key()))
            throw EditError(QStringLiteral("未知几何参数"));
    for (auto [key, member] : {std::pair{"vertical", &s.vertical},
                               {"horizontal", &s.horizontal},
                               {"rotation", &s.rotation},
                               {"aspect", &s.aspect},
                               {"scale", &s.scale},
                               {"offsetX", &s.offset_x},
                               {"offsetY", &s.offset_y}})
        if (j.contains(QLatin1String(key))) {
            if (!j.value(QLatin1String(key)).isDouble())
                throw EditError(QStringLiteral("几何数值格式无效"));
            *member = j.value(QLatin1String(key)).toDouble();
        }
    for (auto [key, member] : {std::pair{"enabled", &s.enabled},
                               {"constrainCrop", &s.constrain_crop},
                               {"flipHorizontal", &s.flip_horizontal},
                               {"flipVertical", &s.flip_vertical}})
        if (j.contains(QLatin1String(key))) {
            if (!j.value(QLatin1String(key)).isBool())
                throw EditError(QStringLiteral("几何开关格式无效"));
            *member = j.value(QLatin1String(key)).toBool();
        }
    validate_geometry(s);
    return s;
}
QTransform geometry_transform(QSize size, const RawGeometry& s) {
    validate_geometry(s);
    if (!s.enabled)
        return {};
    if (s.vertical == 0 && s.horizontal == 0 && s.rotation == 0 && s.aspect == 0 &&
        s.scale == 100 && s.offset_x == 0 && s.offset_y == 0 && !s.flip_horizontal &&
        !s.flip_vertical)
        return {};
    const double cx = (size.width() - 1) / 2., cy = (size.height() - 1) / 2.,
                 r = std::max(1., std::hypot(cx, cy));
    const QTransform normalize(1 / r, 0, 0, 1 / r, -cx / r, -cy / r);
    const QTransform perspective(1, 0, s.horizontal / 180, 0, 1, s.vertical / 180, 0, 0, 1);
    QTransform affine;
    affine.translate(cx + s.offset_x / 100 * size.width(), cy + s.offset_y / 100 * size.height());
    affine.rotate(s.rotation);
    const double aspect = std::exp2(s.aspect / 100), scale = s.scale / 100;
    affine.scale(r * scale * aspect * (s.flip_horizontal ? -1 : 1),
                 r * scale / aspect * (s.flip_vertical ? -1 : 1));
    return normalize * perspective * affine;
}
QRect geometry_crop_rect(QSize size, const RawGeometry& s) {
    const QRect full(QPoint(), size);
    if (!s.enabled || !s.constrain_crop)
        return full;
    const auto transform = geometry_transform(size, s);
    if (transform.isIdentity())
        return full;
    const QRectF pixels(0, 0, size.width() - 1, size.height() - 1);
    auto polygon = transform.map(QPolygonF(pixels)).intersected(QPolygonF(pixels));
    if (polygon.size() > 1 && polygon.first() == polygon.last())
        polygon.removeLast();
    if (polygon.size() < 3)
        throw EditError(QStringLiteral("几何变换后没有可裁剪的图像区域"));
    QPointF center;
    for (const auto& p : polygon)
        center += p;
    center /= double(polygon.size());
    const auto inverse = transform.inverted();
    double low = 0, high = 1;
    for (int iteration = 0; iteration < 40; ++iteration) {
        const double scale = (low + high) / 2;
        const QRectF rect(
            center - QPointF((size.width() - 1) * scale / 2, (size.height() - 1) * scale / 2),
            QSizeF((size.width() - 1) * scale, (size.height() - 1) * scale));
        bool valid = true;
        for (const auto& p : QPolygonF(rect))
            if (!pixels.contains(p) || !pixels.contains(inverse.map(p)))
                valid = false;
        (valid ? low : high) = scale;
    }
    const int left = int(std::ceil(center.x() - (size.width() - 1) * low / 2));
    const int top = int(std::ceil(center.y() - (size.height() - 1) * low / 2));
    const int right = int(std::floor(center.x() + (size.width() - 1) * low / 2));
    const int bottom = int(std::floor(center.y() + (size.height() - 1) * low / 2));
    const QRect crop(QPoint(left, top), QPoint(right, bottom));
    if (crop.isEmpty())
        throw EditError(QStringLiteral("约束裁剪范围为空"));
    return crop;
}
QImage apply_geometry(const QImage& source, const RawGeometry& s,
                      const std::atomic_bool* cancelled) {
    validate_geometry(s);
    if (cancelled && cancelled->load())
        throw EditError(QStringLiteral("几何变换已取消"));
    const auto transform = geometry_transform(source.size(), s);
    if (transform.isIdentity())
        return source;
    if (source.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("几何变换需要浮点图像"));
    QImage result(source.size(), source.format());
    result.setColorSpace(source.colorSpace());
    result.fill(Qt::transparent);
    const auto inverse = transform.inverted();
    for (int y = 0; y < result.height(); ++y) {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("几何变换已取消"));
        auto* row = reinterpret_cast<float*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const auto p = inverse.map(QPointF(x, y));
            if (p.x() < -1e-7 || p.y() < -1e-7 || p.x() > source.width() - 1 + 1e-7 ||
                p.y() > source.height() - 1 + 1e-7)
                continue;
            const double px = std::clamp(p.x(), 0., double(source.width() - 1)),
                         py = std::clamp(p.y(), 0., double(source.height() - 1));
            const int ix = int(px), iy = int(py), right = std::min(ix + 1, source.width() - 1),
                      bottom = std::min(iy + 1, source.height() - 1);
            const double dx = px - ix, dy = py - iy;
            const auto* a = reinterpret_cast<const float*>(source.constScanLine(iy));
            const auto* b = reinterpret_cast<const float*>(source.constScanLine(bottom));
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] =
                    float((1 - dy) * ((1 - dx) * a[ix * 4 + c] + dx * a[right * 4 + c]) +
                          dy * ((1 - dx) * b[ix * 4 + c] + dx * b[right * 4 + c]));
        }
    }
    return result;
}
} // namespace huelith::editing
