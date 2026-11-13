#include <QCoreApplication>
#include "huelith/editing/photoshop_file.h"
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QtEndian>
#include <climits>
#include <zlib.h>

namespace huelith::editing {
namespace {
void bad(const QString& s) {
    throw EditError("PSD/PSB：" + s);
}
struct Reader {
    QByteArrayView data;
    QByteArrayView take(quint64 n) {
        if (n > quint64(data.size()))
            bad("数据截断或区段长度无效");
        auto v = data.first(qsizetype(n));
        data = data.sliced(qsizetype(n));
        return v;
    }
    template <class T> T num() {
        return qFromBigEndian<T>(take(sizeof(T)).data());
    }
    quint64 length(bool big = false) {
        return big ? num<quint64>() : num<quint32>();
    }
    QByteArray tag() {
        return take(4).toByteArray();
    }
    Reader section(bool big = false) {
        return {take(length(big))};
    }
};
constexpr qint64 budget = 48'000'000;
struct Rect {
    int y, x, b, r;
    static Rect read(Reader& in) {
        return {in.num<qint32>(), in.num<qint32>(), in.num<qint32>(), in.num<qint32>()};
    }
    QSize size() const {
        qint64 w = qint64(r) - x, h = qint64(b) - y;
        if (w < 0 || h < 0 || w > 300000 || h > 300000 || w * h > budget)
            bad("图层尺寸超出预算");
        return {int(w), int(h)};
    }
};
const QList<QByteArray> blends = {"norm", "dark", "mul ", "idiv", "lbrn", "lite", "scrn", "div ",
                                  "lddg", "over", "sLit", "hLit", "vLit", "lLit", "pLit", "hMix",
                                  "diff", "smud", "fsub", "fdiv", "hue ", "sat ", "colr", "lum "};
struct Record {
    Layer l;
    Rect box{}, mask{};
    int bg{}, flag{}, divider{};
    QList<std::pair<int, quint64>> channels;
};
QByteArray unzip(QByteArrayView compressed, qsizetype size) {
    // A bounded destination rejects damaged streams and decompression bombs.
    QByteArray raw(size, Qt::Uninitialized);
    uLongf length = uLongf(size);
    uLong input = uLong(compressed.size());
    if (compressed.size() > ULONG_MAX ||
        uncompress2(reinterpret_cast<Bytef*>(raw.data()), &length,
                    reinterpret_cast<const Bytef*>(compressed.data()), &input) != Z_OK ||
        length != uLongf(size) || input != uLong(compressed.size()))
        bad("ZIP 通道损坏、截断或解压尺寸不匹配");
    return raw;
}
void unpredict(QByteArray& raw, int width, int rows, int depth) {
    for (int y = 0; y < rows; ++y) {
        auto* row = reinterpret_cast<uchar*>(raw.data()) + qsizetype(y) * width * (depth / 8);
        for (int x = 1; x < width; ++x) {
            if (depth == 8)
                row[x] = uchar(row[x] + row[x - 1]);
            else
                qToBigEndian<quint16>(quint16(qFromBigEndian<quint16>(row + x * 2) +
                                              qFromBigEndian<quint16>(row + (x - 1) * 2)),
                                      row + x * 2);
        }
    }
}
QByteArray plane(Reader in, QSize sz, bool big, int depth) {
    int row = sz.width() * (depth / 8);
    auto compression = in.num<quint16>();
    if (compression == 0)
        return in.take(qint64(row) * sz.height()).toByteArray();
    if (compression == 2 || compression == 3) {
        auto raw = unzip(in.data, qsizetype(row) * sz.height());
        if (compression == 3)
            unpredict(raw, sz.width(), sz.height(), depth);
        return raw;
    }
    if (compression != 1)
        bad("未知图层压缩格式");
    QList<quint32> rows;
    for (int y = 0; y < sz.height(); ++y)
        rows.append(big ? in.num<quint32>() : in.num<quint16>());
    QByteArray out;
    out.reserve(qint64(row) * sz.height());
    for (auto bytes : rows) {
        Reader r{in.take(bytes)};
        auto end = out.size() + row;
        while (!r.data.empty()) {
            int n = r.num<qint8>();
            if (n == -128)
                continue;
            int count = n < 0 ? 1 - n : 1 + n;
            if (out.size() + count > end)
                bad("无效 RLE 行长度");
            if (n < 0)
                out.append(count, r.num<char>());
            else
                out.append(r.take(count));
        }
        if (out.size() != end)
            bad("RLE 行截断");
    }
    return out;
}
} // namespace
State read_photoshop(const QString& path, QStringList& warnings, bool mergedOnly) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0)
        bad("无法读取：" + file.errorString());
    auto* mapped = file.map(0, file.size());
    if (!mapped)
        bad("无法映射文件：" + file.errorString());
    // QFile unmaps on destruction, including exceptions; channel decoding remains bounded.
    Reader r{QByteArrayView(reinterpret_cast<const char*>(mapped), file.size())};
    if (r.tag() != "8BPS")
        bad("签名无效");
    int version = r.num<quint16>();
    if (version != 1 && version != 2)
        bad("版本不支持");
    bool big = version == 2;
    r.take(6);
    int nc = r.num<quint16>();
    quint32 h = r.num<quint32>(), w = r.num<quint32>();
    int depth = r.num<quint16>();
    if (nc < 3 || nc > 56 || !w || !h || w > 300000 || h > 300000 || quint64(w) * h > budget ||
        (depth != 8 && depth != 16) || r.num<quint16>() != 3)
        bad("支持 RGB 8/16 位，画布上限 4800 万像素");
    r.section();
    auto resources = r.section();
    QColorSpace colors = QColorSpace::SRgb;
    while (!resources.data.empty()) {
        if (resources.tag() != "8BIM")
            bad("图像资源签名无效");
        int id = resources.num<quint16>();
        int n = resources.num<quint8>();
        resources.take(n + ((n + 1) % 2));
        auto v = resources.section();
        if (v.data.size() % 2)
            resources.take(1);
        if (id == 1039) {
            colors = QColorSpace::fromIccProfile(v.data.toByteArray());
            if (!colors.isValid() || colors.colorModel() != QColorSpace::ColorModel::Rgb)
                bad("嵌入 RGB ICC 无法转换");
        }
    }
    auto section = r.section(big);
    Reader layers{};
    if (!section.data.empty())
        layers = section.section(big);
    if (depth == 16 && !section.data.empty()) {
        section.section();
        while (section.data.size() >= 12) {
            section.tag();
            auto key = section.tag();
            bool wide =
                big && QList<QByteArray>{"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                         "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"}
                           .contains(key);
            auto block = section.section(wide);
            section.take((4 - block.data.size() % 4) % 4);
            if (key == "Lr16") {
                layers = block;
                break;
            }
        }
    }
    const int signedCount = layers.data.empty() ? 0 : int(layers.num<qint16>());
    int count = mergedOnly ? 0 : std::abs(signedCount);
    if (count > 2048)
        bad("图层数超出预算");
    QList<Record> records;
    for (int i = 0; i < count; ++i) {
        Record a;
        a.box = Rect::read(layers);
        a.box.size();
        int chans = layers.num<quint16>();
        if (chans > 56)
            bad("图层通道数无效");
        for (int c = 0; c < chans; ++c) {
            int id = layers.num<qint16>();
            auto len = layers.length(big);
            a.channels.append({id, len});
        }
        if (layers.tag() != "8BIM")
            bad("图层签名无效");
        auto mode = layers.tag();
        int bi = blends.indexOf(mode);
        if (bi < 0 && mode != "pass")
            warnings.append("未映射混合模式：" + QString::fromLatin1(mode));
        a.l.blend = Blend(std::max(bi, 0));
        a.l.opacity = layers.num<quint8>() / 255.f;
        a.l.clipped = layers.num<quint8>() != 0;
        a.l.visible = !(layers.num<quint8>() & 2);
        layers.take(1);
        auto extra = layers.section();
        auto mask = extra.section();
        if (!mask.data.empty()) {
            a.mask = Rect::read(mask);
            a.mask.size();
            a.bg = mask.num<quint8>();
            a.flag = mask.num<quint8>();
            a.l.mask_enabled = !(a.flag & 2);
            if (a.flag & 16)
                warnings.append("蒙版羽化/密度参数未转换");
        }
        extra.section();
        int len = extra.num<quint8>();
        a.l.name = QString::fromLatin1(extra.take(len).toByteArray());
        extra.take((4 - (len + 1) % 4) % 4);
        while (extra.data.size() >= 12) {
            extra.tag();
            auto key = extra.tag();
            auto block = extra.section(
                big && QList<QByteArray>{"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                         "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"}
                           .contains(key));
            if (block.data.size() % 2)
                extra.take(1);
            if (key == "luni") {
                auto n = block.num<quint32>();
                if (n > 65536)
                    bad("图层名过长");
                a.l.name.clear();
                while (n--)
                    a.l.name += QChar(block.num<quint16>());
            } else if (key == "lsct" || key == "lsdk") {
                a.divider = block.num<qint32>();
                if (block.data.size() >= 8) {
                    block.tag();
                    auto blend = block.tag();
                    a.l.isolated = blend != "pass";
                    if (blends.contains(blend))
                        a.l.blend = Blend(blends.indexOf(blend));
                }
            } else if (!QList<QByteArray>{"lyid", "lclr", "fxrp", "lnsr"}.contains(key))
                warnings.append(QCoreApplication::translate("Huelith", "图层「%1」：%2 以像素外观导入，原生属性未转换")
                                    .arg(a.l.name, QString::fromLatin1(key)));
        }
        records.append(std::move(a));
    }
    State s;
    s.size = {int(w), int(h)};
    s.name = QFileInfo(path).completeBaseName();
    qint64 used = 0;
    std::vector<std::vector<Layer>> groups(1);
    for (auto& a : records) {
        auto sz = a.box.size();
        used += qint64(sz.width()) * sz.height();
        if (used > budget)
            bad("解码图层总量超过 4800 万像素");
        QImage image(sz, QImage::Format_RGBA64);
        if (!sz.isEmpty() && image.isNull())
            bad("图层内存不足");
        image.fill(Qt::black);
        for (auto [id, len] : a.channels) {
            Reader channel{layers.take(len)};
            if (id == -2) {
                auto ms = a.mask.size();
                if (ms.isEmpty())
                    continue;
                used += qint64(w) * h + qint64(ms.width()) * ms.height();
                if (used > budget)
                    bad("蒙版超过解码预算");
                auto raw = plane(channel, ms, big, depth);
                a.l.mask = QImage(s.size, QImage::Format_Grayscale8);
                a.l.mask.fill(a.bg);
                for (int y = 0; y < ms.height(); ++y) {
                    qint64 dy = qint64(y) + a.mask.y + ((a.flag & 1) ? a.box.y : 0);
                    if (dy < 0 || dy >= h)
                        continue;
                    for (int x = 0; x < ms.width(); ++x) {
                        qint64 dx = qint64(x) + a.mask.x + ((a.flag & 1) ? a.box.x : 0);
                        if (dx >= 0 && dx < w)
                            a.l.mask.scanLine(dy)[dx] =
                                quint8(raw[(y * ms.width() + x) * (depth / 8)]);
                    }
                }
                continue;
            }
            if (id < -1 || id > 2 || sz.isEmpty())
                continue;
            auto raw = plane(channel, sz, big, depth);
            for (int y = 0; y < sz.height(); ++y) {
                auto* dest = reinterpret_cast<quint16*>(image.scanLine(y));
                for (int x = 0; x < sz.width(); ++x) {
                    int i = y * sz.width() + x;
                    dest[4 * x + (id < 0 ? 3 : id)] =
                        depth == 8 ? quint8(raw[i]) * 257
                                   : qFromBigEndian<quint16>(raw.constData() + 2 * i);
                }
            }
        }
        if (a.divider == 3) {
            groups.emplace_back();
            continue;
        }
        if (a.divider == 1 || a.divider == 2) {
            if (groups.size() < 2)
                bad("图层组边界不匹配");
            a.l.kind = LayerKind::group;
            a.l.children = std::move(groups.back());
            groups.pop_back();
        } else {
            if (image.isNull())
                continue;
            image.setColorSpace(colors);
            a.l.pixels = to_working(image);
            a.l.transform.translate(a.box.x, a.box.y);
        }
        groups.back().push_back(std::move(a.l));
    }
    if (groups.size() != 1)
        bad("图层组未闭合");
    s.layers = std::move(groups.front());
    if (s.layers.empty()) {
        const int comp = r.num<quint16>();
        if (comp > 3)
            bad("合成图使用未支持的压缩格式");
        QByteArray zipPixels;
        if (comp >= 2) {
            const quint64 size = quint64(nc) * w * h * (depth / 8);
            if (size > quint64(budget) * 8)
                bad("合成图通道解压超过内存预算");
            zipPixels = unzip(r.data, qsizetype(size));
            if (comp == 3)
                unpredict(zipPixels, int(w), int(h) * nc, depth);
            r.data = zipPixels;
        }
        QByteArrayView rowTable;
        if (comp == 1)
            rowTable = r.take(quint64(nc) * h * (big ? 4 : 2));
        QImage image(s.size, QImage::Format_RGBA64);
        if (image.isNull())
            bad("合成图内存不足");
        image.fill(Qt::black);
        for (int c = 0; c < nc; ++c) {
            QByteArray channel(2, 0);
            channel[1] = char(comp >= 2 ? 0 : comp);
            quint64 n = quint64(w) * h * (depth / 8);
            if (comp == 1) {
                auto rowBytes =
                    rowTable.sliced(qsizetype(c) * h * (big ? 4 : 2), h * (big ? 4 : 2));
                channel.append(rowBytes);
                Reader table{rowBytes};
                n = 0;
                for (quint32 y = 0; y < h; ++y)
                    n += big ? table.num<quint32>() : table.num<quint16>();
            }
            auto payload = r.take(n);
            if (c > 2 && !(c == 3 && signedCount < 0))
                continue;
            channel.append(payload);
            auto raw = plane(Reader{channel}, s.size, big, depth);
            for (quint32 y = 0; y < h; ++y) {
                auto* dest = reinterpret_cast<quint16*>(image.scanLine(y));
                for (quint32 x = 0; x < w; ++x) {
                    auto i = y * w + x;
                    dest[4 * x + c] = depth == 8 ? quint8(raw[i]) * 257
                                                 : qFromBigEndian<quint16>(raw.constData() + 2 * i);
                }
            }
        }
        image.setColorSpace(colors);
        Layer flat;
        flat.name = s.name;
        flat.pixels = to_working(image);
        s.layers = {flat};
        if (count)
            warnings.append("未发现可编辑像素层，使用文件中的合成图");
        if (nc > 3 && signedCount >= 0)
            warnings.append("合成图的额外通道未导入");
    }
    s.metadata["photoshopSource"] = QFileInfo(path).canonicalFilePath();
    warnings.removeDuplicates();
    validate_state(s);
    return s;
}
void write_photoshop(const State& state, const QString& path) {
    const auto prior = state.metadata.value("photoshopSource").toString();
    const auto dest = QFileInfo(path).canonicalFilePath();
    if (!prior.isEmpty() && !dest.isEmpty() && prior.compare(dest, Qt::CaseInsensitive) == 0)
        bad("请另选副本路径，以保留导入的原文件");
    const auto suffix = QFileInfo(path).suffix().toLower();
    const bool big = suffix == "psb";
    if (!big && suffix != "psd")
        bad("请选择 .psd 或 .psb 后缀");
    int w = state.size.width(), h = state.size.height();
    qint64 pixels = qint64(w) * h;
    struct Entry {
        Layer layer;
        int divider{};
        bool separateMask{};
    };
    std::vector<Entry> entries;
    std::function<void(const std::vector<Layer>&)> collect = [&](const auto& layers) {
        for (auto l : layers) {
            if (l.kind == LayerKind::adjustment)
                bad("调整图层暂不能原生导出；请在工程副本中合并相关图层");
            if (l.kind == LayerKind::group) {
                if (!l.transform.isIdentity() || l.warped || !l.effects.empty() ||
                    l.styles.active() || l.vector_enabled || l.artboard || l.frame)
                    bad("带变换或滤镜的图层组需在工程副本中栅格化后导出");
                Layer end;
                end.name = "</Layer group>";
                entries.push_back({end, 3, false});
                collect(l.children);
                l.children.clear();
                entries.push_back({l, 1, !l.mask.isNull()});
            } else {
                entries.push_back({l, 0, !l.mask.isNull() && !l.styles.active()});
            }
        }
    };
    collect(state.layers);
    if (entries.empty() || entries.size() > 2048 || pixels <= 0 ||
        (!big && (w > 30000 || h > 30000)) || pixels > budget / (qint64(entries.size()) + 1))
        bad("画布或分层像素总量超过导出预算");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        bad(file.errorString());
    QDataStream out(&file);
    auto tag = [&](const char* v) { out.writeRawData(v, 4); };
    auto zeros = [&](int n) {
        for (int i = 0; i < n; ++i)
            out << quint8(0);
    };
    auto length = [&](quint64 n) {
        if (big)
            out << n;
        else
            out << quint32(n);
    };
    auto patch = [&](qint64 at) {
        auto pos = file.pos();
        if (!file.seek(at))
            bad(file.errorString());
        length(pos - at - (big ? 8 : 4));
        if (!file.seek(pos))
            bad(file.errorString());
    };
    tag("8BPS");
    out << quint16(big ? 2 : 1);
    zeros(6);
    out << quint16(4) << quint32(h) << quint32(w) << quint16(8) << quint16(3) << quint32(0);
    auto icc = QColorSpace(QColorSpace::SRgb).iccProfile();
    out << quint32(12 + icc.size() + (icc.size() % 2));
    tag("8BIM");
    out << quint16(1039) << quint16(0) << quint32(icc.size());
    out.writeRawData(icc.constData(), icc.size());
    zeros(icc.size() % 2);
    auto section = file.pos();
    length(0);
    auto info = file.pos();
    length(0);
    out << qint16(-int(entries.size()));
    for (const auto& entry : entries) {
        const auto& l = entry.layer;
        out << qint32(0) << qint32(0) << qint32(entry.divider ? 0 : h)
            << qint32(entry.divider ? 0 : w) << quint16(4 + int(entry.separateMask));
        for (int c : {0, 1, 2, -1}) {
            out << qint16(c);
            length(entry.divider ? 2 : pixels + 2);
        }
        if (entry.separateMask) {
            out << qint16(-2);
            length(pixels + 2);
        }
        tag("8BIM");
        tag(blends.at(int(l.blend)).constData());
        out << quint8(qRound(l.opacity * 255)) << quint8(l.clipped) << quint8(l.visible ? 0 : 2)
            << quint8(0);
        QString name = l.name.left(65535);
        QByteArray asc = name.toLatin1().left(255);
        int pad = (4 - (1 + asc.size()) % 4) % 4;
        out << quint32(8 + 1 + asc.size() + pad + 16 + 2 * name.size() +
                       (entry.separateMask ? 20 : 0) + (entry.divider ? 24 : 0));
        out << quint32(entry.separateMask ? 20 : 0);
        if (entry.separateMask)
            out << qint32(0) << qint32(0) << qint32(h) << qint32(w) << quint8(0)
                << quint8(l.mask_enabled ? 0 : 2) << quint16(0);
        out << quint32(0) << quint8(asc.size());
        out.writeRawData(asc.constData(), asc.size());
        zeros(pad);
        tag("8BIM");
        tag("luni");
        out << quint32(4 + name.size() * 2) << quint32(name.size());
        for (auto ch : name)
            out << quint16(ch.unicode());
        if (entry.divider) {
            tag("8BIM");
            tag("lsct");
            out << quint32(12) << quint32(entry.divider);
            tag("8BIM");
            tag(l.isolated ? blends.at(int(l.blend)).constData() : "pass");
        }
    }
    auto channels = [&](const QImage& linear, bool merged) {
        auto image =
            display_image(linear, QColorSpace::SRgb).convertToFormat(QImage::Format_RGBA8888);
        QByteArray row(w, Qt::Uninitialized);
        if (merged)
            out << quint16(0);
        for (int c = 0; c < 4; ++c) {
            if (!merged)
                out << quint16(0);
            for (int y = 0; y < h; ++y) {
                auto* src = image.constScanLine(y);
                for (int x = 0; x < w; ++x)
                    row[x] = char(src[4 * x + c]);
                if (out.writeRawData(row.constData(), w) != w)
                    bad(file.errorString());
            }
        }
    };
    for (const auto& entry : entries) {
        auto layer = entry.layer;
        State one;
        one.name = state.name;
        one.size = state.size;
        one.global_light_angle = state.global_light_angle;
        layer.visible = true;
        layer.opacity = 1;
        layer.blend = Blend::normal;
        layer.clipped = false;
        if (entry.separateMask)
            layer.mask = {};
        one.layers.push_back(std::move(layer));
        if (entry.divider) {
            for (int c = 0; c < 4; ++c)
                out << quint16(0);
        } else {
            channels(render(one), false);
        }
        if (entry.separateMask) {
            QImage coverage(state.size, QImage::Format_ARGB32);
            coverage.fill(Qt::black);
            QPainter painter(&coverage);
            painter.setTransform(entry.layer.mask_transform);
            painter.drawImage(QPoint(), entry.layer.mask);
            painter.end();
            coverage = coverage.convertToFormat(QImage::Format_Grayscale8);
            out << quint16(0);
            for (int y = 0; y < h; ++y)
                out.writeRawData(reinterpret_cast<const char*>(coverage.constScanLine(y)), w);
        }
    }
    if ((file.pos() - info - (big ? 8 : 4)) % 2)
        zeros(1);
    patch(info);
    out << quint32(0);
    patch(section);
    channels(render(state), true);
    if (out.status() != QDataStream::Ok || !file.commit())
        bad("副本写入失败：" + file.errorString());
}
} // namespace huelith::editing
