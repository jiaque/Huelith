#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
struct MetadataPolicy {
    bool exif{true}, iptc{true}, xmp{true}, copyright{true}, gps{false};
};
[[nodiscard]] QJsonObject read_photo_metadata(const QString& path,
                                             const std::atomic_bool* cancelled = nullptr);
[[nodiscard]] QByteArray extract_embedded_jpeg(const QByteArray& raw);
void write_photo_metadata(const QString& staged, const QJsonObject& metadata,
                          const MetadataPolicy& policy, QSize size, bool srgb,
                          const std::atomic_bool* cancelled = nullptr);
[[nodiscard]] QString exif_caption(const QJsonObject& metadata);
[[nodiscard]] QString exif_caption_template(const QString& pattern,const QJsonObject& metadata,const QString& name);
[[nodiscard]] State exif_border(const State& source, int margin, int caption_height,
                                int font_pixels, QColor background, QColor text,
                                const QString& caption,const QImage& logo = {});
}
