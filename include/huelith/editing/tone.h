#pragma once
#include "huelith/editing/calibration.h"
#include "huelith/editing/detail.h"
#include "huelith/editing/point_color.h"
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <array>
#include <atomic>
#include <vector>
namespace huelith::editing {
struct ToneSettings {
    std::array<bool, 9> sections{true, true, true, true, true, true, true, true, true};
    CalibrationSettings calibration;
    DetailSettings details;
    std::vector<PointColor> point_colors;
    double exposure{};
    double contrast{};
    double highlights{};
    double shadows{};
    double whites{};
    double blacks{};
    double temperature{}; // Relative red/blue balance, not an absolute CCT claim.
    double tint{};
    double saturation{};
    double vibrance{};
    double texture{};
    double clarity{};
    double dehaze{};
    double vignette{};
    double grain{};
    double vignette_style{}; // Highlight priority, color priority, paint overlay.
    double vignette_midpoint{50};
    double vignette_roundness{};
    double vignette_feather{50};
    double vignette_highlights{};
    double grain_size{25};
    double grain_roughness{50};
    bool legacy_film_effects{};
    bool legacy_color_transform{};
    double sharpening{};
    double black_point{};
    double white_point{1};
    double gamma{1};
    std::array<double,4> parametric_curve{}; // Shadows, darks, lights, highlights.
    std::array<double,3> curve_splits{.25,.5,.75};
    bool legacy_curve_interpolation{};
    std::vector<QPointF> curve{{0, 0}, {1, 1}};
    std::array<std::vector<QPointF>, 3> channel_curves{std::vector<QPointF>{{0, 0}, {1, 1}},
                                                       std::vector<QPointF>{{0, 0}, {1, 1}},
                                                       std::vector<QPointF>{{0, 0}, {1, 1}}};
    std::array<double, 4> grading_hue{}; // Shadows, midtones, highlights, global; degrees.
    std::array<double, 4> grading_saturation{};
    std::array<double, 4> grading_luminance{};
    double grading_balance{};
    double grading_blending{50};
    bool monochrome{};
    std::array<double, 8> hue{};
    std::array<double, 8> saturation_bands{};
    std::array<double, 8> luminance_bands{};
    bool operator==(const ToneSettings&) const = default;
};
void validate_tone(const ToneSettings& settings);
[[nodiscard]] double evaluate_point_curve(double value,const std::vector<QPointF>& points,bool legacy=false);
[[nodiscard]] double evaluate_parametric_curve(double value,const ToneSettings& settings);
[[nodiscard]] QJsonObject tone_json(const ToneSettings& settings);
[[nodiscard]] ToneSettings tone_from_json(const QJsonObject& json);
[[nodiscard]] QImage apply_tone(const QImage& image, const ToneSettings& settings,
                                const std::atomic_bool* cancelled = nullptr, double pixel_scale = 1,
                                int detail_preview = 0, QRectF effect_bounds = {});
} // namespace huelith::editing
