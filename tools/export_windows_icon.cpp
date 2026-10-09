#include <QBuffer>
#include <QDataStream>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QSvgRenderer>
#include <array>

// Explicit developer tool: regenerate the multi-resolution ICO from the approved SVG.
int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    if (argc != 3) {
        return 2;
    }
    QSvgRenderer renderer(QString::fromLocal8Bit(argv[1]));
    if (!renderer.isValid()) {
        return 3;
    }
    constexpr std::array<int, 7> sizes{16, 24, 32, 48, 64, 128, 256};
    std::array<QByteArray, sizes.size()> images;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        QImage image(sizes[i], sizes[i], QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        QBuffer buffer(&images[i]);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            return 4;
        }
    }
    QSaveFile output(QString::fromLocal8Bit(argv[2]));
    if (!output.open(QIODevice::WriteOnly)) {
        return 5;
    }
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16{0} << quint16{1} << static_cast<quint16>(sizes.size());
    quint32 offset = 6 + 16 * static_cast<quint32>(sizes.size());
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const auto dimension = static_cast<quint8>(sizes[i] == 256 ? 0 : sizes[i]);
        const auto bytes = static_cast<quint32>(images[i].size());
        stream << dimension << dimension << quint8{0} << quint8{0}
               << quint16{1} << quint16{32} << bytes << offset;
        offset += bytes;
    }
    for (const auto& image : images) {
        if (output.write(image) != image.size()) {
            return 6;
        }
    }
    return stream.status() == QDataStream::Ok && output.commit() ? 0 : 7;
}
