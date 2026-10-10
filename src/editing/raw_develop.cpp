#include "huelith/editing/raw_develop.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <dml_provider_factory.h>
#include <libraw/libraw.h>
#include <numeric>
#include <onnxruntime_cxx_api.h>

namespace huelith::editing {
namespace {
using Matrix = std::array<double, 9>;
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result{};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            for (int k = 0; k < 3; ++k)
                result[y * 3 + x] += a[y * 3 + k] * b[k * 3 + x];
    return result;
}
Matrix inverse(const Matrix& a) {
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
                       a[2] * (a[3] * a[7] - a[4] * a[6]);
    if (!std::isfinite(det) || std::abs(det) < 1e-12)
        throw EditError(QStringLiteral("RAW 相机矩阵不可逆"));
    Matrix result{a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
                  a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
                  a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3]};
    for (auto& value : result)
        value /= det;
    return result;
}
Matrix color_matrix(const libraw_colordata_t& color) {
    const Matrix xyz_srgb{3.2404542, -1.5371385, -.4985314, -.969266, 1.8760108,
                          .041556,   .0556434,   -.2040259, 1.0572252};
    const Matrix xyz_rec{1.716651188, -.355670784, -.253366281, -.666684352, 1.616481237,
                         .015768546,  .017639857,  -.042770613, .942103121};
    Matrix camera{};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            camera[y * 3 + x] = color.cam_xyz[y][x];
    auto normalized = multiply(camera, inverse(xyz_srgb));
    for (int row = 0; row < 3; ++row) {
        const double sum = normalized[row * 3] + normalized[row * 3 + 1] + normalized[row * 3 + 2];
        if (!std::isfinite(sum) || std::abs(sum) < 1e-10)
            throw EditError(QStringLiteral("RAW 中性轴校准无效"));
        for (int col = 0; col < 3; ++col)
            normalized[row * 3 + col] /= sum;
    }
    Matrix white_balance{};
    for (int c = 0; c < 3; ++c) {
        if (!std::isfinite(color.cam_mul[c]) || color.cam_mul[c] <= 0)
            throw EditError(QStringLiteral("RAW 白平衡元数据无效"));
        white_balance[c * 3 + c] = color.cam_mul[c] / color.cam_mul[1];
    }
    return multiply(multiply(xyz_rec, inverse(xyz_srgb)),
                    multiply(inverse(normalized), white_balance));
}
void check_cancel(const std::atomic_bool& cancelled) {
    if (cancelled.load())
        throw EditError(QStringLiteral("RAW 处理已取消"));
}
int reflect(int coordinate, int length) {
    const int period = 2 * (length - 1);
    coordinate = ((coordinate % period) + period) % period;
    return coordinate < length ? coordinate : period - coordinate;
}
} // namespace

