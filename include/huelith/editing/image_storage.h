#pragma once
#include <QImage>
#include <QColorSpace>

namespace huelith::editing {
// A format/coordinate guard, independent of the resident-memory working set.
inline constexpr qint64 maximum_image_pixels = 512'000'000;
inline constexpr qint64 mapped_image_threshold = 64LL * 1024 * 1024;
QImage allocate_image(QSize size, QImage::Format format);
inline QImage allocate_image(int w, int h, QImage::Format f) { return allocate_image(QSize(w,h),f); }
QImage copy_image(const QImage& source);
void persist_image(QImage& image);
QImage convert_image(const QImage& source, const QColorSpace& space, QImage::Format format);
}
