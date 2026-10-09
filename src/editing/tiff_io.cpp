#include "huelith/editing/tiff_io.h"
#include <QFile>
#include <QSaveFile>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <tiffio.h>

namespace huelith::editing {
namespace {
using Tiff = std::unique_ptr<TIFF, decltype(&TIFFClose)>;
TIFF* open_device(QIODevice& device, const char* mode) {
    return TIFFClientOpen(
        "Huelith TIFF", mode, &device,
        [](thandle_t h, void* data, tmsize_t size) -> tmsize_t {
            return static_cast<QIODevice*>(h)->read(static_cast<char*>(data), size);
        },
        [](thandle_t h, void* data, tmsize_t size) -> tmsize_t {
            return static_cast<QIODevice*>(h)->write(static_cast<const char*>(data), size);
        },
        [](thandle_t h, toff_t offset, int origin) -> toff_t {
            auto* file = static_cast<QIODevice*>(h);
            const qint64 target = static_cast<qint64>(offset) + (origin == SEEK_CUR   ? file->pos()
                                                                 : origin == SEEK_END ? file->size()
                                                                                      : 0);
            return target >= 0 && file->seek(target) ? toff_t(target) : toff_t(-1);
        },
        [](thandle_t) { return 0; },
        [](thandle_t h) -> toff_t { return toff_t(static_cast<QIODevice*>(h)->size()); },
        [](thandle_t, void**, toff_t*) { return 0; }, [](thandle_t, void*, toff_t) {});
}
void check(int status) {
    if (status != 1)
        throw EditError(QStringLiteral("TIFF 数据读写失败"));
}
} // namespace
QImage read_tiff(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw EditError(file.errorString());
    Tiff handle(open_device(file, "r"), TIFFClose);
    if (!handle)
        throw EditError(QStringLiteral("TIFF 头无效"));
    auto* tiff = handle.get();
    uint32_t width{}, height{};
    uint16_t bits{}, samples{}, planar{}, photometric{}, orientation{}, format{};
    check(TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width));
    check(TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height));
    TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &planar);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION, &orientation);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &format);
    check(TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric));
    if (!width || !height || uint64_t(width) * height > 64'000'000 || (bits != 8 && bits != 16) ||
        (samples != 3 && samples != 4) || planar != PLANARCONFIG_CONTIG ||
        photometric != PHOTOMETRIC_RGB || format != SAMPLEFORMAT_UINT || orientation < 1 ||
        orientation > 8 || TIFFIsTiled(tiff))
        throw EditError(QStringLiteral("当前支持条带式 RGB/RGBA 8/16 位 TIFF；此文件布局不受支持"));
    uint16_t extra_count{}, *extra{};
    TIFFGetFieldDefaulted(tiff, TIFFTAG_EXTRASAMPLES, &extra_count, &extra);
    if (samples == 4 &&
        (extra_count != 1 || !extra ||
         (extra[0] != EXTRASAMPLE_ASSOCALPHA && extra[0] != EXTRASAMPLE_UNASSALPHA)))
        throw EditError(QStringLiteral("TIFF 透明度语义不明确"));
    const bool associated = samples == 4 && extra[0] == EXTRASAMPLE_ASSOCALPHA;
    QImage image(int(width), int(height),
                 associated ? QImage::Format_RGBA64_Premultiplied : QImage::Format_RGBA64);
    if (image.isNull())
        throw EditError(QStringLiteral("TIFF 内存不足"));
    const auto line_size = TIFFScanlineSize64(tiff);
    const auto expected = uint64_t(width) * samples * (bits / 8);
    if (line_size != expected)
        throw EditError(QStringLiteral("TIFF 行数据尺寸无效"));
    std::vector<uint16_t> line((std::size_t(line_size) + 1) / 2);
    for (uint32_t y = 0; y < height; ++y) {
        check(TIFFReadScanline(tiff, line.data(), y));
        auto* output = reinterpret_cast<QRgba64*>(image.scanLine(int(y)));
        for (uint32_t x = 0; x < width; ++x) {
            std::array<quint16, 4> value{0, 0, 0, 65535};
            for (int c = 0; c < samples; ++c) {
                const auto index = std::size_t(x) * samples + c;
                value[c] = bits == 16
                               ? line[index]
                               : quint16(reinterpret_cast<const uchar*>(line.data())[index] * 257);
            }
            output[x] = QRgba64::fromRgba64(value[0], value[1], value[2], value[3]);
        }
    }
    uint32_t profile_length{};
    void* profile{};
    if (TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &profile_length, &profile)) {
        if (profile_length > 16 * 1024 * 1024)
            throw EditError(QStringLiteral("TIFF ICC 配置过大"));
        const auto space = QColorSpace::fromIccProfile(
            QByteArray(static_cast<const char*>(profile), profile_length));
        if (!space.isValid())
            throw EditError(QStringLiteral("TIFF ICC 配置无效"));
        image.setColorSpace(space);
    } else {
        image.setColorSpace(QColorSpace::SRgb);
    }
    if (orientation == 2)
        image = image.flipped(Qt::Horizontal);
    if (orientation == 3)
        image = image.transformed(QTransform().rotate(180));
    if (orientation == 4)
        image = image.flipped(Qt::Vertical);
    if (orientation == 5)
        image = image.transformed(QTransform(0, 1, 1, 0, 0, 0));
    if (orientation == 6)
        image = image.transformed(QTransform().rotate(90));
    if (orientation == 7)
        image = image.transformed(QTransform(0, -1, -1, 0, 0, 0));
    if (orientation == 8)
        image = image.transformed(QTransform().rotate(270));
    return to_working(image);
}
void write_tiff(const QImage& source, const QString& path, int bits,
                const std::atomic_bool& cancelled, double ppi) {
    if (source.isNull() || (bits != 8 && bits != 16) || !source.colorSpace().isValid() ||
        !std::isfinite(ppi) || ppi <= 0 || ppi > 100000)
        throw EditError(QStringLiteral("TIFF 输出参数无效"));
    const auto image =
        source.convertedTo(bits == 16 ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw EditError(file.errorString());
    Tiff handle(open_device(file, "w"), TIFFClose);
    if (!handle)
        throw EditError(QStringLiteral("无法创建 TIFF"));
    auto* tiff = handle.get();
    check(TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, uint32_t(image.width())));
    check(TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, uint32_t(image.height())));
    check(TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, bits));
    check(TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 4));
    check(TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB));
    check(TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
    check(TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE));
    check(TIFFSetField(tiff, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL));
    check(TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tiff, 0)));
    check(TIFFSetField(tiff, TIFFTAG_XRESOLUTION, ppi));
    check(TIFFSetField(tiff, TIFFTAG_YRESOLUTION, ppi));
    check(TIFFSetField(tiff, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH));
    uint16_t extra = EXTRASAMPLE_UNASSALPHA;
    check(TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES, 1, &extra));
    const auto profile = image.colorSpace().iccProfile();
    check(TIFFSetField(tiff, TIFFTAG_ICCPROFILE, uint32_t(profile.size()), profile.constData()));
    for (int y = 0; y < image.height(); ++y) {
        if (cancelled.load())
            throw EditError(QStringLiteral("TIFF 导出已取消"));
        QByteArray row(reinterpret_cast<const char*>(image.constScanLine(y)), image.bytesPerLine());
        check(TIFFWriteScanline(tiff, row.data(), uint32_t(y)));
    }
    check(TIFFWriteDirectory(tiff));
    handle.reset();
    if (cancelled.load())
        throw EditError(QStringLiteral("TIFF 导出已取消"));
    if (!file.commit())
        throw EditError(file.errorString());
}
} // namespace huelith::editing
