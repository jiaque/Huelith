#include "huelith/editing/raw_cache.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace huelith::editing {
RawCache::RawCache(qint64 memory_budget, qint64 disk_budget)
    : memory_budget_(std::max(qint64(0), memory_budget)),
      disk_budget_(std::max(qint64(0), disk_budget)) {
    if (!directory_.isValid())
        throw EditError(QStringLiteral("无法建立 RAW 临时缓存目录"));
}
QString raw_cache_key(const QByteArray& source, const RawOptions& options) {
    QByteArray model_hash;
    if (options.denoise) {
        QFile model(options.model_path);
        if (!model.open(QIODevice::ReadOnly))
            throw EditError(QStringLiteral("AI RAW 模型缺失，请指定已验证的 Bayer ONNX 模型"));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&model))
            throw EditError(QStringLiteral("无法读取 AI RAW 模型"));
        model_hash = hash.result().toHex();
    }
    const QJsonObject identity{
        {"pipeline", "raw-linear-rec2020-v4-denoise-reference"},
        {"source",
         QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex())},
        {"model", QString::fromLatin1(model_hash)},
        {"denoise", options.denoise},
        {"demosaic", options.denoise        ? "model"
                     : options.high_quality ? "malvar-2004"
                                            : "bilinear"},
        {"customModel", options.denoise && options.custom_model},
        {"directml", options.denoise && options.directml},
        {"exposure", options.exposure}};
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}
bool RawCache::contains(const QString& key) const {
    return !key.isEmpty() && std::any_of(entries_.begin(), entries_.end(),
                                         [&](const Entry& entry) { return entry.key == key; });
}
std::function<RawResult()> RawCache::read_task(const QString& key) const {
    const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& entry){return entry.key==key;});
    if(found==entries_.end())throw EditError(QStringLiteral("RAW缓存已失效，请更新全部预览"));
    return [entry=*found]{
        auto result=entry.result;
        if(!result.image.isNull())return result;
        QFile file(entry.path);
        QImage image(entry.size,entry.format),reference=entry.has_reference?QImage(entry.size,entry.format):QImage{};
        if(image.isNull()||!file.open(QIODevice::ReadOnly)||file.size()!=entry.bytes||file.read(reinterpret_cast<char*>(image.bits()),image.sizeInBytes())!=image.sizeInBytes()||
           (entry.has_reference && (reference.isNull()||file.read(reinterpret_cast<char*>(reference.bits()),reference.sizeInBytes())!=reference.sizeInBytes())))
            throw EditError(QStringLiteral("RAW临时缓存读取失败，请更新预览"));
        image.setColorSpace(entry.color_space);reference.setColorSpace(entry.color_space);
        result.image=std::move(image);result.reference=std::move(reference);return result;
    };
}
std::optional<RawResult> RawCache::get(const QString& key) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&](const Entry& entry) { return entry.key == key; });
    if (found == entries_.end())
        return std::nullopt;
    entries_.splice(entries_.begin(), entries_, found);
    auto& entry = entries_.front();
    if (entry.result.image.isNull()) {
        QFile file(entry.path);
        QImage image(entry.size, entry.format);
        QImage reference = entry.has_reference ? QImage(entry.size, entry.format) : QImage{};
        if (image.isNull() || !file.open(QIODevice::ReadOnly) || file.size() != entry.bytes ||
            file.read(reinterpret_cast<char*>(image.bits()), image.sizeInBytes()) !=
                image.sizeInBytes() ||
            (entry.has_reference &&
             (reference.isNull() || file.read(reinterpret_cast<char*>(reference.bits()),
                                              reference.sizeInBytes()) != reference.sizeInBytes())))
            throw EditError(QStringLiteral("RAW 临时缓存读取失败，请重新计算"));
        image.setColorSpace(entry.color_space);
        reference.setColorSpace(entry.color_space);
        entry.result.image = std::move(image);
        entry.result.reference = std::move(reference);
        memory_bytes_ += entry.bytes;
    }
    const auto result = entry.result;
    trim_memory();
    return result;
}
void RawCache::put(const QString& key, const RawResult& result) {
    const auto& image = result.image;
    const auto& reference = result.reference;
    if (!reference.isNull() &&
        (reference.size() != image.size() || reference.format() != image.format() ||
         reference.colorSpace() != image.colorSpace()))
        throw EditError(QStringLiteral("RAW 缓存对照尺寸或色彩空间不一致"));
    const auto bytes = image.sizeInBytes() + reference.sizeInBytes();
    if (key.isEmpty() || image.isNull() || bytes > disk_budget_)
        throw EditError(QStringLiteral("RAW 结果超出会话缓存预算"));
    const auto path = directory_.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()) !=
            image.sizeInBytes() ||
        (!reference.isNull() && file.write(reinterpret_cast<const char*>(reference.constBits()),
                                           reference.sizeInBytes()) != reference.sizeInBytes()) ||
        !file.commit())
        throw EditError(QStringLiteral("RAW 缓存写入失败，请检查磁盘空间"));
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->key == key) {
            memory_bytes_ -= it->result.image.isNull() ? 0 : it->bytes;
            disk_bytes_ -= it->bytes;
            QFile::remove(it->path);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    while (!entries_.empty() && disk_bytes_ + bytes > disk_budget_) {
        const auto& old = entries_.back();
        memory_bytes_ -= old.result.image.isNull() ? 0 : old.bytes;
        disk_bytes_ -= old.bytes;
        QFile::remove(old.path);
        entries_.pop_back();
    }
    entries_.push_front({key, path, result, image.size(), image.format(), image.colorSpace(), bytes,
                         !reference.isNull()});
    memory_bytes_ += bytes;
    disk_bytes_ += bytes;
    trim_memory();
}
void RawCache::trim_memory() {
    for (auto it = entries_.rbegin(); it != entries_.rend() && memory_bytes_ > memory_budget_;
         ++it) {
        if (!it->result.image.isNull()) {
            it->result.image = {};
            it->result.reference = {};
            memory_bytes_ -= it->bytes;
        }
    }
}
qint64 RawCache::memory_bytes() const noexcept {
    return memory_bytes_;
}
void RawCache::configure(qint64 memory_budget, qint64 disk_budget) {
    if (memory_budget < 0 || disk_budget < 1)
        throw EditError(QStringLiteral("RAW缓存预算无效"));
    memory_budget_ = memory_budget;
    disk_budget_ = disk_budget;
    while (!entries_.empty() && disk_bytes_ > disk_budget_) {
        const auto& entry = entries_.back();
        if (QFile::exists(entry.path) && !QFile::remove(entry.path))
            throw EditError(QStringLiteral("无法清除RAW缓存文件"));
        memory_bytes_ -= entry.result.image.isNull() ? 0 : entry.bytes;
        disk_bytes_ -= entry.bytes;
        entries_.pop_back();
    }
    trim_memory();
}
void RawCache::clear() {
    while (!entries_.empty()) {
        const auto& entry = entries_.back();
        if (QFile::exists(entry.path) && !QFile::remove(entry.path))
            throw EditError(QStringLiteral("无法清除RAW缓存文件"));
        memory_bytes_ -= entry.result.image.isNull() ? 0 : entry.bytes;
        disk_bytes_ -= entry.bytes;
        entries_.pop_back();
    }
}
qint64 RawCache::disk_bytes() const noexcept {
    return disk_bytes_;
}
} // namespace huelith::editing