RawResult develop_raw(const QString& path, const RawOptions& options,
                      const std::atomic_bool& cancelled, const RawProgress& progress) {
    if (!std::isfinite(options.exposure) || std::abs(options.exposure) > 20)
        throw EditError(QStringLiteral("RAW 曝光参数无效"));
    check_cancel(cancelled);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 512LL * 1024 * 1024)
        throw EditError(QStringLiteral("无法读取 RAW 或源文件超出预算"));
    const auto source_bytes = file.readAll();
    LibRaw decoder;
    const auto check = [](int result) {
        if (result != LIBRAW_SUCCESS)
            throw EditError(QString::fromUtf8(libraw_strerror(result)));
    };
    check(decoder.open_buffer(const_cast<char*>(source_bytes.constData()),
                              std::size_t(source_bytes.size())));
    check(decoder.unpack());
    check_cancel(cancelled);
    const auto& data = decoder.imgdata;
    const int width = data.sizes.width, height = data.sizes.height;
    if (!data.rawdata.raw_image || width < 4 || height < 4 || (width % 2) || (height % 2) ||
        qint64(width) * height > 64'000'000 || data.idata.filters == 9)
        throw EditError(QStringLiteral("当前 RAW 路径需要偶数尺寸 Bayer 传感器"));
    const int pw = width / 2, ph = height / 2;
    const std::size_t plane = std::size_t(pw) * ph;
    std::vector<float> packed(plane * 4);
    std::array<QPoint, 4> positions;
    std::array<int, 4> color_ids{};
    std::array<int, 3> color_counts{};
    int greens{};
    QJsonArray black_levels;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const int channel =
                decoder.COLOR(y + data.sizes.top_margin, x + data.sizes.left_margin);
            const char letter = data.idata.cdesc[channel];
            if (letter != 'R' && letter != 'G' && letter != 'B')
                throw EditError(QStringLiteral("不支持此 RAW CFA 通道"));
            ++color_counts[letter == 'R' ? 0 : letter == 'G' ? 1 : 2];
            const int destination = letter == 'R' ? 0 : letter == 'B' ? 3 : greens++ == 0 ? 1 : 2;
            positions[destination] = {x, y};
            color_ids[destination] = channel;
        }
    if (greens != 2 || color_counts != std::array<int, 3>{1, 2, 1})
        throw EditError(QStringLiteral("不支持此 RAW CFA 排列"));
    for (int channel = 0; channel < 4; ++channel) {
        const double black = data.color.black + data.color.cblack[color_ids[channel]];
        const double white = data.color.maximum;
        if (white <= black)
            throw EditError(QStringLiteral("RAW 黑白位无效"));
        black_levels.append(black);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x) {
                const int sy = y * 2 + positions[channel].y() + data.sizes.top_margin;
                const int sx = x * 2 + positions[channel].x() + data.sizes.left_margin;
                const auto raw =
                    data.rawdata.raw_image[std::size_t(sy) * (data.sizes.raw_pitch / 2) + sx];
                packed[std::size_t(channel) * plane + std::size_t(y) * pw + x] =
                    float(std::clamp((raw - black) / (white - black), 0., 1.));
            }
    }
    QImage image(width, height, QImage::Format_RGBA32FPx4_Premultiplied);
    if (image.isNull())
        throw EditError(QStringLiteral("RAW 输出内存不足"));
    image.setColorSpace(working_space());
    double gain = 1;
    QByteArray model_hash;
    if (options.denoise) {
        QFile model(options.model_path);
        if (!model.open(QIODevice::ReadOnly))
            throw EditError(QStringLiteral("AI RAW 模型缺失，请指定已验证的 Bayer ONNX 模型"));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&model);
        model_hash = hash.result().toHex();
        if (!options.custom_model &&
            model_hash != "da27509dab6a2915da67e988acd86cf71f9d5bbc8d1aa0ed32933578a887b901")
            throw EditError(QStringLiteral("RAW 模型哈希与 M0 固定版本不一致"));
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "HuelithRaw");
        Ort::SessionOptions session_options;
        session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        session_options.DisableMemPattern();
        if (options.directml) {
            const OrtDmlApi* dml{};
            Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
                "DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml)));
            Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(session_options, 0));
            session_options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
        }
        const auto model_path = options.model_path.toStdWString();
        Ort::Session session(env, model_path.c_str(), session_options);
        if (session.GetInputCount() != 1 || session.GetOutputCount() != 1)
            throw EditError(QStringLiteral("自定义模型必须是单输入/单输出的 RawNIND Bayer 模型"));
        Ort::AllocatorWithDefaultOptions contract_allocator;
        const auto input_name = session.GetInputNameAllocated(0, contract_allocator);
        const auto input_type = session.GetInputTypeInfo(0);
        const auto input_info = input_type.GetTensorTypeAndShapeInfo();
        const auto input_shape = input_info.GetShape();
        if (QString::fromUtf8(input_name.get()) != "input" || input_shape.size() != 4 ||
            input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            (input_shape[0] > 0 && input_shape[0] != 1) ||
            (input_shape[1] > 0 && input_shape[1] != 4) ||
            (input_shape[2] > 0 && input_shape[2] != 512) ||
            (input_shape[3] > 0 && input_shape[3] != 512))
            throw EditError(QStringLiteral(
                "模型不兼容：需要 FP32 input[1,4,512,512]，输出线性相机 RGB[1,3,1024,1024]"));
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        constexpr int tile = 512, halo = 128, core = 256;
        constexpr std::size_t tile_plane = std::size_t(tile) * tile;
        std::vector<float> input(tile_plane * 4);
        const std::array<int64_t, 4> shape{1, 4, tile, tile};
        const char* inputs[]{"input"};
        Ort::AllocatorWithDefaultOptions allocator;
        auto output_name = session.GetOutputNameAllocated(0, allocator);
        const char* outputs[]{output_name.get()};
        const int total = ((pw + core - 1) / core) * ((ph + core - 1) / core);
        int completed{};
        double output_sum{};
        for (int ty = 0; ty < ph; ty += core)
            for (int tx = 0; tx < pw; tx += core) {
                check_cancel(cancelled);
                for (int c = 0; c < 4; ++c)
                    for (int y = 0; y < tile; ++y)
                        for (int x = 0; x < tile; ++x)
                            input[std::size_t(c) * tile_plane + y * tile + x] =
                                packed[std::size_t(c) * plane + reflect(ty + y - halo, ph) * pw +
                                       reflect(tx + x - halo, pw)];
                auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                              shape.data(), shape.size());
                auto output = session.Run(Ort::RunOptions{}, inputs, &tensor, 1, outputs, 1);
                const auto info = output[0].GetTensorTypeAndShapeInfo();
                if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                    info.GetShape() != std::vector<int64_t>{1, 3, tile * 2, tile * 2})
                    throw EditError(QStringLiteral("AI RAW 输出尺寸不符合约定"));
                const auto* values = output[0].GetTensorData<float>();
                for (int y = 0; y < std::min(core, ph - ty) * 2; ++y) {
                    auto* row = reinterpret_cast<float*>(image.scanLine(ty * 2 + y));
                    for (int x = 0; x < std::min(core, pw - tx) * 2; ++x) {
                        for (int c = 0; c < 3; ++c) {
                            const float value = values[std::size_t(c) * tile_plane * 4 +
                                                       (y + halo * 2) * tile * 2 + x + halo * 2];
                            if (!std::isfinite(value))
                                throw EditError(QStringLiteral("AI RAW 输出非有限像素"));
                            row[(tx * 2 + x) * 4 + c] = value;
                            output_sum += value;
                        }
                        row[(tx * 2 + x) * 4 + 3] = 1;
                    }
                }
                if (progress)
                    progress(++completed, total);
            }
        check_cancel(cancelled);
        const double input_mean = std::accumulate(packed.begin(), packed.end(), 0.) / packed.size();
        const double output_mean = output_sum / (double(width) * height * 3);
        if (!std::isfinite(output_mean) || output_mean <= 0)
            throw EditError(QStringLiteral("AI RAW 输出增益无效"));
        gain = input_mean / output_mean;
    } else {
        // Float bilinear reference: retain camera-domain samples until the common color transform.
        for (int y = 0; y < height; ++y) {
            check_cancel(cancelled);
            auto* row = reinterpret_cast<float*>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                std::array<float, 4> channels{};
                for (int c = 0; c < 4; ++c) {
                    const double fx = (x - positions[c].x()) / 2., fy = (y - positions[c].y()) / 2.;
                    const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
                    const double dx = fx - ix, dy = fy - iy;
                    for (int yy = 0; yy < 2; ++yy)
                        for (int xx = 0; xx < 2; ++xx)
                            channels[c] += float((xx ? dx : 1 - dx) * (yy ? dy : 1 - dy)) *
                                           packed[std::size_t(c) * plane +
                                                  std::clamp(iy + yy, 0, ph - 1) * pw +
                                                  std::clamp(ix + xx, 0, pw - 1)];
                }
                row[x * 4] = channels[0];
                row[x * 4 + 1] = (channels[1] + channels[2]) / 2;
                row[x * 4 + 2] = channels[3];
                row[x * 4 + 3] = 1;
            }
            if (progress && y % 100 == 0)
                progress(y, height);
        }
    }
    const auto matrix = color_matrix(data.color);
    gain *= std::exp2(options.exposure);
    for (int y = 0; y < height; ++y) {
        check_cancel(cancelled);
        auto* row = reinterpret_cast<float*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const std::array<double, 3> input{row[x * 4], row[x * 4 + 1], row[x * 4 + 2]};
            for (int c = 0; c < 3; ++c) {
                double value{};
                for (int k = 0; k < 3; ++k)
                    value += matrix[c * 3 + k] * input[k];
                row[x * 4 + c] = float(value * gain);
            }
        }
    }
    if (data.sizes.flip == 3)
        image = image.transformed(QTransform().rotate(180));
    else if (data.sizes.flip == 5)
        image = image.transformed(QTransform().rotate(270));
    else if (data.sizes.flip == 6)
        image = image.transformed(QTransform().rotate(90));
    if (progress)
        progress(1, 1);
    return {image,
            {{"camera", QString::fromUtf8(data.idata.model)},
             {"iso", double(data.other.iso_speed)},
             {"aperture", double(data.other.aperture)},
             {"shutter", double(data.other.shutter)},
             {"focalLength", double(data.other.focal_len)},
             {"width", width},
             {"height", height},
             {"black", black_levels},
             {"white", double(data.color.maximum)},
             {"gain", gain},
             {"sourceSha256",
              QString::fromLatin1(
                  QCryptographicHash::hash(source_bytes, QCryptographicHash::Sha256).toHex())},
             {"modelSha256", QString::fromLatin1(model_hash)},
             {"denoise", options.denoise},
             {"backend",
              options.denoise ? (options.directml ? "DirectML" : "CPU") : "bilinear-reference"}}};
}
} // namespace huelith::editing
