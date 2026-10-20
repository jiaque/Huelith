#pragma once
#include <QImage>
#include <QJsonObject>
#include <atomic>
#include <functional>

namespace huelith::editing {
using AiProgress = std::function<void(int, int)>;
struct AiResult { QImage image; QJsonObject metadata; QImage reference; };
// Each invocation owns its worker and private exchange directory. No document is mutated.
AiResult run_ai_job(QJsonObject request, const QImage& image,
                    const std::atomic_bool* cancelled = nullptr, const AiProgress& progress = {});
bool is_ai_worker();
int ai_worker_main(const QString& job_path);
void write_ai_image(const QString& path, const QImage& image);
QImage read_ai_image(const QString& path);
} // namespace huelith::editing
