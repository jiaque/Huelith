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
struct Backing {
    QTemporaryFile file{QDir::tempPath()+"/Huelith-pixels-XXXXXX"};
    uchar* pixels{};
    ~Backing() {
        if(pixels) {
            {const std::lock_guard lock(mapping_mutex);mappings.erase(pixels);}
            file.unmap(pixels);
        }
    }
};
void release(void* data) { delete static_cast<Backing*>(data); }
}
QImage allocate_image(QSize size, QImage::Format format) {
    if(size.isEmpty() || size.width()>65536 || size.height()>65536 ||
       qint64(size.width())*size.height()>maximum_image_pixels)
        throw EditError(QStringLiteral("图像超出尺寸范围（最多 5.12 亿像素，单边 65536）"));
    const QImage sample(1,1,format);
    if(sample.isNull()) throw EditError(QStringLiteral("无效像素格式"));
    const qint64 stride=((qint64(size.width())*sample.depth()+31)/32)*4;
    const qint64 length=stride*size.height();
    if(length<mapped_image_threshold) {
        QImage image(size,format);
        if(image.isNull()) throw EditError(QStringLiteral("图像内存不足"));
        return image;
    }
    auto backing=std::make_unique<Backing>();
    const QStorageInfo disk(QDir::tempPath());
    if(disk.isValid() && disk.bytesAvailable()<length+128LL*1024*1024)
        throw EditError(QStringLiteral("临时磁盘空间不足：此像素缓冲需要 %1 MiB").arg(length/(1024*1024)));
    if(!backing->file.open() || !backing->file.resize(length) ||
       !(backing->pixels=backing->file.map(0,length)))
        throw EditError(QStringLiteral("无法创建大图磁盘缓存：%1").arg(backing->file.errorString()));
    QImage result(backing->pixels,size.width(),size.height(),stride,format,release,backing.get());
    if(result.isNull()) throw EditError(QStringLiteral("无法映射图像缓存"));
    {const std::lock_guard lock(mapping_mutex);mappings.insert(backing->pixels);}
    backing.release();
    return result;
}
void persist_image(QImage& image) {
    if(image.sizeInBytes()<mapped_image_threshold)return;
    {const std::lock_guard lock(mapping_mutex);if(mappings.contains(image.constBits()))return;}
    image=copy_image(image);
}
QImage copy_image(const QImage& source) {
    if(source.isNull()) return {};
    auto result=allocate_image(source.size(),source.format());
    for(int y=0;y<source.height();++y)
        std::memcpy(result.scanLine(y),source.constScanLine(y),std::size_t(result.bytesPerLine()));
    result.setColorSpace(source.colorSpace());
    result.setDotsPerMeterX(source.dotsPerMeterX());result.setDotsPerMeterY(source.dotsPerMeterY());
    return result;
}
QImage convert_image(const QImage& source,const QColorSpace& space,QImage::Format format) {
    if(source.isNull()) return {};
    if(source.colorSpace()==space && source.format()==format) return source;
    auto output=allocate_image(source.size(),format);
    // Bounded strips avoid Qt allocating another full floating-point image.
    const int rows=std::max(1,int((8LL*1024*1024)/source.bytesPerLine()));
    for(int y=0;y<source.height();y+=rows) {
        auto part=source.copy(0,y,source.width(),std::min(rows,source.height()-y));
        if(part.format()==QImage::Format_RGBA32FPx4_Premultiplied)
            part=part.convertToFormat(QImage::Format_RGBA32FPx4);
        part=part.convertedToColorSpace(space,format);
        if(part.isNull()) throw EditError(QStringLiteral("分块色彩转换失败"));
        for(int row=0;row<part.height();++row)
            std::memcpy(output.scanLine(y+row),part.constScanLine(row),std::size_t(output.bytesPerLine()));
    }
    output.setColorSpace(space);
    return output;
}
}
