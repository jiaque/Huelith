#include "huelith/editing/image_storage.h"
#include "huelith/editing/enhancement.h"
#include "huelith/editing/project.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace huelith::editing {
QString enhancement_model(const EnhanceOptions& options) {
    if (!options.model.isEmpty()) return options.model;
    auto directory = qEnvironmentVariable("HUELITH_AI_MODEL_DIR");
    if (directory.isEmpty()) directory = QCoreApplication::applicationDirPath() + "/models";
    const auto name = options.mode == EnhanceMode::denoise ? "nafnet-sidd.onnx" :
                      options.mode == EnhanceMode::depth ? "midas-small.onnx" :
                      options.quality ? "realesrgan-x4plus.onnx" : "realesr-general-x4v3.onnx";
    return QDir(directory).filePath(name);
}
AiResult enhance_image(const QImage& image, const EnhanceOptions& options,
                       const std::atomic_bool* cancelled, const AiProgress& progress) {
    if (image.isNull() || !std::isfinite(options.strength) || options.strength < 0 || options.strength > 1 ||
        (options.mode == EnhanceMode::super_resolution && options.scale != 2 && options.scale != 4))
        throw EditError(QStringLiteral("增强参数无效"));
    if (options.mode == EnhanceMode::denoise && options.strength == 0)
        return {image, {{"bypassed", true}}, {}};
    const int factor = options.mode == EnhanceMode::super_resolution ? options.scale : 1;
    if (qint64(image.width()) * image.height() * factor * factor > huelith::editing::maximum_image_pixels)
        throw EditError(QStringLiteral("结果为 %1 × %2，超过当前工程的 5.12 亿像素上限。请选择较小倍数或先裁剪。")
            .arg(image.width() * factor).arg(image.height() * factor));
    return run_ai_job({{"operation", "enhance"}, {"mode", int(options.mode)}, {"scale", options.scale},
                       {"strength", options.strength}, {"directml", options.directml},
                       {"quality", options.quality}, {"model", enhancement_model(options)}},
                      to_working(image), cancelled, progress);
}
AiResult infer_enhancement(const QImage& source, const EnhanceOptions& options, const AiProgress& progress) {
    const auto model = enhancement_model(options);
    const auto name = QFileInfo(model).fileName();
    const QByteArray expected = name == "nafnet-sidd.onnx" ? "a455857d8490083b7ede87cd48ae5a56531a14701f7607565be9efd766e4664d" :
        name == "realesrgan-x4plus.onnx" ? "6b55a4507a1fefddc1fdc5d4a65dd80d62fa029a303455c935c223cae08dcdd0" :
        name == "realesr-general-x4v3.onnx" ? "abd38ff81ca7cd9e03e213394267c50d393c8021b64f2f57b818f1ec82ad2369" :
        name == "midas-small.onnx" ? "2d8c6cb8f415229daf1eb041024208e2608c9f98e17c81cc7c6ecb449c56fd58" : "";
    QFile file(model);
    if (expected.isEmpty() || !file.open(QIODevice::ReadOnly))
        throw EditError(QStringLiteral("内置增强模型缺失：%1").arg(name));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    if (hash.result().toHex() != expected)
        throw EditError(QStringLiteral("增强模型校验失败，请重新安装内置模型"));
    const bool depth = options.mode == EnhanceMode::depth;
    const int scale = options.mode == EnhanceMode::super_resolution ? options.scale : 1;
    const int native = options.mode == EnhanceMode::super_resolution ? 4 : 1;
    // Bound full-image storage before multiplying integer dimensions or allocating model tensors.
    if (qint64(source.width()) * source.height() * scale * scale > huelith::editing::maximum_image_pixels)
        throw EditError(QStringLiteral("增强结果超过当前工程的 5.12 亿像素上限"));
    const auto rgb = convert_image(source, QColorSpace::SRgb, QImage::Format_RGBA32FPx4);
    if (rgb.isNull()) throw EditError(QStringLiteral("增强颜色空间转换失败"));
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "HuelithEnhance");
    Ort::SessionOptions settings;
    settings.SetIntraOpNumThreads(4);
    settings.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    settings.DisableMemPattern();
    if (options.directml) {
        const OrtDmlApi* dml{};
        Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml)));
        Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(settings, 0));
    }
    Ort::Session session(env, model.toStdWString().c_str(), settings);
    if (session.GetInputCount() != 1 || session.GetOutputCount() != 1)
        throw EditError(QStringLiteral("增强模型输入输出数量不匹配"));
    Ort::AllocatorWithDefaultOptions allocator;
    const auto input_name = session.GetInputNameAllocated(0, allocator);
    const auto output_name = session.GetOutputNameAllocated(0, allocator);
    const char* inputs[]{input_name.get()};
    const char* outputs[]{output_name.get()};
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto run = [&](std::vector<float>& values, int w, int h) {
        const std::array<int64_t, 4> shape{1, 3, h, w};
        auto tensor = Ort::Value::CreateTensor<float>(memory, values.data(), values.size(), shape.data(), shape.size());
        return session.Run(Ort::RunOptions{}, inputs, &tensor, 1, outputs, 1);
    };
    QJsonObject metadata{{"model", name}, {"sha256", QString::fromLatin1(expected)},
                          {"provider", options.directml ? "DirectML" : "CPU"}, {"scale", scale}};
    if (depth) {
        const auto depth_input = rgb.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        std::vector<float> values(3 * 256 * 256);
        constexpr std::array<float, 3> mean{.485F, .456F, .406F}, deviation{.229F, .224F, .225F};
        for (int y = 0; y < 256; ++y) {
            const auto* row = reinterpret_cast<const float*>(depth_input.constScanLine(y));
            for (int x = 0; x < 256; ++x)
                for (int c = 0; c < 3; ++c)
                    values[std::size_t(c) * 65536 + y * 256 + x] = (std::clamp(row[x * 4 + c], 0.F, 1.F) - mean[c]) / deviation[c];
        }
        auto results = run(values, 256, 256);
        const auto info = results[0].GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || info.GetElementCount() != 65536)
            throw EditError(QStringLiteral("深度模型输出不兼容"));
        const auto* data = results[0].GetTensorData<float>();
        float low = data[0], high = data[0];
        for (int i = 0; i < 65536; ++i) {
            if (!std::isfinite(data[i])) throw EditError(QStringLiteral("深度模型输出无效"));
            low = std::min(low, data[i]); high = std::max(high, data[i]);
        }
        QImage mask(256, 256, QImage::Format_Grayscale8);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                mask.scanLine(y)[x] = uchar(std::clamp((data[y * 256 + x] - low) / std::max(1e-6F, high - low), 0.F, 1.F) * 255);
        if (progress) progress(1, 1);
        metadata["relativeDepth"] = true;
        return {mask.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation), metadata, {}};
    }
    // Overlapping cores with halo avoid exposing the convolution boundary as a seam.
    constexpr int core = 128, overlap = 32, halo = 32, tile = core + 2 * halo;
    const int ow = rgb.width() * scale, oh = rgb.height() * scale;
    auto output = allocate_image(ow, oh, QImage::Format_RGBA32FPx4);
    if (output.isNull()) throw EditError(QStringLiteral("增强结果内存不足"));
    output.fill(Qt::transparent);
    output.setColorSpace(QColorSpace::SRgb);
    std::vector<float> values(std::size_t(tile) * tile * 3);
    const int step = core - overlap;
    const int total = ((rgb.width() + step - 1) / step) * ((rgb.height() + step - 1) / step);
    int done{};
    for (int ty = 0; ty < rgb.height(); ty += step)
        for (int tx = 0; tx < rgb.width(); tx += step) {
            for (int y = 0; y < tile; ++y) {
                const auto* row = reinterpret_cast<const float*>(rgb.constScanLine(std::clamp(ty + y - halo, 0, rgb.height() - 1)));
                for (int x = 0; x < tile; ++x)
                    for (int c = 0; c < 3; ++c) {
                        const auto value = row[std::clamp(tx + x - halo, 0, rgb.width() - 1) * 4 + c];
                        if (!std::isfinite(value)) throw EditError(QStringLiteral("增强输入包含无效像素"));
                        values[std::size_t(c) * tile * tile + y * tile + x] = std::clamp(value, 0.F, 1.F);
                    }
            }
            auto results = run(values, tile, tile);
            const auto info = results[0].GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                info.GetShape() != std::vector<int64_t>{1, 3, tile * native, tile * native})
                throw EditError(QStringLiteral("增强模型输出尺寸不兼容"));
            const auto* data = results[0].GetTensorData<float>();
            const int width = std::min(core, rgb.width() - tx) * scale;
            const int height = std::min(core, rgb.height() - ty) * scale;
            const int ratio = native / scale;
            for (int y = 0; y < height; ++y) {
                auto* row = reinterpret_cast<float*>(output.scanLine(ty * scale + y));
                for (int x = 0; x < width; ++x) {
                    auto ramp = [&](int p, int length, int origin, int limit) {
                        float weight = 1.F;
                        if (origin > 0 && p < overlap * scale) weight *= float(p + 1) / (overlap * scale + 1);
                        if (origin + length < limit && p >= length - overlap * scale) weight *= float(length - p) / (overlap * scale + 1);
                        return weight;
                    };
                    const float weight = ramp(x, width, tx * scale, ow) * ramp(y, height, ty * scale, oh);
                    row[(tx * scale + x) * 4 + 3] += weight;
                    for (int c = 0; c < 3; ++c) {
                        float sum{};
                        for (int yy = 0; yy < ratio; ++yy)
                            for (int xx = 0; xx < ratio; ++xx)
                                sum += data[std::size_t(c) * tile * tile * native * native +
                                            (halo * native + y * ratio + yy) * tile * native + halo * native + x * ratio + xx];
                        if (!std::isfinite(sum)) throw EditError(QStringLiteral("增强模型输出包含无效像素"));
                        row[(tx * scale + x) * 4 + c] += weight * sum / (ratio * ratio);
                    }
                }
            }
            if (progress) progress(++done, total);
        }
    for (int y = 0; y < oh; ++y) {
        auto* row = reinterpret_cast<float*>(output.scanLine(y));
        const double sy=std::clamp((y+.5)/scale-.5,0.,double(rgb.height()-1));
        const int y0=int(sy), y1=std::min(y0+1,rgb.height()-1);
        const auto* a=reinterpret_cast<const float*>(rgb.constScanLine(y0));
        const auto* b=reinterpret_cast<const float*>(rgb.constScanLine(y1));
        for (int x = 0; x < ow; ++x) {
            const float weight = row[x*4+3];
            const double sx=std::clamp((x+.5)/scale-.5,0.,double(rgb.width()-1));
            const int x0=int(sx),x1=std::min(x0+1,rgb.width()-1);
            float original[4];
            for(int c=0;c<4;++c) original[c]=float((1-(sy-y0))*((1-(sx-x0))*a[x0*4+c]+(sx-x0)*a[x1*4+c])+
                                                        (sy-y0)*((1-(sx-x0))*b[x0*4+c]+(sx-x0)*b[x1*4+c]));
            for (int c = 0; c < 3; ++c) {
                const float base = original[c];
                // The network processes SDR; retain out-of-range residuals rather than clipping the document.
                const float value = std::clamp(row[x * 4 + c] / weight, 0.F, 1.F) + base - std::clamp(base, 0.F, 1.F);
                row[x * 4 + c] = base + float(options.strength) * (value - base);
            }
            row[x * 4 + 3] = original[3];
        }
    }
    return {to_working(std::move(output)), metadata, {}};
}
} // namespace huelith::editing
