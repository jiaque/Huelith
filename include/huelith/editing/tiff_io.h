#pragma once
#include "huelith/editing/project.h"
#include <atomic>
namespace huelith::editing {
[[nodiscard]] QImage read_tiff(const QString& path);
void write_tiff(const QImage& image, const QString& path, int bits,
                const std::atomic_bool& cancelled, double ppi = 300);
} // namespace huelith::editing
