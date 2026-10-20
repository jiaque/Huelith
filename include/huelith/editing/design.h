#pragma once
#include <QColor>
#include <QJsonObject>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
class QTextDocument;
class QPainter;
namespace huelith::editing {
struct TextContent {
    QString html;
    QSizeF box{320,
               160}; // 96-dpi text layout units; layer transform maps these to document pixels.
    bool paragraph{};
};
struct ShapeContent {
    int kind{}; // Rectangle, ellipse, polygon, line, editable custom path.
    QSizeF size{160, 100};
    double radius{};
    int sides{5};
    double star_ratio{1};
    QPainterPath path;
    bool fill_enabled{true};
    QColor fill{Qt::black};
    bool gradient{};
    QColor fill_end{Qt::white};
    double gradient_angle{};
    bool stroke_enabled{};
    QColor stroke{Qt::black};
    double stroke_width{1};
    int stroke_position{}; // Center, inside, outside.
    int stroke_style{};    // Solid, dash, dot.
};
struct Layer;
void validate_text(const TextContent& value);
void validate_shape(const ShapeContent& value);
void configure_text_document(QTextDocument& document, const TextContent& value);
[[nodiscard]] QPainterPath shape_path(const ShapeContent& value);
[[nodiscard]] QRectF design_bounds(const Layer& layer);
void paint_design(QPainter& painter, const Layer& layer);
[[nodiscard]] QJsonObject text_json(const TextContent& value);
[[nodiscard]] TextContent text_from_json(const QJsonObject& value);
[[nodiscard]] QJsonObject shape_json(const ShapeContent& value);
[[nodiscard]] ShapeContent shape_from_json(const QJsonObject& value);
} // namespace huelith::editing
