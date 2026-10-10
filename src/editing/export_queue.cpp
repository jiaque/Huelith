#include "huelith/editing/export_queue.h"
#include "huelith/editing/tiff_io.h"
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QLockFile>
#include <QPainter>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>

namespace huelith::editing {
struct ExportQueue::Job {
    Snapshot snapshot;
    ExportOptions options;
    ExportItem item;
    std::atomic_bool cancelled{};
};
namespace {
bool terminal(ExportStatus state) {
    return state == ExportStatus::complete || state == ExportStatus::failed ||
           state == ExportStatus::cancelled || state == ExportStatus::skipped;
}
QString normalized_path(const QString& path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical)
        .toCaseFolded();
}
bool protected_path(const ExportOptions& options, const QString& path) {
    const auto normalized = normalized_path(path);
    return std::any_of(options.protected_paths.begin(), options.protected_paths.end(),
                       [&](const auto& source) { return normalized_path(source) == normalized; });
}
void collect_sources(const std::vector<Layer>& layers, QStringList& paths) {
    for (const auto& layer : layers) {
        if (!layer.linked_path.isEmpty())
            paths.append(layer.linked_path);
        collect_sources(layer.children, paths);
    }
}
} // namespace
ExportQueue::ExportQueue() : worker_([this] { run(); }) {}
ExportQueue::~ExportQueue() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        for (const auto& job : jobs_)
            job->cancelled = true;
    }
    ready_.notify_one();
    worker_.join();
}
quint64 ExportQueue::add(Snapshot snapshot, ExportOptions options) {
    validate_state(snapshot.state);
    if (options.size == QSize() || options.size == QSize(0, 0))
        options.size = snapshot.state.size;
    if (options.size.isEmpty())
        throw EditError(QStringLiteral("导出尺寸无效"));
    if (options.path.isEmpty() ||
        qint64(options.size.width()) * options.size.height() > 64'000'000 ||
        options.size.width() > 65536 || options.size.height() > 65536 ||
        (options.format != "png" && options.format != "jpg" && options.format != "tif") ||
        (options.bits != 8 && options.bits != 16) ||
        (options.format == "jpg" && options.bits != 8) || !options.space.isValid() ||
        options.quality < 1 || options.quality > 100)
        throw EditError(QStringLiteral("导出路径、尺寸、格式或位深无效"));
    collect_sources(snapshot.state.layers, options.protected_paths);
    auto job = std::make_shared<Job>();
    job->item.document = snapshot.state.name;
    job->item.revision = snapshot.revision;
    job->item.path = QFileInfo(options.path).absoluteFilePath();
    job->snapshot = std::move(snapshot);
    job->options = std::move(options);
    const std::lock_guard lock(mutex_);
    job->item.id = next_id_++;
    jobs_.push_back(job);
    ready_.notify_one();
    return job->item.id;
}
void ExportQueue::start() {
    const std::lock_guard lock(mutex_);
    running_ = true;
    ready_.notify_one();
}
void ExportQueue::cancel(quint64 id) {
    const std::lock_guard lock(mutex_);
    for (const auto& job : jobs_)
        if (job->item.id == id && !terminal(job->item.status)) {
            job->cancelled = true;
            if (job->item.status == ExportStatus::queued) {
                job->item.status = ExportStatus::cancelled;
                job->snapshot = {};
            }
        }
}
void ExportQueue::cancel_all() {
    const std::lock_guard lock(mutex_);
    for (const auto& job : jobs_)
        if (!terminal(job->item.status)) {
            job->cancelled = true;
            if (job->item.status == ExportStatus::queued) {
                job->item.status = ExportStatus::cancelled;
                job->snapshot = {};
            }
        }
}
void ExportQueue::retry_failed() {
    const std::lock_guard lock(mutex_);
    for (const auto& job : jobs_)
        if (job->item.status == ExportStatus::failed) {
            job->cancelled = false;
            job->item.status = ExportStatus::queued;
            job->item.error.clear();
        }
    running_ = true;
    ready_.notify_one();
}
std::vector<ExportItem> ExportQueue::items() const {
    const std::lock_guard lock(mutex_);
    std::vector<ExportItem> result;
    for (const auto& job : jobs_)
        result.push_back(job->item);
    return result;
}
void ExportQueue::status(const std::shared_ptr<Job>& job, ExportStatus value, QString error) {
    const std::lock_guard lock(mutex_);
    job->item.status = value;
    job->item.error = std::move(error);
    if (terminal(value) && value != ExportStatus::failed)
        job->snapshot = {};
}
void ExportQueue::run() {
    while (true) {
        std::shared_ptr<Job> next;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] {
                return stopping_ ||
                       (running_ && std::any_of(jobs_.begin(), jobs_.end(), [](const auto& job) {
                            return job->item.status == ExportStatus::queued;
                        }));
            });
            if (stopping_)
                return;
            for (const auto& job : jobs_)
                if (job->item.status == ExportStatus::queued) {
                    next = job;
                    next->item.status = ExportStatus::rendering;
                    break;
                }
        }
        try {
            process(next);
        } catch (const std::exception& error) {
            status(next, next->cancelled ? ExportStatus::cancelled : ExportStatus::failed,
                   QString::fromUtf8(error.what()));
        }
    }
}
void ExportQueue::process(const std::shared_ptr<Job>& job) {
    const auto& options = job->options;
    const auto check_cancel = [&] {
        if (job->cancelled)
            throw EditError(QStringLiteral("任务已取消"));
    };
    check_cancel();
    QString target = QFileInfo(options.path).absoluteFilePath();
    const QFileInfo original(target);
    if (!original.dir().exists())
        throw EditError(QStringLiteral("输出目录不存在"));
    // Serialize path allocation across cooperating Huelith queues/processes.
    QLockFile lock(original.dir().filePath(".huelith-export.lock"));
    if (!lock.tryLock(0))
        throw EditError(QStringLiteral("输出目录正在被其他导出任务使用，可稍后重试"));
    if (options.conflict == ExportConflict::number) {
        for (int index = 1; QFileInfo::exists(target) || protected_path(options, target); ++index) {
            if (index > 100000)
                throw EditError(QStringLiteral("同名文件数量超限"));
            target = original.dir().filePath(original.completeBaseName() + "_" +
                                             QString::number(index) + "." + original.suffix());
        }
    } else if (protected_path(options, target))
        throw EditError(QStringLiteral("禁止覆盖原始素材或工程文件"));
    else if (QFileInfo::exists(target) && options.conflict == ExportConflict::skip) {
        status(job, ExportStatus::skipped);
        return;
    }
    {
        const std::lock_guard guard(mutex_);
        job->item.path = target;
    }
    auto image = render(job->snapshot.state);
    check_cancel();
    // Resample in scene-linear float before encoding the requested output profile/bit depth.
    image = image.scaled(options.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertedToColorSpace(options.space, options.bits == 16 ? QImage::Format_RGBA64
                                                                         : QImage::Format_RGBA8888);
    if (image.isNull())
        throw EditError(QStringLiteral("输出色彩转换或像素分配失败"));
    image.setColorSpace(options.space);
    if (!options.alpha || options.format == "jpg") {
        QImage filled(image.size(), image.format());
        filled.setColorSpace(options.space);
        QImage swatch(1, 1, QImage::Format_RGBA8888);
        swatch.setColorSpace(QColorSpace::SRgb);
        swatch.fill(options.matte);
        filled.fill(swatch.convertedToColorSpace(options.space).pixelColor(0, 0));
        QPainter painter(&filled);
        painter.drawImage(0, 0, image);
        painter.end();
        image = filled;
    }
    const int dpm = qRound(job->snapshot.state.ppi / .0254);
    image.setDotsPerMeterX(dpm);
    image.setDotsPerMeterY(dpm);
    check_cancel();
    status(job, ExportStatus::writing);
    QTemporaryDir temporary(original.dir().filePath(".huelith-export-XXXXXX"));
    if (!temporary.isValid())
        throw EditError(QStringLiteral("无法创建原子导出临时目录"));
    const auto staged = temporary.filePath("output." + options.format);
    if (options.format == "tif")
        write_tiff(image, staged, options.bits, job->cancelled, job->snapshot.state.ppi);
    else {
        QImageWriter writer(staged, options.format);
        writer.setQuality(options.quality);
        if (!writer.write(image))
            throw EditError(writer.errorString());
    }
    check_cancel();
    if (options.conflict == ExportConflict::overwrite) {
        // QSaveFile commits atomically; it never removes the existing file first.
        QFile input(staged);
        QSaveFile output(target);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            throw EditError(QStringLiteral("无法打开导出目标"));
        while (!input.atEnd()) {
            check_cancel();
            const auto bytes = input.read(1024 * 1024);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                throw EditError(QStringLiteral("导出写入失败"));
        }
        check_cancel();
        if (!output.commit())
            throw EditError(output.errorString());
    } else if (!QFile::rename(staged, target))
        throw EditError(QStringLiteral("输出文件已被占用或同名文件刚被创建，请重试"));
    status(job, ExportStatus::complete);
}
} // namespace huelith::editing
