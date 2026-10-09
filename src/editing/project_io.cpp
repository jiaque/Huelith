#include "huelith/editing/project.h"
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>
#include <cstring>
#include <limits>

namespace huelith::editing {
namespace {
constexpr qint64 file_budget = 1024LL * 1024 * 1024;
constexpr quint32 schema = 3;
void checked(QDataStream& stream) {
    if (stream.status() != QDataStream::Ok)
        throw EditError(QStringLiteral("工程截断或数据无效"));
}
void write_image(QDataStream& out, const QImage& image) {
    out << qint32(image.width()) << qint32(image.height()) << qint32(image.format());
    if (image.isNull())
        return;
    const auto bytes = image.sizeInBytes();
    if (bytes > file_budget)
        throw EditError(QStringLiteral("单项资源超过 M1 内存预算"));
    out << QByteArray(reinterpret_cast<const char*>(image.constBits()), bytes)
        << image.colorSpace().iccProfile();
}
QByteArray bounded_bytes(QDataStream& in) {
    quint32 size{};
    in >> size;
    checked(in);
    if (size == 0xffffffffU)
        return {};
    if (size > file_budget || qint64(size) > in.device()->bytesAvailable())
        throw EditError(QStringLiteral("工程资源长度无效"));
    QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (in.readRawData(bytes.data(), static_cast<int>(size)) != static_cast<int>(size))
        throw EditError(QStringLiteral("工程资源读取失败"));
    return bytes;
}
QImage read_image(QDataStream& in) {
    qint32 width{}, height{}, format{};
    in >> width >> height >> format;
    checked(in);
    if (width == 0 && height == 0)
        return {};
    if (width < 1 || height < 1 || qint64(width) * height > 64'000'000 ||
        (format != QImage::Format_RGBA32FPx4_Premultiplied && format != QImage::Format_Grayscale8 &&
         format != QImage::Format_RGBA8888))
        throw EditError(QStringLiteral("不支持的资源尺寸或格式"));
    const auto bytes = bounded_bytes(in);
    const auto icc = bounded_bytes(in);
    QImage result(width, height, static_cast<QImage::Format>(format));
    if (result.isNull() || bytes.size() != result.sizeInBytes())
        throw EditError(QStringLiteral("资源像素长度不匹配"));
    std::memcpy(result.bits(), bytes.constData(), static_cast<std::size_t>(bytes.size()));
    if (!icc.isEmpty())
        result.setColorSpace(QColorSpace::fromIccProfile(icc));
    return result;
}
void write_layers(QDataStream& out, const std::vector<Layer>& layers) {
    out << quint32(layers.size());
    for (const auto& layer : layers) {
        out << layer.id << layer.name << qint32(layer.kind) << layer.transform
            << layer.mask_transform << layer.vector_mask << layer.vector_enabled
            << layer.mask_enabled << layer.mask_linked << layer.visible << layer.locked
            << layer.clipped << layer.isolated << layer.opacity << qint32(layer.blend)
            << layer.exposure << layer.fill << layer.fill_end << layer.gradient << layer.linked_path
            << layer.linked_hash;
        out << layer.warped;
        for (const auto& point : layer.warp_grid)
            out << point;
        write_image(out, layer.pixels);
        write_image(out, layer.mask);
        write_layers(out, layer.children);
    }
}
std::vector<Layer> read_layers(QDataStream& in, int depth, int& count, quint32 version) {
    quint32 size{};
    in >> size;
    checked(in);
    if (depth > 32 || size > 4096 || count + size > 4096)
        throw EditError(QStringLiteral("工程图层数量或深度超限"));
    count += static_cast<int>(size);
    std::vector<Layer> layers;
    for (quint32 i = 0; i < size; ++i) {
        Layer layer;
        qint32 kind{}, blend{};
        in >> layer.id >> layer.name >> kind >> layer.transform >> layer.mask_transform >>
            layer.vector_mask >> layer.vector_enabled >> layer.mask_enabled >> layer.mask_linked >>
            layer.visible >> layer.locked >> layer.clipped >> layer.isolated >> layer.opacity >>
            blend >> layer.exposure >> layer.fill >> layer.fill_end >> layer.gradient >>
            layer.linked_path >> layer.linked_hash;
        checked(in);
        if (kind < 0 || kind > qint32(LayerKind::composition))
            throw EditError(QStringLiteral("工程含未知图层，已拒绝打开并保留原文件"));
        layer.kind = static_cast<LayerKind>(kind);
        layer.blend = static_cast<Blend>(blend);
        if (version >= 3) {
            in >> layer.warped;
            for (auto& point : layer.warp_grid)
                in >> point;
        }
        layer.pixels = read_image(in);
        layer.mask = read_image(in);
        layer.children = read_layers(in, depth + 1, count, version);
        layers.push_back(std::move(layer));
    }
    return layers;
}
void write_images(QDataStream& out, const QMap<QString, QImage>& images) {
    out << quint32(images.size());
    for (auto it = images.begin(); it != images.end(); ++it) {
        out << it.key();
        write_image(out, it.value());
    }
}
QMap<QString, QImage> read_images(QDataStream& in) {
    quint32 count{};
    in >> count;
    if (count > 256)
        throw EditError(QStringLiteral("工程保存范围或快照过多"));
    QMap<QString, QImage> result;
    for (quint32 i = 0; i < count; ++i) {
        QString name;
        in >> name;
        result.insert(name, read_image(in));
    }
    return result;
}
quint32 crc32(const QByteArray& data) {
    quint32 crc = 0xffffffffU;
    for (const auto c : data) {
        crc ^= static_cast<unsigned char>(c);
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
// Stored ZIP entries with ZIP64 end records. M1 retains an explicit 1 GiB
// resource budget; larger projects are rejected before writing.
QByteArray archive(const QMap<QByteArray, QByteArray>& entries) {
    QByteArray result, central;
    QDataStream out(&result, QIODevice::WriteOnly), index(&central, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    index.setByteOrder(QDataStream::LittleEndian);
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const auto offset = quint32(result.size()), size = quint32(it.value().size()),
                   crc = crc32(it.value());
        out << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0) << quint16(0)
            << quint16(0) << crc << size << size << quint16(it.key().size()) << quint16(0);
        out.writeRawData(it.key().data(), static_cast<int>(it.key().size()));
        out.writeRawData(it.value().data(), static_cast<int>(size));
        index << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0) << quint16(0)
              << quint16(0) << quint16(0) << crc << size << size << quint16(it.key().size())
              << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << offset;
        index.writeRawData(it.key().data(), static_cast<int>(it.key().size()));
    }
    const auto start = quint32(result.size());
    out.writeRawData(central.data(), static_cast<int>(central.size()));
    const quint64 zip64_offset = quint64(result.size());
    out << quint32(0x06064b50) << quint64(44) << quint16(45) << quint16(45) << quint32(0)
        << quint32(0) << quint64(entries.size()) << quint64(entries.size())
        << quint64(central.size()) << quint64(start);
    out << quint32(0x07064b50) << quint32(0) << zip64_offset << quint32(1);
    out << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(0xffff) << quint16(0xffff)
        << quint32(0xffffffff) << quint32(0xffffffff) << quint16(0);
    checked(out);
    checked(index);
    return result;
}
QMap<QByteArray, QByteArray> unarchive(const QByteArray& bytes) {
    if (bytes.size() < 98 ||
        qFromLittleEndian<quint32>(bytes.constData() + bytes.size() - 22) != 0x06054b50U ||
        qFromLittleEndian<quint16>(bytes.constData() + bytes.size() - 2) != 0)
        throw EditError(QStringLiteral("工程目录尾部缺失或损坏"));
    QDataStream in(bytes);
    in.setByteOrder(QDataStream::LittleEndian);
    QMap<QByteArray, QByteArray> entries;
    while (!in.atEnd()) {
        quint32 signature{};
        in >> signature;
        if (signature == 0x02014b50U)
            break;
        if (signature != 0x04034b50U || entries.size() >= 8)
            throw EditError(QStringLiteral("无效工程容器"));
        quint16 version{}, flags{}, method{}, time{}, date{}, names{}, extra{};
        quint32 crc{}, packed{}, size{};
        in >> version >> flags >> method >> time >> date >> crc >> packed >> size >> names >> extra;
        checked(in);
        if (flags != 0 || method != 0 || size != packed || size > file_budget || names > 100 ||
            qint64(size) + names + extra > in.device()->bytesAvailable())
            throw EditError(QStringLiteral("工程容器参数超限"));
        QByteArray name(names, Qt::Uninitialized);
        in.readRawData(name.data(), names);
        in.skipRawData(extra);
        QByteArray data(size, Qt::Uninitialized);
        in.readRawData(data.data(), static_cast<int>(size));
        checked(in);
        if (crc32(data) != crc || entries.contains(name))
            throw EditError(QStringLiteral("工程资源校验失败"));
        entries.insert(name, data);
    }
    return entries;
}
} // namespace
void save_project(const Snapshot& snapshot, const QString& path, bool simulate_failure) {
    validate_state(snapshot.state);
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);
    const auto& state = snapshot.state;
    out << schema << state.size << state.name << state.ppi;
    write_layers(out, state.layers);
    write_image(out, state.selection);
    write_images(out, state.saved_selections);
    write_images(out, state.snapshots);
    out << state.paths;
    out << quint32(state.horizontal_guides.size());
    for (const auto value : state.horizontal_guides)
        out << value;
    out << quint32(state.vertical_guides.size());
    for (const auto value : state.vertical_guides)
        out << value;
    checked(out);
    if (bytes.size() > file_budget)
        throw EditError(QStringLiteral("工程超过 M1 的 1GiB 保存预算"));
    const QJsonObject manifest{{"schema", int(schema)},
                               {"workingSpace", "linear-rec2020"},
                               {"pixels", "rgba-f32-le-premultiplied"},
                               {"payload", "document.bin"}};
    const auto zip =
        archive({{"manifest.json", QJsonDocument(manifest).toJson()}, {"document.bin", bytes}});
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(zip) != zip.size())
        throw EditError(file.errorString());
    if (simulate_failure) {
        file.cancelWriting();
        throw EditError(QStringLiteral("注入保存失败"));
    }
    if (!file.commit())
        throw EditError(file.errorString());
}
State load_project(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw EditError(file.errorString());
    if (file.size() > file_budget + 65536)
        throw EditError(QStringLiteral("工程超过当前读取预算"));
    const auto entries = unarchive(file.readAll());
    const auto manifest = QJsonDocument::fromJson(entries.value("manifest.json")).object();
    if ((manifest.value("schema").toInt() < 2 || manifest.value("schema").toInt() > int(schema)) ||
        manifest.value("pixels").toString() != "rgba-f32-le-premultiplied")
        throw EditError(QStringLiteral("不支持的工程版本，原文件未修改"));
    QDataStream in(entries.value("document.bin"));
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 version{};
    State state;
    in >> version >> state.size >> state.name >> state.ppi;
    if (version < 2 || version > schema || int(version) != manifest.value("schema").toInt())
        throw EditError(QStringLiteral("工程版本不匹配"));
    int count{};
    state.layers = read_layers(in, 0, count, version);
    state.selection = read_image(in);
    state.saved_selections = read_images(in);
    state.snapshots = read_images(in);
    in >> state.paths;
    for (auto* guides : {&state.horizontal_guides, &state.vertical_guides}) {
        quint32 length{};
        in >> length;
        if (length > 10000)
            throw EditError(QStringLiteral("参考线过多"));
        for (quint32 i = 0; i < length; ++i) {
            double value{};
            in >> value;
            guides->push_back(value);
        }
    }
    checked(in);
    if (!in.atEnd())
        throw EditError(QStringLiteral("未知工程尾部数据"));
    validate_state(state);
    return state;
}
} // namespace huelith::editing
