#include <QCoreApplication>
#include "huelith/editing/pdf_import.h"
#include "huelith/editing/project.h"
#include <QFile>
#ifdef Q_OS_WIN
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#endif
namespace huelith::editing {
PdfPages import_pdf_pages(const QString& path, const QString& password, int first, int last,
                          int ppi, const std::atomic_bool* cancel) {
    if (ppi < 36 || ppi > 1200 || first < 0 || last < first || last - first > 49)
        throw EditError(QCoreApplication::translate("Huelith", "PDF 页码或分辨率无效；单次最多导入 50 页"));
#ifdef Q_OS_WIN
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    struct Apartment {
        ~Apartment() {
            winrt::uninit_apartment();
        }
    } apartment;
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw EditError(file.errorString());
        if (file.size() > 256 * 1024 * 1024)
            throw EditError(QCoreApplication::translate("Huelith", "PDF 文件超过 256 MiB，请拆分后导入"));
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            throw EditError(file.errorString());
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream input;
        winrt::Windows::Storage::Streams::DataWriter writer(input);
        writer.WriteBytes(winrt::array_view<const uint8_t>(
            reinterpret_cast<const uint8_t*>(bytes.constData()), uint32_t(bytes.size())));
        writer.StoreAsync().get();
        writer.DetachStream();
        input.Seek(0);
        const auto document = winrt::Windows::Data::Pdf::PdfDocument::LoadFromStreamAsync(
                                  input, password.toStdWString())
                                  .get();
        PdfPages result;
        result.count = int(document.PageCount());
        if (first == 0)
            return result;
        if (last > result.count)
            throw EditError(QCoreApplication::translate("Huelith", "页码超出 PDF 范围"));
        qint64 total_pixels{};
        for (int i = first; i <= last; ++i) {
            if (cancel && cancel->load())
                throw EditError(QCoreApplication::translate("Huelith", "PDF 导入已取消"));
            const auto page = document.GetPage(uint32_t(i - 1));
            const auto size = page.Size();
            const int width = std::max(1, qRound(size.Width * ppi / 96.)),
                      height = std::max(1, qRound(size.Height * ppi / 96.));
            total_pixels += qint64(width) * height;
            if (width > 65536 || height > 65536 || qint64(width) * height > 32000000 ||
                total_pixels > 128000000)
                throw EditError(QCoreApplication::translate("Huelith", "PDF 栅格化超过内存预算，请降低分辨率或减少页数"));
            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
            options.DestinationWidth(uint32_t(width));
            options.DestinationHeight(uint32_t(height));
            page.RenderToStreamAsync(stream, options).get();
            if (stream.Size() > 256 * 1024 * 1024)
                throw EditError(QCoreApplication::translate("Huelith", "PDF 页面缓存过大"));
            const auto length = uint32_t(stream.Size());
            winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
            reader.LoadAsync(length).get();
            std::vector<uint8_t> data(length);
            reader.ReadBytes(data);
            auto image = QImage::fromData(data.data(), int(data.size()));
            if (image.isNull())
                throw EditError(QCoreApplication::translate("Huelith", "PDF 页面解码失败"));
            image.setColorSpace(QColorSpace::SRgb);
            result.images.push_back(std::move(image));
            page.Close();
        }
        return result;
    } catch (const winrt::hresult_error& e) {
        throw EditError(QString::fromStdWString(e.message().c_str()));
    }
#else
    (void)path;
    (void)password;
    (void)cancel;
    throw EditError(QCoreApplication::translate("Huelith", "此构建未包含 PDF 渲染后端"));
#endif
}
} // namespace huelith::editing
