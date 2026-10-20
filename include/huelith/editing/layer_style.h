#pragma once
#include "huelith/editing/blend.h"
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <array>
#include <vector>
namespace huelith::editing {
enum class StyleKind { stroke, drop_shadow, inner_shadow, outer_glow, inner_glow, color_overlay };
struct LayerStyle {
    bool enabled{};
    QColor color{Qt::black};
    double opacity{.75};
    Blend blend{Blend::normal};
    double size{5};
    double angle{120};
    double distance{5};
    double spread{};
    int position{}; // Stroke: outside, inside, center. Inner glow: edge, center.
    bool global_light{true};
};
struct LayerStyles {
    bool enabled{true};
    double global_angle{120};
    std::array<LayerStyle, 6> entries{};
    LayerStyles();
    [[nodiscard]] bool active() const;
};
void validate_styles(const LayerStyles& styles);
[[nodiscard]] QJsonObject styles_json(const LayerStyles& styles);
[[nodiscard]] LayerStyles styles_from_json(const QJsonObject& json);
// Coverage is normalized FP32, including the original edge alpha for interior styles.
[[nodiscard]] std::vector<float> style_coverage(const QImage& source, StyleKind kind,
                                                const LayerStyle& style, double global_angle,
                                                double pixel_scale = 1);
} // namespace huelith::editing
