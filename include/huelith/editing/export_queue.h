#pragma once
#include "huelith/editing/project.h"
#include "huelith/editing/metadata.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace huelith::editing {
enum class ExportStatus { queued, rendering, writing, complete, failed, cancelled, skipped };
enum class ExportConflict { number, skip, overwrite };
struct ExportOptions {
    MetadataPolicy metadata;
    QString path;
    QSize size; // Empty = original dimensions.
    QByteArray format{"png"};
    int bits{8};
    int quality{95};
    QColorSpace space{QColorSpace::SRgb};
    bool alpha{true};
    QColor matte{Qt::white};
    ExportConflict conflict{ExportConflict::number};
    QStringList protected_paths; // Original files and project files must never be replaced.
};
struct ExportItem {
    quint64 id{};
    quint64 revision{};
    QString document;
    QString path;
    ExportStatus status{ExportStatus::queued};
    QString error;
};
[[nodiscard]] QImage prepare_export_image(const QImage& source, const ExportOptions& options,
                                          double ppi);
// One worker owns rendering/writing; GUI only exchanges value snapshots under the mutex.
class ExportQueue final {
  public:
    ExportQueue();
    ~ExportQueue();
    ExportQueue(const ExportQueue&) = delete;
    ExportQueue& operator=(const ExportQueue&) = delete;
    quint64 add(Snapshot snapshot, ExportOptions options);
    void start();
    void cancel(quint64 id);
    void cancel_all();
    void retry_failed();
    [[nodiscard]] std::vector<ExportItem> items() const;

  private:
    struct Job;
    void run();
    void process(const std::shared_ptr<Job>& job);
    void status(const std::shared_ptr<Job>& job, ExportStatus value, QString error = {});
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<std::shared_ptr<Job>> jobs_;
    quint64 next_id_{1};
    bool running_{};
    bool stopping_{};
    std::thread worker_;
};
} // namespace huelith::editing
