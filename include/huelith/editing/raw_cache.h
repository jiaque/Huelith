#pragma once
#include "huelith/editing/raw_develop.h"
#include <QTemporaryDir>
#include <list>
#include <optional>
#include <functional>

namespace huelith::editing {
// Session-owned, GUI-thread-only cache. Disk spill preserves float pixels exactly.
class RawCache final {
  public:
    explicit RawCache(qint64 memory_budget = 512LL * 1024 * 1024,
                      qint64 disk_budget = 4LL * 1024 * 1024 * 1024);
    [[nodiscard]] bool contains(const QString& key) const;
    [[nodiscard]] std::optional<RawResult> get(const QString& key);
    // Capture on the owner thread; execute elsewhere without accessing cache state.
    // The cache directory must remain alive until the returned task finishes.
    [[nodiscard]] std::function<RawResult()> read_task(const QString& key) const;
    void put(const QString& key, const RawResult& result);
    void configure(qint64 memory_budget, qint64 disk_budget);
    void clear();
    [[nodiscard]] qint64 memory_bytes() const noexcept;
    [[nodiscard]] qint64 disk_bytes() const noexcept;

  private:
    struct Entry {
        QString key;
        QString path;
        RawResult result;
        QSize size;
        QImage::Format format;
        QColorSpace color_space;
        qint64 bytes;
        bool has_reference{};
    };
    void trim_memory();
    QTemporaryDir directory_;
    std::list<Entry> entries_;
    qint64 memory_budget_;
    qint64 disk_budget_;
    qint64 memory_bytes_{};
    qint64 disk_bytes_{};
};
[[nodiscard]] QString raw_cache_key(const QByteArray& source, const RawOptions& options);
} // namespace huelith::editing
