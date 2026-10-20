#include "huelith/editing/design.h"
#include "huelith/editing/project.h"
#include <QAbstractTextDocumentLayout>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTextDocument>
#include <QTextOption>
#include <cmath>
#include <numbers>

namespace huelith::editing {
namespace {
bool valid_size(QSizeF size) {
    return std::isfinite(size.width()) && std::isfinite(size.height()) && size.width() > 0 &&
           size.height() > 0 && size.width() <= 100000 && size.height() <= 100000;
}
double number(const QJsonObject& object, const char* key, double fallback) {
    if (!object.contains(key))
        return fallback;
    if (!object[key].isDouble())
        throw EditError(QStringLiteral("设计参数必须为数值"));
    return object[key].toDouble();
}
} // namespace
void validate_text(const TextContent& value) {
    if (!valid_size(value.box) || value.html.size() > 65536 ||
        value.html.contains("<img", Qt::CaseInsensitive) ||
        value.html.contains("<object", Qt::CaseInsensitive) ||
        value.html.contains("<table", Qt::CaseInsensitive))
        throw EditError(QStringLiteral("文字内容或文本框无效；文字图层不支持嵌入图片或表格"));
}
void configure_text_document(QTextDocument& document, const TextContent& value) {
    validate_text(value);
    document.setDocumentMargin(0);
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPointSizeF(24);
    document.setDefaultFont(font);
    auto option = document.defaultTextOption();
    option.setWrapMode(value.paragraph ? QTextOption::WrapAtWordBoundaryOrAnywhere
                                       : QTextOption::NoWrap);
    document.setDefaultTextOption(option);
    document.setHtml(value.html);
    document.setTextWidth(value.paragraph ? value.box.width() : -1);
    document.adjustSize();
    if (value.paragraph)
        document.setTextWidth(value.box.width());
}
void validate_shape(const ShapeContent& value) {
    if (value.kind < 0 || value.kind > 4 || !valid_size(value.size) ||
        !std::isfinite(value.radius) || value.radius < 0 || value.radius > 50000 ||
        value.sides < 3 || value.sides > 100 || !std::isfinite(value.star_ratio) ||
        value.star_ratio < .01 || value.star_ratio > 1 || !value.fill.isValid() ||
        !value.fill_end.isValid() || !value.stroke.isValid() ||
        !std::isfinite(value.stroke_width) || value.stroke_width < 0 || value.stroke_width > 1000 ||
        value.stroke_position < 0 || value.stroke_position > 2 || value.stroke_style < 0 ||
        value.stroke_style > 2 || !std::isfinite(value.gradient_angle) ||
        std::abs(value.gradient_angle) > 360 || value.path.elementCount() > 10000)
        throw EditError(QStringLiteral("形状参数无效"));
    for (int i = 0; i < value.path.elementCount(); ++i) {
        const auto point = value.path.elementAt(i);
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 100000 ||
            std::abs(point.y) > 100000)
            throw EditError(QStringLiteral("路径坐标无效"));
    }
}
QPainterPath shape_path(const ShapeContent& value) {
    validate_shape(value);
    QPainterPath path;
    const QRectF box(QPointF(), value.size);
    switch (value.kind) {
    case 0:
        path.addRoundedRect(box, value.radius, value.radius);
        break;
    case 1:
        path.addEllipse(box);
        break;
    case 2: {
        const int count = value.sides * (value.star_ratio < 1 ? 2 : 1);
        for (int i = 0; i < count; ++i) {
            const double angle = 2 * std::numbers::pi * i / count - std::numbers::pi / 2;
            const double radius = value.star_ratio < 1 && i % 2 ? value.star_ratio : 1;
            const QPointF point(box.center().x() + std::cos(angle) * box.width() * radius / 2,
                                box.center().y() + std::sin(angle) * box.height() * radius / 2);
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        path.closeSubpath();
        break;
    }
    case 3:
        path.moveTo(0, 0);
        path.lineTo(value.size.width(), value.size.height());
        break;
    case 4:
        path = value.path;
        break;
    }
    return path;
}
QRectF design_bounds(const Layer& layer) {
    if (layer.kind == LayerKind::text) {
        QTextDocument document;
        configure_text_document(document, layer.text);
        return QRectF(QPointF(), layer.text.paragraph ? layer.text.box : document.size());
    }
    const double margin = layer.shape.stroke_enabled ? layer.shape.stroke_width : 0;
    return shape_path(layer.shape).boundingRect().adjusted(-margin, -margin, margin, margin);
}
void paint_design(QPainter& painter, const Layer& layer) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    if (layer.kind == LayerKind::text) {
        QTextDocument document;
        configure_text_document(document, layer.text);
        if (layer.text.paragraph)
            painter.setClipRect(QRectF(QPointF(), layer.text.box), Qt::IntersectClip);
        document.drawContents(&painter);
    } else {
        const auto& shape = layer.shape;
        const auto path = shape_path(shape);
        QBrush fill(shape.fill);
        if (shape.gradient) {
            const auto box = path.boundingRect();
            const double angle = shape.gradient_angle * std::numbers::pi / 180;
            const QPointF vector(std::cos(angle) * box.width() / 2,
                                 -std::sin(angle) * box.height() / 2);
            QLinearGradient gradient(box.center() - vector, box.center() + vector);
            gradient.setColorAt(0, shape.fill);
            gradient.setColorAt(1, shape.fill_end);
            fill = QBrush(gradient);
        }
        if (shape.fill_enabled && shape.kind != 3)
            painter.fillPath(path, fill);
        if (shape.stroke_enabled && shape.stroke_width > 0) {
            QPen pen(shape.stroke, shape.stroke_width,
                     shape.stroke_style == 1   ? Qt::DashLine
                     : shape.stroke_style == 2 ? Qt::DotLine
                                               : Qt::SolidLine,
                     Qt::RoundCap, Qt::MiterJoin);
            if (shape.stroke_position != 0 && shape.kind != 3) {
                QPainterPathStroker stroker;
                stroker.setWidth(shape.stroke_width * 2);
                stroker.setDashPattern(pen.style());
                auto outline = stroker.createStroke(path);
                outline = shape.stroke_position == 1 ? outline.intersected(path)
                                                     : outline.subtracted(path);
                painter.fillPath(outline, shape.stroke);
            } else {
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
            }
        }
    }
    painter.restore();
}
QJsonObject text_json(const TextContent& value) {
    validate_text(value);
    return {{"version", 1},
            {"html", value.html},
            {"width", value.box.width()},
            {"height", value.box.height()},
            {"paragraph", value.paragraph}};
}
TextContent text_from_json(const QJsonObject& json) {
    if (json.value("version").toInt() != 1 || !json.value("html").isString())
        throw EditError(QStringLiteral("不支持的文字版本"));
    TextContent value{json.value("html").toString(),
                      QSizeF(number(json, "width", 320), number(json, "height", 160)),
                      json.value("paragraph").toBool()};
    validate_text(value);
    return value;
}
QJsonObject shape_json(const ShapeContent& value) {
    validate_shape(value);
    QJsonArray path;
    for (int i = 0; i < value.path.elementCount(); ++i) {
        const auto point = value.path.elementAt(i);
        path.append(QJsonArray{int(point.type), point.x, point.y});
    }
    return {{"version", 1},
            {"kind", value.kind},
            {"width", value.size.width()},
            {"height", value.size.height()},
            {"radius", value.radius},
            {"sides", value.sides},
            {"starRatio", value.star_ratio},
            {"path", path},
            {"fillEnabled", value.fill_enabled},
            {"fill", value.fill.name(QColor::HexArgb)},
            {"gradient", value.gradient},
            {"fillEnd", value.fill_end.name(QColor::HexArgb)},
            {"angle", value.gradient_angle},
            {"strokeEnabled", value.stroke_enabled},
            {"stroke", value.stroke.name(QColor::HexArgb)},
            {"strokeWidth", value.stroke_width},
            {"strokePosition", value.stroke_position},
            {"strokeStyle", value.stroke_style}};
}
ShapeContent shape_from_json(const QJsonObject& json) {
    if (json.value("version").toInt() != 1)
        throw EditError(QStringLiteral("不支持的形状版本"));
    ShapeContent value;
    value.kind = int(number(json, "kind", 0));
    value.size = QSizeF(number(json, "width", 160), number(json, "height", 100));
    value.radius = number(json, "radius", 0);
    value.sides = int(number(json, "sides", 5));
    value.star_ratio = number(json, "starRatio", 1);
    value.fill_enabled = json.value("fillEnabled").toBool(true);
    value.fill = QColor(json.value("fill").toString("#ff000000"));
    value.gradient = json.value("gradient").toBool();
    value.fill_end = QColor(json.value("fillEnd").toString("#ffffffff"));
    value.gradient_angle = number(json, "angle", 0);
    value.stroke_enabled = json.value("strokeEnabled").toBool();
    value.stroke = QColor(json.value("stroke").toString("#ff000000"));
    value.stroke_width = number(json, "strokeWidth", 1);
    value.stroke_position = int(number(json, "strokePosition", 0));
    value.stroke_style = int(number(json, "strokeStyle", 0));
    const auto path = json.value("path").toArray();
    if (path.size() > 10000)
        throw EditError(QStringLiteral("路径节点超限"));
    auto point = [&](qsizetype i) {
        if (i >= path.size() || !path[i].isArray() || path[i].toArray().size() != 3)
            throw EditError(QStringLiteral("路径节点无效"));
        const auto entry = path[i].toArray();
        if (!entry[1].isDouble() || !entry[2].isDouble())
            throw EditError(QStringLiteral("路径坐标必须为数值"));
        return QPointF(entry[1].toDouble(), entry[2].toDouble());
    };
    for (qsizetype i = 0; i < path.size(); ++i) {
        const auto p = point(i);
        const int type = path[i].toArray()[0].toInt(-1);
        if (type == 0)
            value.path.moveTo(p);
        else if (type == 1 && i > 0)
            value.path.lineTo(p);
        else if (type == 2 && i > 0) {
            const auto second = point(i + 1), end = point(i + 2);
            if (path[i + 1].toArray()[0].toInt(-1) != 3 || path[i + 2].toArray()[0].toInt(-1) != 3)
                throw EditError(QStringLiteral("贝塞尔节点无效"));
            value.path.cubicTo(p, second, end);
            i += 2;
        } else
            throw EditError(QStringLiteral("未知路径节点"));
    }
    validate_shape(value);
    return value;
}
} // namespace huelith::editing
