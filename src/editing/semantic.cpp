#include "huelith/editing/semantic.h"
#include "huelith/editing/ai_worker.h"
#include "huelith/editing/project.h"
#include <QCryptographicHash>
#include <QFile>
#include <algorithm>
#include <array>
#include <chrono>
#include <onnxruntime_cxx_api.h>
#include <thread>

namespace huelith::editing {
namespace {
void check(const std::atomic_bool& cancelled) {
    if (cancelled.load())
        throw EditError(QStringLiteral("范围识别已取消"));
}
bool matches(int label, SemanticTarget target) {
    switch (target) {
    case SemanticTarget::sky:
        return label == 2;
    case SemanticTarget::grass:
        return label == 9;
    case SemanticTarget::vegetation:
        return label == 4 || label == 9 || label == 17 || label == 29 || label == 66 || label == 72;
    case SemanticTarget::people:
        return label == 12 || label == 92;
    case SemanticTarget::subject:
    case SemanticTarget::background:
    case SemanticTarget::object:
        return false;
    }
    return false;
}
} // namespace
QImage semantic_labels(const QImage& image, const QString& model,
                       const std::atomic_bool& cancelled) {
    check(cancelled);
    if (image.isNull())
        throw EditError(QStringLiteral("没有可识别的图像"));
    if (!is_ai_worker())
        return run_ai_job({{"operation", "semantic"}, {"model", model}, {"directml", false}},
            image.format() == QImage::Format_RGBA32FPx4_Premultiplied ? image : image.convertToFormat(QImage::Format_RGBA8888),
            &cancelled).image;
    QFile file(model);
    if (!file.open(QIODevice::ReadOnly))
        throw EditError(QStringLiteral("内置场景模型缺失：") + model);
    if (file.size() != 24'204'118)
        throw EditError(QStringLiteral("场景模型文件大小无效"));
    const auto bytes = file.readAll();
    // Bound the packaged model contract: arbitrary graphs are not accepted here.
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    if (hash != "367115635ad3f5ffba4cb0244e4ce93bb36bcff1ece97fca8728ffb952c32695")
        throw EditError(QStringLiteral("场景模型校验失败，请恢复完整模型文件"));
    auto rgb =
        image.format() == QImage::Format_RGBA32FPx4_Premultiplied ? display_image(image) : image;
    rgb = rgb.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888);
    std::vector<float> input(3 * 512 * 512);
    constexpr std::array<float, 3> mean{.485F, .456F, .406F}, deviation{.229F, .224F, .225F};
    for (int y = 0; y < 512; ++y) {
        check(cancelled);
        const auto* row = rgb.constScanLine(std::min(y, rgb.height() - 1));
        for (int x = 0; x < 512; ++x) {
            const auto* p = row + 4 * std::min(x, rgb.width() - 1);
            const float alpha = p[3] / 255.F;
            for (int c = 0; c < 3; ++c)
                input[std::size_t(c) * 512 * 512 + y * 512 + x] =
                    ((p[c] / 255.F * alpha + .5F * (1 - alpha)) - mean[c]) / deviation[c];
        }
    }
    try {
        static Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "HuelithSemantic");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(4);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        Ort::Session session(environment, bytes.constData(), std::size_t(bytes.size()), options);
        if (session.GetInputCount() != 1 || session.GetOutputCount() != 1)
            throw EditError(QStringLiteral("场景模型接口无效"));
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> shape{1, 3, 512, 512};
        auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                      shape.data(), shape.size());
        const char* inputs[]{"x"};
        const char* outputs[]{"labels"};
        Ort::RunOptions run;
        std::jthread interrupt([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                if (cancelled.load()) {
                    run.SetTerminate();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        auto output = session.Run(run, inputs, &tensor, 1, outputs, 1);
        interrupt.request_stop();
        check(cancelled);
        const auto info = output.front().GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
            info.GetShape() != std::vector<int64_t>{1, 512, 512})
            throw EditError(QStringLiteral("场景模型输出无效"));
        const auto* values = output.front().GetTensorData<int64_t>();
        QImage labels(rgb.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < labels.height(); ++y) {
            check(cancelled);
            for (int x = 0; x < labels.width(); ++x) {
                const auto value = values[y * 512 + x];
                if (value < 0 || value >= 150)
                    throw EditError(QStringLiteral("场景模型输出类别越界"));
                labels.scanLine(y)[x] = uchar(value);
            }
        }
        return labels;
    } catch (const Ort::Exception& error) {
        check(cancelled);
        throw EditError(QStringLiteral("场景识别失败：") + QString::fromUtf8(error.what()));
    }
}
QImage semantic_mask(const QImage& labels, SemanticTarget target, QSize output_size) {
    if (labels.isNull() || labels.format() != QImage::Format_Grayscale8 || output_size.isEmpty() ||
        qint64(output_size.width()) * output_size.height() > 128'000'000)
        throw EditError(QStringLiteral("场景蒙版尺寸无效"));
    QImage mask(labels.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < labels.height(); ++y)
        for (int x = 0; x < labels.width(); ++x)
            mask.scanLine(y)[x] = matches(labels.constScanLine(y)[x], target) ? 255 : 0;
    return mask.scaled(output_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
std::vector<SemanticComponent> semantic_people(const QImage& labels) {
    auto mask = semantic_mask(labels, SemanticTarget::people, labels.size());
    const int w = mask.width(), h = mask.height();
    std::vector<SemanticComponent> result;
    std::vector<QPoint> queue;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (mask.constScanLine(y)[x] == 0)
                continue;
            queue.clear();
            queue.emplace_back(x, y);
            mask.scanLine(y)[x] = 0;
            QRect bounds(x, y, 1, 1);
            for (std::size_t i = 0; i < queue.size(); ++i) {
                const auto p = queue[i];
                bounds = bounds.united(QRect(p, QSize(1, 1)));
                for (int yy = std::max(0, p.y() - 1); yy <= std::min(h - 1, p.y() + 1); ++yy)
                    for (int xx = std::max(0, p.x() - 1); xx <= std::min(w - 1, p.x() + 1); ++xx)
                        if (mask.constScanLine(yy)[xx]) {
                            mask.scanLine(yy)[xx] = 0;
                            queue.emplace_back(xx, yy);
                        }
            }
            if (queue.size() < std::size_t(std::max(32, w * h / 2000)))
                continue;
            QImage component(labels.size(), QImage::Format_Grayscale8);
            component.fill(0);
            for (const auto p : queue)
                component.scanLine(p.y())[p.x()] = 255;
            result.push_back({std::move(component), bounds, int(queue.size())});
        }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.pixels > b.pixels; });
    return result;
}
} // namespace huelith::editing
