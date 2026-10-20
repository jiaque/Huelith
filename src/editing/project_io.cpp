#include "project_archive.h"
#include <QTemporaryFile>
#include "huelith/editing/image_storage.h"
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
constexpr quint32 schema = 16;
void checked(QDataStream& stream) {
    if (stream.status() != QDataStream::Ok)
        throw EditError(QStringLiteral("工程截断或数据无效"));
}
void write_image(QDataStream& out, const QImage& image) {
    out << qint32(image.width()) << qint32(image.height()) << qint32(image.format());
    if (image.isNull())
        return;
    out << image.colorSpace().iccProfile();
    const int rows=std::max(1,int((8LL*1024*1024)/image.bytesPerLine()));
    out << qint32(rows);
    for(int y=0;y<image.height();y+=rows) {
        const auto length=std::min(rows,image.height()-y)*image.bytesPerLine();
        const auto block=QByteArray::fromRawData(reinterpret_cast<const char*>(image.constScanLine(y)),length);
        out << qCompress(block,1);
        checked(out);
    }
}
QByteArray bounded_bytes(QDataStream& in,qint64 limit=file_budget) {
    quint32 size{};
    in >> size;
    checked(in);
    if (size == 0xffffffffU)
        return {};
    if (size > limit || qint64(size) > in.device()->bytesAvailable())
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
    if (width < 1 || height < 1 || qint64(width) * height > huelith::editing::maximum_image_pixels ||
        (format != QImage::Format_RGBA32FPx4_Premultiplied && format != QImage::Format_Grayscale8 &&
         format != QImage::Format_RGBA8888))
        throw EditError(QStringLiteral("不支持的资源尺寸或格式"));
    auto result=allocate_image(width,height,static_cast<QImage::Format>(format));
    QByteArray icc;
    if(in.device()->property("tiledImages").toBool()) {
        icc=bounded_bytes(in,16*1024*1024);
        qint32 rows{};in>>rows;
        if(rows<1 || qint64(rows)*result.bytesPerLine()>8LL*1024*1024)
            throw EditError(QStringLiteral("工程分块尺寸无效"));
        for(int y=0;y<height;y+=rows) {
            const auto compressed=bounded_bytes(in,9*1024*1024);
            const auto expected=std::min(rows,height-y)*result.bytesPerLine();
            if(compressed.size()<4 || compressed.size()>9*1024*1024 ||
               qFromBigEndian<quint32>(compressed.constData())!=expected)
                throw EditError(QStringLiteral("工程分块长度无效"));
            const auto block=qUncompress(compressed);
            if(block.size()!=expected)throw EditError(QStringLiteral("工程分块损坏"));
            std::memcpy(result.scanLine(y),block.constData(),std::size_t(expected));
        }
    } else {
        const auto bytes=bounded_bytes(in);icc=bounded_bytes(in);
        if(bytes.size()!=result.sizeInBytes())throw EditError(QStringLiteral("资源像素长度不匹配"));
        std::memcpy(result.bits(),bytes.constData(),std::size_t(bytes.size()));
    }
    if(!icc.isEmpty()) result.setColorSpace(QColorSpace::fromIccProfile(icc));
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
        out << layer.raw_source << QJsonDocument(layer.raw_recipe).toJson(QJsonDocument::Compact);
        out << QJsonDocument(tone_json(layer.tone)).toJson(QJsonDocument::Compact);
        out << layer.background;
        out << quint32(layer.effects.size());
        for (const auto& effect : layer.effects) {
            out << effect.id << effect.name << effect.enabled << effect.amount
                << QJsonDocument(tone_json(effect.tone)).toJson(QJsonDocument::Compact)
                << (effect.lut ? effect.lut->source.toUtf8() : QByteArray{});
            write_image(out, effect.mask);
            write_image(out, effect.liquify);
            out << effect.blur_radius;
            out << QJsonDocument(effect.portrait).toJson(QJsonDocument::Compact);
        }
        out << QJsonDocument(text_json(layer.text)).toJson(QJsonDocument::Compact)
            << QJsonDocument(shape_json(layer.shape)).toJson(QJsonDocument::Compact);
        out << QJsonDocument(styles_json(layer.styles)).toJson(QJsonDocument::Compact);
        out << layer.frame.has_value();
        if (layer.frame)
            out << *layer.frame;
        out << QJsonDocument(layer.source_metadata).toJson(QJsonDocument::Compact);
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
        if (kind < 0 || kind > qint32(version >= 8 ? LayerKind::shape : LayerKind::composition))
            throw EditError(QStringLiteral("工程含未知图层，已拒绝打开并保留原文件"));
        layer.kind = static_cast<LayerKind>(kind);
        layer.blend = static_cast<Blend>(blend);
        if (version >= 3) {
            in >> layer.warped;
            for (auto& point : layer.warp_grid)
                in >> point;
        }
        if (version >= 4) {
            layer.raw_source = bounded_bytes(in);
            const auto recipe = bounded_bytes(in);
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(recipe, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                throw EditError(QStringLiteral("RAW 配方无效"));
            layer.raw_recipe = document.object();
        }
        if (version >= 5) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(bounded_bytes(in), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                throw EditError(QStringLiteral("调色配方无效"));
            layer.tone = tone_from_json(document.object());
        }
        if (version >= 6)
            in >> layer.background;
        if (version >= 7) {
            quint32 effect_count{};
            in >> effect_count;
            checked(in);
            if (effect_count > 64)
                throw EditError(QStringLiteral("效果数量超限"));
            for (quint32 e = 0; e < effect_count; ++e) {
                Effect effect;
                in >> effect.id >> effect.name >> effect.enabled >> effect.amount;
                QJsonParseError error;
                const auto json = QJsonDocument::fromJson(bounded_bytes(in), &error);
                if (error.error != QJsonParseError::NoError || !json.isObject())
                    throw EditError(QStringLiteral("效果参数无效"));
                effect.tone = tone_from_json(json.object());
                const auto lut = bounded_bytes(in);
                if (!lut.isEmpty())
                    effect.lut = parse_cube(QString::fromUtf8(lut));
                effect.mask = read_image(in);
                if (version >= 12)
                    effect.liquify = read_image(in);
                if (version >= 13)
                    in >> effect.blur_radius;
                if (version >= 16) {
                    const auto data=bounded_bytes(in,64*1024*1024);
                    const auto document=QJsonDocument::fromJson(data);
                    if(!document.isObject()) throw EditError(QStringLiteral("人像设置损坏"));
                    effect.portrait=document.object();
                }
                layer.effects.push_back(std::move(effect));
            }
        }
        if (version >= 8) {
            const auto text = QJsonDocument::fromJson(bounded_bytes(in));
            const auto shape = QJsonDocument::fromJson(bounded_bytes(in));
            if (!text.isObject() || !shape.isObject())
                throw EditError(QStringLiteral("文字或形状数据损坏"));
            layer.text = text_from_json(text.object());
            layer.shape = shape_from_json(shape.object());
        }
        if (version >= 9) {
            const auto styles = QJsonDocument::fromJson(bounded_bytes(in));
            if (!styles.isObject())
                throw EditError(QStringLiteral("样式数据损坏"));
            layer.styles = styles_from_json(styles.object());
        }
        if (version >= 10) {
            bool has_frame{};
            in >> has_frame;
            if (has_frame) {
                QRectF frame;
                in >> frame;
                layer.frame = frame;
            }
        }
        if(version>=14) {
            const auto data=QJsonDocument::fromJson(bounded_bytes(in));
            if(!data.isObject())throw EditError(QStringLiteral("图层元数据损坏"));
            layer.source_metadata=data.object();
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
} // namespace
void save_project(const Snapshot& snapshot, const QString& path, bool simulate_failure) {
    validate_state(snapshot.state);
    QTemporaryFile payload;
    if(!payload.open()) throw EditError(payload.errorString());
    QDataStream out(&payload);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);
    const auto& state = snapshot.state;
    out << schema << state.size << state.name << state.ppi << state.global_light_angle;
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
    out << QJsonDocument(state.metadata).toJson(QJsonDocument::Compact);
    checked(out);
    const QJsonObject manifest{{"schema", int(schema)},
                               {"workingSpace", "linear-rec2020"},
                               {"pixels", "rgba-f32-le-premultiplied"},
                               {"payload", "document.bin"}};
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)) throw EditError(file.errorString());
    write_project_archive(file,payload,manifest);
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
    QTemporaryFile payload;
    if(!payload.open()) throw EditError(payload.errorString());
    const auto manifest=read_project_archive(file,payload);
    if ((manifest.value("schema").toInt() < 2 || manifest.value("schema").toInt() > int(schema)) ||
        manifest.value("pixels").toString() != "rgba-f32-le-premultiplied")
        throw EditError(QStringLiteral("不支持的工程版本，原文件未修改"));
    QDataStream in(&payload);
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 version{};
    State state;
    in >> version >> state.size >> state.name >> state.ppi;
    if (version >= 11)
        in >> state.global_light_angle;
    if (version < 2 || version > schema || int(version) != manifest.value("schema").toInt())
        throw EditError(QStringLiteral("工程版本不匹配"));
    payload.setProperty("tiledImages",version>=15);
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
    if(version>=14){
        const auto data=QJsonDocument::fromJson(bounded_bytes(in));
        if(!data.isObject())throw EditError(QStringLiteral("文档元数据损坏"));
        state.metadata=data.object();
    }
    checked(in);
    if (!in.atEnd())
        throw EditError(QStringLiteral("未知工程尾部数据"));
    validate_state(state);
    return state;
}
} // namespace huelith::editing
