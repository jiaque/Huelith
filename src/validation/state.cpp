#include "huelith/validation/state.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace huelith::validation {
namespace {
constexpr qsizetype max_block_bytes{16 * 1024 * 1024};
bool valid(const State& state) {
    return !state.document_id.isEmpty() && state.document_id.size() <= 128 &&
           state.block.size() <= max_block_bytes && std::isfinite(state.parameter);
}
QByteArray digest(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
} // namespace
BlockDelta block_delta(const QByteArray& before, const QByteArray& after) {
    if (before.size() != after.size())
        throw std::invalid_argument("fixed block sizes must match");
    qsizetype begin{};
    while (begin < before.size() && before[begin] == after[begin])
        ++begin;
    auto end = before.size();
    while (end > begin && before[end - 1] == after[end - 1])
        --end;
    return {begin, before.mid(begin, end - begin), after.mid(begin, end - begin)};
}
QByteArray apply_delta(const QByteArray& source, const BlockDelta& delta, bool reverse) {
    const auto& expected = reverse ? delta.after : delta.before;
    const auto& replacement = reverse ? delta.before : delta.after;
    if (delta.offset < 0 || delta.offset > source.size() || expected.size() != replacement.size() ||
        expected.size() > source.size() - delta.offset ||
        source.mid(delta.offset, expected.size()) != expected)
        throw std::invalid_argument("delta does not match source");
    auto result = source;
    result.replace(delta.offset, expected.size(), replacement);
    return result;
}
double FrameCache::evaluate(const Snapshot& snapshot, std::int64_t time_ticks,
                            std::uint64_t device_generation) {
    const FrameKey key{snapshot.state.document_id, snapshot.revision, time_ticks,
                       device_generation};
    for (const auto& entry : entries_)
        if (entry.key == key)
            return entry.value;
    const auto value = snapshot.state.parameter + static_cast<double>(time_ticks) / 1000.0;
    if (entries_.size() >= 64)
        entries_.erase(entries_.begin());
    entries_.push_back({key, value});
    return value;
}
std::size_t FrameCache::entry_count() const noexcept {
    return entries_.size();
}
Document::Document(State state) : history_{std::move(state)} {
    if (!valid(history_.front()))
        throw std::invalid_argument("invalid document state");
}
Snapshot Document::snapshot() const {
    return {history_[cursor_], revision_};
}
Session Document::begin_session() const {
    return {snapshot(), history_[cursor_]};
}
bool Document::commit(const Session& session) {
    if (session.base.revision != revision_ || session.base.state != history_[cursor_] ||
        session.draft.document_id != history_[cursor_].document_id || !valid(session.draft))
        return false;
    if (session.draft == history_[cursor_])
        return true;
    auto next = history_;
    next.resize(cursor_ + 1);
    next.push_back(session.draft);
    history_.swap(next);
    ++cursor_;
    ++revision_;
    return true;
}
bool Document::undo() {
    if (cursor_ == 0)
        return false;
    --cursor_;
    ++revision_;
    return true;
}
bool Document::redo() {
    if (cursor_ + 1 == history_.size())
        return false;
    ++cursor_;
    ++revision_;
    return true;
}
bool Document::dirty() const noexcept {
    return revision_ != saved_revision_;
}
void Document::mark_saved(std::uint64_t revision) noexcept {
    if (revision <= revision_)
        saved_revision_ = revision;
}
QString save_snapshot(const QString& path, const Snapshot& snapshot, SaveFault fault) {
    if (!valid(snapshot.state))
        return QStringLiteral("invalid state");
    const QJsonObject data{{"schema", 1},
                           {"document", snapshot.state.document_id},
                           {"parameter", snapshot.state.parameter},
                           {"revision", QString::number(snapshot.revision)},
                           {"block", QString::fromLatin1(snapshot.state.block.toBase64())},
                           {"sha256", QString::fromLatin1(digest(snapshot.state.block))}};
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size())
        return file.errorString();
    if (fault == SaveFault::before_commit) {
        file.cancelWriting();
        return QStringLiteral("injected failure before atomic replacement");
    }
    if (!file.commit())
        return file.errorString();
    return {};
}
LoadResult load_snapshot(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, file.errorString(), false};
    if (file.size() > max_block_bytes * 2)
        return {{}, QStringLiteral("file too large"), false};
    QJsonParseError parse_error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !json.isObject())
        return {{}, QStringLiteral("invalid JSON"), false};
    const auto object = json.object();
    const auto schema = object.value("schema").toInt(-1);
    if (schema != 0 && schema != 1)
        return {{}, QStringLiteral("unsupported schema"), false};
    if (!object.value("document").isString() || !object.value("parameter").isDouble() ||
        !object.value("block").isString() || !object.value("sha256").isString())
        return {{}, QStringLiteral("missing required fields"), false};
    const auto block = QByteArray::fromBase64Encoding(object.value("block").toString().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
    if (!block)
        return {{}, QStringLiteral("invalid block encoding"), false};
    State state{object.value("document").toString(), block.decoded,
                object.value("parameter").toDouble()};
    if (!valid(state) || digest(state.block) != object.value("sha256").toString().toLatin1())
        return {{}, QStringLiteral("invalid state or block checksum"), false};
    return {std::move(state), {}, schema == 0};
}
} // namespace huelith::validation
