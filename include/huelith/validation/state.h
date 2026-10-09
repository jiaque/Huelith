#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <vector>

namespace huelith::validation {
// M0 transaction/storage prototype. No M1 editing commands are exposed.
struct State {
    QString document_id;
    QByteArray block;
    double parameter{};
    friend bool operator==(const State&, const State&) = default;
};
struct Snapshot {
    State state;
    std::uint64_t revision{};
};
struct Session {
    Snapshot base;
    State draft;
};
struct FrameKey {
    QString document_id;
    std::uint64_t revision{};
    std::int64_t time_ticks{};
    std::uint64_t device_generation{};
    friend bool operator==(const FrameKey&, const FrameKey&) = default;
};
struct BlockDelta {
    qsizetype offset{};
    QByteArray before;
    QByteArray after;
};
[[nodiscard]] BlockDelta block_delta(const QByteArray& before, const QByteArray& after);
[[nodiscard]] QByteArray apply_delta(const QByteArray& source, const BlockDelta& delta,
                                     bool reverse);
// A deterministic time-varying reference node, exclusively for M0 cache testing.
class FrameCache final {
  public:
    [[nodiscard]] double evaluate(const Snapshot& snapshot, std::int64_t time_ticks,
                                  std::uint64_t device_generation);
    [[nodiscard]] std::size_t entry_count() const noexcept;

  private:
    struct Entry {
        FrameKey key;
        double value{};
    };
    std::vector<Entry> entries_;
};
class Document final {
  public:
    explicit Document(State state);
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] Session begin_session() const;
    [[nodiscard]] bool commit(const Session& session);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool dirty() const noexcept;
    void mark_saved(std::uint64_t revision) noexcept;

  private:
    std::vector<State> history_;
    std::size_t cursor_{};
    std::uint64_t revision_{};
    std::uint64_t saved_revision_{};
};
enum class SaveFault { none, before_commit };
struct LoadResult {
    State state;
    QString error;
    bool migrated{};
};
[[nodiscard]] QString save_snapshot(const QString& path, const Snapshot& snapshot,
                                    SaveFault fault = SaveFault::none);
[[nodiscard]] LoadResult load_snapshot(const QString& path);
} // namespace huelith::validation
