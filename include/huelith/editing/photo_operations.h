#pragma once
#include "huelith/editing/project.h"

namespace huelith::editing {
enum class Retouch { clone, heal, spot, blur, sharpen, dodge, burn, sponge };
enum class SampleLayers { current, current_below, visible };
struct RetouchOptions {
    Retouch operation{Retouch::clone};
    SampleLayers sample{SampleLayers::current_below};
    QPointF source_offset;
    double radius{10};
    double strength{1};
};
[[nodiscard]] QImage sample_layers(const State& state, const QUuid& target, SampleLayers scope);
[[nodiscard]] QImage color_range(const QImage& image, QPoint seed, int tolerance, bool contiguous);
void fill_color(State& state, const QUuid& target, QPoint seed, QColor color, int tolerance,
                bool contiguous, bool all_layers, double opacity = 1);
void retouch(State& state, const QUuid& target, const QImage& coverage,
             const RetouchOptions& options);
void crop_canvas(State& state, const QRect& bounds);
} // namespace huelith::editing
