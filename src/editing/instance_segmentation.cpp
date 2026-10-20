#include "huelith/editing/project.h"
#include "huelith/editing/semantic.h"
#include "huelith/editing/ai_worker.h"
#include <QJsonArray>
#include <QCryptographicHash>
#include <QFile>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <onnxruntime_cxx_api.h>
#include <thread>

namespace huelith::editing {
namespace {
constexpr int extent = 800;
void check(const std::atomic_bool& cancelled) {
    if (cancelled.load())
        throw EditError(QStringLiteral("对象识别已取消"));
}
QImage paste_mask(const float* probabilities, const float* box, QSize size) {
    // Torchvision: one-pixel mask padding, integer expanded box, bilinear
    // interpolation with align_corners=false, then clipping to the source.
    constexpr int padded = 30;
    const double cx = (box[0] + box[2]) * .5, cy = (box[1] + box[3]) * .5;
    const double rx = (box[2] - box[0]) * .5 * padded / 28;
    const double ry = (box[3] - box[1]) * .5 * padded / 28;
    const int left = int(cx - rx), top = int(cy - ry);
    const int right = int(cx + rx), bottom = int(cy + ry);
    const int width = std::max(1, right - left + 1), height = std::max(1, bottom - top + 1);
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    const auto value = [probabilities](int x, int y) -> double {
        return x > 0 && x < 29 && y > 0 && y < 29 ? probabilities[(y - 1) * 28 + x - 1] : 0.;
    };
    for (int y = std::max(0, top); y < std::min(size.height(), bottom + 1); ++y) {
        auto* row = mask.scanLine(y);
        const double py = std::clamp((y - top + .5) * padded / height - .5, 0., 29.);
        const int y0 = int(py), y1 = std::min(y0 + 1, 29);
        const double fy = py - y0;
        for (int x = std::max(0, left); x < std::min(size.width(), right + 1); ++x) {
            const double px = std::clamp((x - left + .5) * padded / width - .5, 0., 29.);
            const int x0 = int(px), x1 = std::min(x0 + 1, 29);
            const double fx = px - x0;
            row[x] =
                uchar(std::clamp(qRound(std::lerp(std::lerp(value(x0, y0), value(x1, y0), fx),
                                                  std::lerp(value(x0, y1), value(x1, y1), fx), fy) *
                                        255),
                                 0, 255));
        }
    }
    return mask;
}
} // namespace

std::vector<SemanticInstance> semantic_instances(const QImage& image, const QString& model,
                                                 const std::atomic_bool& cancelled) {
    check(cancelled);
    if (image.isNull())
        throw EditError(QStringLiteral("没有可识别的图像"));
    if (!is_ai_worker()) {
        const auto result = run_ai_job({{"operation", "instances"}, {"model", model}, {"directml", false}},
            image.format() == QImage::Format_RGBA32FPx4_Premultiplied ? image : image.convertToFormat(QImage::Format_RGBA8888), &cancelled);
        std::vector<SemanticInstance> instances;
        const int height = result.metadata["height"].toInt();
        int index{};
        for (const auto& record : result.metadata["instances"].toArray()) {
            const auto item = record.toObject();
            const auto bounds = item["bounds"].toArray();
            instances.push_back({result.image.copy(0, index++ * height, result.image.width(), height),
                QRect(bounds[0].toInt(), bounds[1].toInt(), bounds[2].toInt(), bounds[3].toInt()),
                item["category"].toInt(), float(item["confidence"].toDouble()), item["pixels"].toInt()});
        }
        return instances;
    }
    QFile file(model);
    if (!file.open(QIODevice::ReadOnly))
        throw EditError(QStringLiteral("内置对象模型缺失：") + model);
    if (file.size() < 100'000'000 || file.size() > 250'000'000)
        throw EditError(QStringLiteral("对象模型文件大小无效"));
    const auto bytes = file.readAll();
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
        "19455030e0a5e56e80476bdc5a3dcdf0ced79a5871ddc34fd3abee600e2ead76")
        throw EditError(QStringLiteral("对象模型校验失败，请恢复完整模型文件"));
    check(cancelled);
    auto rgb =
        image.format() == QImage::Format_RGBA32FPx4_Premultiplied ? display_image(image) : image;
    rgb = rgb.scaled(extent, extent, Qt::KeepAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888);
    std::vector<float> input(3 * extent * extent, 128.F / 255.F);
    for (int y = 0; y < rgb.height(); ++y) {
        check(cancelled);
        const auto* row = rgb.constScanLine(y);
        for (int x = 0; x < rgb.width(); ++x) {
            const float alpha = row[x * 4 + 3] / 255.F;
            for (int c = 0; c < 3; ++c)
                input[std::size_t(c) * extent * extent + y * extent + x] =
                    row[x * 4 + c] / 255.F * alpha + (128.F / 255.F) * (1 - alpha);
        }
    }
    try {
        static Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "HuelithInstances");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(4);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        Ort::Session session(environment, bytes.constData(), std::size_t(bytes.size()), options);
        if (session.GetInputCount() != 1 || session.GetOutputCount() != 4)
            throw EditError(QStringLiteral("对象模型接口无效"));
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> shape{1, 3, extent, extent};
        auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                      shape.data(), shape.size());
        const char* inputs[]{"image"};
        const char* outputs[]{"boxes", "labels", "scores", "masks"};
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
        auto result = session.Run(run, inputs, &tensor, 1, outputs, 4);
        interrupt.request_stop();
        check(cancelled);
        const auto box_info = result[0].GetTensorTypeAndShapeInfo();
        const auto box_shape = box_info.GetShape();
        if (box_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            box_shape.size() != 2 || box_shape[1] != 4 || box_shape[0] < 0 || box_shape[0] > 50)
            throw EditError(QStringLiteral("对象模型边界输出无效"));
        const auto n = box_shape[0];
        for (int i = 1; i < 4; ++i) {
            const auto info = result[i].GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != (i == 1 ? ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
                                                 : ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) ||
                info.GetShape() !=
                    (i == 3 ? std::vector<int64_t>{n, 1, 28, 28} : std::vector<int64_t>{n}))
                throw EditError(QStringLiteral("对象模型蒙版输出无效"));
        }
        const auto* boxes = result[0].GetTensorData<float>();
        const auto* labels = result[1].GetTensorData<int64_t>();
        const auto* scores = result[2].GetTensorData<float>();
        const auto* masks = result[3].GetTensorData<float>();
        std::vector<SemanticInstance> instances;
        for (int64_t i = 0; i < n; ++i) {
            check(cancelled);
            const auto* box = boxes + i * 4;
            if (labels[i] < 1 || labels[i] > 90 || !std::isfinite(scores[i]) || scores[i] < 0 ||
                scores[i] > 1 ||
                !std::all_of(box, box + 4,
                             [](float x) { return std::isfinite(x) && x >= 0 && x <= extent; }) ||
                box[2] < box[0] || box[3] < box[1])
                throw EditError(QStringLiteral("对象模型输出数值无效"));
            const auto* values = masks + i * 28 * 28;
            if (!std::all_of(values, values + 28 * 28,
                             [](float x) { return std::isfinite(x) && x >= 0 && x <= 1; }))
                throw EditError(QStringLiteral("对象模型蒙版数值无效"));
            if (scores[i] < .5F)
                continue;
            auto mask = paste_mask(values, box, rgb.size());
            QRect bounds;
            int pixels{};
            for (int y = 0; y < mask.height(); ++y) {
                const auto* row = mask.constScanLine(y);
                for (int x = 0; x < mask.width(); ++x)
                    if (row[x] > 127) {
                        ++pixels;
                        bounds = bounds.united(QRect(x, y, 1, 1));
                    }
            }
            if (pixels >= 16)
                instances.push_back({std::move(mask), bounds, int(labels[i]), scores[i], pixels});
        }
        std::stable_sort(instances.begin(), instances.end(),
                         [](const auto& a, const auto& b) { return a.pixels > b.pixels; });
        return instances;
    } catch (const Ort::Exception& error) {
        check(cancelled);
        throw EditError(QStringLiteral("对象识别失败：") + QString::fromUtf8(error.what()));
    }
}

QString semantic_category_name(int category) {
    static const auto names =
        QStringLiteral(
            "背景,人物,自行车,汽车,摩托车,飞机,巴士,火车,卡车,船,交通灯,消防栓,,停车标志,"
            "停车计时器,长椅,鸟,猫,狗,马,羊,牛,大象,熊,斑马,长颈鹿,,背包,雨伞,,,手提包,领带,行李箱,"
            "飞盘,滑雪板,单板滑雪板,球,风筝,棒球棒,棒球手套,滑板,冲浪板,网球拍,瓶子,,酒杯,杯子,"
            "叉子,刀,勺子,碗,香蕉,苹果,三明治,橙子,西兰花,胡萝卜,热狗,披萨,甜甜圈,蛋糕,椅子,沙发,"
            "盆栽,床,,餐桌,,,马桶,,电视,笔记本电脑,鼠标,遥控器,键盘,手机,微波炉,烤箱,烤面包机,水槽,"
            "冰箱,,书,时钟,花瓶,剪刀,玩具熊,吹风机,牙刷")
            .split(',');
    return category > 0 && category < names.size() && !names[category].isEmpty()
               ? names[category]
               : QStringLiteral("对象");
}
} // namespace huelith::editing
