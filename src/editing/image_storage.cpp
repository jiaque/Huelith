#include <QCoreApplication>
#include "huelith/editing/image_storage.h"
#include "huelith/editing/project.h"
#include <QDir>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace huelith::editing {
namespace {
std::mutex mapping_mutex;
std::unordered_set<const uchar*> mappings;
QString scratchDirectory;
qint64 residentBytes{}, mappedBytes{}, residentBudget = 2048LL << 20, diskBudget = 32LL << 30;
struct Backing {
    std::unique_ptr<QTemporaryFile> file;
    std::unique_ptr<uchar[]> memory;
    bool onDisk{};
    qint64 length{};
    uchar* pixels{};
    ~Backing() {
        if (file && pixels)
            file->unmap(pixels);
        const std::lock_guard lock(mapping_mutex);
        if (onDisk) {
            if (pixels)
                mappings.erase(pixels);
            mappedBytes -= length;
        } else
            residentBytes -= length;
    }
};
void release(void* data) {
    delete static_cast<Backing*>(data);
}
} // namespace
void configure_pixel_storage(QString directory, qint64 residentLimit, qint64 diskLimit) {
    if (residentLimit < 1 || diskLimit < 1)
        throw EditError(QCoreApplication::translate("Huelith", "像素缓冲预算无效"));
    const std::lock_guard lock(mapping_mutex);
    scratchDirectory = std::move(directory);
    residentBudget = residentLimit;
    diskBudget = diskLimit;
}
PixelStorageUsage pixel_storage_usage() {
    const std::lock_guard lock(mapping_mutex);
    return {residentBytes, mappedBytes};
}
QImage allocate_image(QSize size, QImage::Format format) {
    if (size.isEmpty() || size.width() > 65536 || size.height() > 65536 ||
        qint64(size.width()) * size.height() > maximum_image_pixels)
        throw EditError(QCoreApplication::translate("Huelith", "图像超出尺寸范围（最多 5.12 亿像素，单边 65536）"));
    const QImage sample(1, 1, format);
    if (sample.isNull())
        throw EditError(QCoreApplication::translate("Huelith", "无效像素格式"));
    const qint64 stride = ((qint64(size.width()) * sample.depth() + 31) / 32) * 4;
    const qint64 length = stride * size.height();
    auto backing = std::make_unique<Backing>();
    QString directory;
    {
        const std::lock_guard lock(mapping_mutex);
        backing->onDisk =
            length >= mapped_image_threshold || length > residentBudget - residentBytes;
        if (backing->onDisk && length > diskBudget - mappedBytes)
            throw EditError(QCoreApplication::translate("Huelith", "像素暂存已达容量上限；请关闭其他文档或提高暂存预算"));
        backing->length = length;
        if (backing->onDisk) {
            mappedBytes += length;
            directory = scratchDirectory;
        } else
            residentBytes += length;
    }
    if (backing->onDisk) {
        if (directory.isEmpty())
            directory = QDir::tempPath();
        const QStorageInfo disk(directory);
        if (disk.isReady() && disk.bytesAvailable() >= 0 &&
            disk.bytesAvailable() < length + (128LL << 20))
            throw EditError(QCoreApplication::translate("Huelith", "像素暂存盘可用空间不足"));
        backing->file =
            std::make_unique<QTemporaryFile>(QDir(directory).filePath("Huelith-pixels-XXXXXX"));
        if (!backing->file->open() || !backing->file->resize(length) ||
            !(backing->pixels = backing->file->map(0, length)))
            throw EditError(
                QCoreApplication::translate("Huelith", "无法建立像素暂存：%1").arg(backing->file->errorString()));
    } else {
        backing->memory.reset(new (std::nothrow) uchar[size_t(length)]);
        if (!backing->memory)
            throw EditError(QCoreApplication::translate("Huelith", "图像内存不足"));
        backing->pixels = backing->memory.get();
    }
    QImage result(backing->pixels, size.width(), size.height(), stride, format, release,
                  backing.get());
    if (result.isNull())
        throw EditError(QCoreApplication::translate("Huelith", "无法映射图像缓存"));
    if (backing->onDisk) {
        const std::lock_guard lock(mapping_mutex);
        mappings.insert(backing->pixels);
    }
    backing.release();
    return result;
}
void persist_image(QImage& image) {
    if (image.sizeInBytes() < mapped_image_threshold)
        return;
    {
        const std::lock_guard lock(mapping_mutex);
        if (mappings.contains(image.constBits()))
            return;
    }
    image = copy_image(image);
}
QImage copy_image(const QImage& source) {
    if (source.isNull())
        return {};
    auto result = allocate_image(source.size(), source.format());
    for (int y = 0; y < source.height(); ++y)
        std::memcpy(result.scanLine(y), source.constScanLine(y),
                    std::size_t(result.bytesPerLine()));
    result.setColorSpace(source.colorSpace());
    result.setDotsPerMeterX(source.dotsPerMeterX());
    result.setDotsPerMeterY(source.dotsPerMeterY());
    return result;
}
QImage convert_image(const QImage& source, const QColorSpace& space, QImage::Format format) {
    if (source.isNull())
        return {};
    if (source.colorSpace() == space && source.format() == format)
        return source;
    auto output = allocate_image(source.size(), format);
    // Bounded strips avoid Qt allocating another full floating-point image.
    const int rows = std::max(1, int((8LL * 1024 * 1024) / source.bytesPerLine()));
    for (int y = 0; y < source.height(); y += rows) {
        auto part = source.copy(0, y, source.width(), std::min(rows, source.height() - y));
        if (part.format() == QImage::Format_RGBA32FPx4_Premultiplied)
            part = part.convertToFormat(QImage::Format_RGBA32FPx4);
        part = part.convertedToColorSpace(space, format);
        if (part.isNull())
            throw EditError(QCoreApplication::translate("Huelith", "分块色彩转换失败"));
        for (int row = 0; row < part.height(); ++row)
            std::memcpy(output.scanLine(y + row), part.constScanLine(row),
                        std::size_t(output.bytesPerLine()));
    }
    output.setColorSpace(space);
    return output;
}
} // namespace huelith::editing
