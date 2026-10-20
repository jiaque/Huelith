#pragma once
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
    int tip{}, count{1}, texture{};
    bool flip_x{}, flip_y{}, scatter_both{}, pressure_size{true}, pressure_flow{},
        direction_angle{};
    bool pulled_string{}, catch_up{true}, catch_up_end{}, adjust_zoom{true}, airbrush{};
    QImage custom_tip;
};
void validate_brush(const BrushSettings& settings);
[[nodiscard]] QJsonObject brush_json(const BrushSettings& settings);
[[nodiscard]] BrushSettings brush_from_json(const QJsonObject& json);
[[nodiscard]] std::vector<std::pair<QString, BrushSettings>> builtin_brushes();
[[nodiscard]] QImage brush_tip(const BrushSettings& settings, int edge = 256);
class BrushStroke final {
  public:
    void reset(const BrushSettings& settings, std::uint32_t seed = 1, double zoom = 1);
    void add(QImage& coverage, QPointF position, double pressure = 1);
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
    QPointF previous_;
    QPointF input_, filtered_;
    double smooth_radius_{}, air_time_{}, input_pressure_{1};
    double previous_pressure_{1}, until_next_{};
    bool started_{};
    std::uint32_t random_{1};
    int stamps_{};
    double uniform();
    void segment(QImage& coverage, QPointF position, double pressure);
    void stamp(QImage& coverage, QPointF position, double pressure, double direction);
};
} // namespace huelith::editing
