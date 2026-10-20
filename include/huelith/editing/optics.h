#pragma once
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <array>
#include <atomic>
#include <vector>

namespace huelith::editing {
struct OpticsSettings {
    bool enabled{true};
    bool profile_enabled{};
    bool remove_ca{};
    QString profile_xml; // Embedded calibration snapshot: reopening never depends on a changed DB.
    QString profile_name;
    double crop_factor{1};
    double focal{50}, aperture{5.6}, distance{1000};
    double profile_distortion{100}, profile_vignette{100};
    double distortion{}, vignette{}, midpoint{50};
    double purple{}, green{};
    std::array<double, 2> purple_hue{30, 70}, green_hue{40, 60};
    bool operator==(const OpticsSettings&) const = default;
};
struct LensProfile {
    QString maker, model, xml;
    double crop_factor{1};
    bool distortion{}, vignette{}, ca{};
};
[[nodiscard]] std::vector<LensProfile> lens_profiles(const QString& directory);
[[nodiscard]] int match_lens_profile(const std::vector<LensProfile>& profiles,
                                     const QJsonObject& metadata);
[[nodiscard]] double camera_crop_factor(const QString& directory, const QJsonObject& metadata);
void validate_optics(const OpticsSettings& settings);
[[nodiscard]] QJsonObject optics_json(const OpticsSettings& settings);
[[nodiscard]] OpticsSettings optics_from_json(const QJsonObject& json);
[[nodiscard]] QImage apply_optics(const QImage& source, const OpticsSettings& settings,
                                  const std::atomic_bool* cancelled = nullptr);
} // namespace huelith::editing
