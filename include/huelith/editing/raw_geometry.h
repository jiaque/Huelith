#pragma once
#include <QImage>
#include <QJsonObject>
#include <QTransform>
#include <atomic>
namespace huelith::editing {
struct RawGeometry {
    bool enabled{true}, constrain_crop{}, flip_horizontal{}, flip_vertical{};
    double vertical{}, horizontal{}, rotation{}, aspect{}, scale{100}, offset_x{}, offset_y{};
    bool operator==(const RawGeometry&) const = default;
};
void validate_geometry(const RawGeometry& settings);
[[nodiscard]] QJsonObject geometry_json(const RawGeometry& settings);
[[nodiscard]] RawGeometry geometry_from_json(const QJsonObject& json);
[[nodiscard]] QTransform geometry_transform(QSize size, const RawGeometry& settings);
[[nodiscard]] QRect geometry_crop_rect(QSize size, const RawGeometry& settings);
[[nodiscard]] QImage apply_geometry(const QImage& source, const RawGeometry& settings,
                                    const std::atomic_bool* cancelled = nullptr);
} // namespace huelith::editing
