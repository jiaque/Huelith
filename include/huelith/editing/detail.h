#pragma once
#include <QImage>
#include <QJsonObject>
#include <atomic>
namespace huelith::editing {
struct DetailSettings {
    double radius{1};
    double detail{25};
    double masking{};
    double luminance{};
    double luminance_detail{50};
    double luminance_contrast{};
    double color{};
    double color_detail{50};
    double color_smoothness{50};
    bool legacy_sharpening{};
    bool operator==(const DetailSettings&) const = default;
};
void validate_detail(const DetailSettings&);
QJsonObject detail_json(const DetailSettings&);
DetailSettings detail_from_json(const QJsonObject&);
// Linear sRGB premultiplied FP32; alpha and out-of-SDR values are preserved.
QImage apply_detail(const QImage&, const DetailSettings&, double amount,
                    const std::atomic_bool* cancelled = nullptr, double pixel_scale = 1,
                    int preview = 0);
} // namespace huelith::editing
