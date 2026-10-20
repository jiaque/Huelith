#pragma once
#include "huelith/editing/project.h"

namespace huelith::editing {
enum class Retouch { clone, heal, spot, blur, sharpen, dodge, burn, sponge };
enum class SampleLayers { current, current_below, visible };
struct RetouchOptions {
    Retouch operation{Retouch::clone};
    SampleLayers sample{SampleLayers::current_below};
    QPointF source_offset;
    QPointF source_anchor;
    double source_scale_x{1}, source_scale_y{1}, source_rotation{};
    bool source_flip_x{}, source_flip_y{};
    double radius{10};
    double strength{1};
    double color_adaptation{1};
    int tone_range{1}; // Shadows, midtones, highlights.
    bool protect_tones{true};
    bool sponge_saturate{};
    bool sponge_vibrance{true};
};
[[nodiscard]] QPointF clone_sample_position(const RetouchOptions& options, QPointF point);
[[nodiscard]] QImage sample_layers(const State& state, const QUuid& target, SampleLayers scope);
[[nodiscard]] QImage color_range(const QImage& image, QPoint seed, int tolerance, bool contiguous);
void fill_color(State& state, const QUuid& target, QPoint seed, QColor color, int tolerance,
                bool contiguous, bool all_layers, double opacity = 1);
void retouch(State& state, const QUuid& target, const QImage& coverage,
             const RetouchOptions& options, const QImage* sampled_source = nullptr);
void crop_canvas(State& state, const QRect& bounds, double angle = 0, bool delete_pixels = true);
void patch_selection(State& state, const QUuid& target, QPointF drag, bool destination,
                     SampleLayers sample = SampleLayers::current_below,
                     double color_adaptation = 1);
void paint_replacement(State& state,const QUuid& target,const QImage& replacement,const QImage& coverage);
} // namespace huelith::editing
