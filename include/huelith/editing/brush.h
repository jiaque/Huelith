#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <cstdint>
#include <vector>
namespace huelith::editing {
struct BrushSettings {
    double diameter{40}, hardness{.8}, opacity{1}, flow{1}, spacing{.1};
    double angle{}, roundness{1}, size_jitter{}, minimum_size{.05}, angle_jitter{},
        roundness_jitter{};
    double scatter{}, count_jitter{}, flow_jitter{}, texture_depth{.5}, texture_scale{1};
    double smoothing{};
    double dual_scale{.6}, dual_angle{}, dual_depth{1};
    int tip{}, count{1}, texture{};
    int dual_tip{2};
    bool dual_enabled{}, tilt_angle{}, tilt_roundness{};
    bool mixer_enabled{};
    double mixer_wet{.5}, mixer_load{.5};
    bool flip_x{}, flip_y{}, scatter_both{}, pressure_size{true}, pressure_flow{},
        direction_angle{};
    bool pulled_string{}, catch_up{true}, catch_up_end{}, adjust_zoom{true}, airbrush{};
    QImage custom_tip;
};
void validate_brush(const BrushSettings& settings);
[[nodiscard]] QJsonObject brush_json(const BrushSettings& settings);
[[nodiscard]] BrushSettings brush_from_json(const QJsonObject& json);
[[nodiscard]] std::vector<std::pair<QString, BrushSettings>> builtin_brushes();
// Sampled 8-bit ABR v1/v2 tips, raw or PackBits. Unsupported entries fail atomically.
[[nodiscard]] std::vector<std::pair<QString, BrushSettings>> import_abr(const QByteArray& bytes);
[[nodiscard]] QImage brush_tip(const BrushSettings& settings, int edge = 256);
class BrushStroke final {
  public:
    void reset(const BrushSettings& settings, std::uint32_t seed = 1, double zoom = 1);
    void set_mixer_source(QImage source, QColor pigment);
    void add(QImage& coverage, QPointF position, double pressure = 1, QPointF tilt = {});
    void idle(QImage& coverage, double seconds);
    void finish(QImage& coverage);
    [[nodiscard]] QPointF position() const {
        return previous_;
    }
    [[nodiscard]] int stamps() const {
        return stamps_;
    }

  private:
    BrushSettings settings_;
    QImage tip_;
    QImage mixer_source_;
    QColor pigment_{Qt::black}, loaded_color_{Qt::black};
    QPointF previous_;
    QPointF input_, filtered_;
    double smooth_radius_{}, air_time_{}, input_pressure_{1};
    double previous_pressure_{1}, until_next_{};
    QPointF tilt_;
    bool started_{};
    std::uint32_t random_{1};
    int stamps_{};
    double uniform();
    void segment(QImage& coverage, QPointF position, double pressure);
    void stamp(QImage& coverage, QPointF position, double pressure, double direction);
};
} // namespace huelith::editing
