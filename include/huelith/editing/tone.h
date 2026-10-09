#pragma once
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <array>
#include <vector>
namespace huelith::editing {
struct ToneSettings {
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
    double black_point{};
    double white_point{1};
    double gamma{1};
    std::vector<QPointF> curve{{0, 0}, {1, 1}};
    std::array<double, 8> hue{};
    std::array<double, 8> saturation_bands{};
    std::array<double, 8> luminance_bands{};
    bool operator==(const ToneSettings&) const = default;
};
void validate_tone(const ToneSettings& settings);
[[nodiscard]] QJsonObject tone_json(const ToneSettings& settings);
[[nodiscard]] ToneSettings tone_from_json(const QJsonObject& json);
[[nodiscard]] QImage apply_tone(const QImage& image, const ToneSettings& settings);
} // namespace huelith::editing
