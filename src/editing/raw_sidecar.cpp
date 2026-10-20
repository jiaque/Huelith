#include "huelith/editing/raw_sidecar.h"
#include "huelith/editing/dng.h"
#include "huelith/editing/raw_recipe.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace huelith::editing {
namespace {
QByteArray source_hash(const Layer& source) {
    if (source.raw_source.isEmpty())
        throw EditError(QStringLiteral("RAW设置缺少原始源数据"));
    return QCryptographicHash::hash(source.raw_source, QCryptographicHash::Sha256).toHex();
}
void validate(const QJsonObject& recipe) {
    RawGroups groups;
    groups.fill(true);
    (void)read_raw_preset(QJsonDocument(raw_preset(QStringLiteral("RAW设置"), recipe, groups))
                              .toJson(QJsonDocument::Compact));
    if (recipe.contains("rating") &&
        (!recipe.value("rating").isDouble() || recipe.value("rating").toInt(-1) < 0 ||
         recipe.value("rating").toInt() > 5 ||
         recipe.value("rating").toDouble() != recipe.value("rating").toInt()))
        throw EditError(QStringLiteral("RAW评级无效"));
    if (recipe.contains("rejected") && !recipe.value("rejected").isBool())
        throw EditError(QStringLiteral("RAW拒绝标记无效"));
}
void check(const std::atomic_bool* cancelled) {
    if (cancelled && cancelled->load())
        throw EditError(QStringLiteral("保存RAW设置已取消"));
}
} // namespace
QString raw_sidecar_path(const QString& path) {
    if (path.isEmpty())
        throw EditError(QStringLiteral("没有RAW原始文件路径；请保存工程以保留设置"));
    return QFileInfo(path).absoluteFilePath() + QStringLiteral(".hraw.json");
}
QJsonObject load_raw_sidecar(const Layer& source) {
    if (source.linked_path.isEmpty())
        return {};
    const auto path = raw_sidecar_path(source.linked_path);
    if (!QFileInfo::exists(path))
        return source.linked_path.endsWith(".dng",Qt::CaseInsensitive) ? dng_recipe(source.raw_source) : QJsonObject{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32LL * 1024 * 1024)
        throw EditError(QStringLiteral("无法读取RAW设置文件：%1").arg(path));
    QJsonParseError error;
    const auto object = QJsonDocument::fromJson(file.readAll(), &error).object();
    if (error.error != QJsonParseError::NoError || object.value("type") != "huelith.raw-sidecar" ||
        object.value("version").toInt() != 1 || !object.value("recipe").isObject())
        throw EditError(QStringLiteral("RAW设置文件损坏或版本不支持：%1").arg(path));
    if (object.value("sourceSha256").toString().toLatin1() != source_hash(source))
        throw EditError(QStringLiteral("RAW源文件已改变，旁车设置未应用：%1").arg(path));
    const auto recipe = object.value("recipe").toObject();
    validate(recipe);
    return recipe;
}
void save_raw_sidecars(const std::vector<Layer>& sources, const std::atomic_bool* cancelled) {
    struct Prepared {
        QString path;
        std::unique_ptr<QSaveFile> file;
    };
    std::vector<Prepared> prepared;
    QSet<QString> paths;
    for (const auto& source : sources) {
        check(cancelled);
        const auto path = raw_sidecar_path(source.linked_path);
        const auto canonical = QFileInfo(source.linked_path).canonicalFilePath();
        if (canonical.isEmpty())
            throw EditError(QStringLiteral("RAW源文件不存在：%1").arg(source.linked_path));
        if (paths.contains(canonical.toCaseFolded()))
            throw EditError(QStringLiteral("同一RAW文件存在多个编辑副本，请分别保存工程"));
        paths.insert(canonical.toCaseFolded());
        QFile original(source.linked_path);
        if (!original.open(QIODevice::ReadOnly))
            throw EditError(QStringLiteral("无法校验RAW源文件"));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!original.atEnd()) {
            check(cancelled);
            const auto chunk = original.read(1024 * 1024);
            if (chunk.isEmpty() && original.error() != QFileDevice::NoError)
                throw EditError(QStringLiteral("RAW源文件读取失败"));
            hash.addData(chunk);
        }
        const auto sha = source_hash(source);
        if (hash.result().toHex() != sha)
            throw EditError(QStringLiteral("原始RAW已改变，未保存旧源设置"));
        validate(source.raw_recipe);
        const auto data = QJsonDocument(QJsonObject{{"type", "huelith.raw-sidecar"},
                                                    {"version", 1},
                                                    {"sourceSha256", QString::fromLatin1(sha)},
                                                    {"recipe", source.raw_recipe}})
                              .toJson(QJsonDocument::Compact);
        if (data.size() > 32LL * 1024 * 1024)
            throw EditError(QStringLiteral("RAW设置文件超过32MiB"));
        auto file = std::make_unique<QSaveFile>(path);
        file->setDirectWriteFallback(false);
        if (!file->open(QIODevice::WriteOnly) || file->write(data) != data.size())
            throw EditError(QStringLiteral("无法暂存RAW设置：%1").arg(path));
        prepared.push_back({path, std::move(file)});
    }
    check(cancelled);
    // Every file is staged before the first commit. Each replacement is atomic;
    // an OS failure between commits is reported with the exact completed count.
    std::size_t complete{};
    for (auto& item : prepared) {
        if (!item.file->commit())
            throw EditError(
                QStringLiteral("已保存%1份RAW设置，无法完成：%2").arg(complete).arg(item.path));
        ++complete;
    }
}
} // namespace huelith::editing
