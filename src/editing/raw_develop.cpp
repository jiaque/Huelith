#include "huelith/editing/raw_camera.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/raw_develop.h"
#include "huelith/editing/ai_worker.h"
#include "huelith/editing/demosaic.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <dml_provider_factory.h>
#include <libraw/libraw.h>
#include <numeric>
#include <memory>
#include <cstring>
#include <QStringList>
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

bool is_raw_suffix(const QString& suffix) {
    static const QStringList formats{"cr3","cr2","crw","nef","nrw","arw","sr2","srf",
        "dng","raf","rw2","orf","pef","ptx","rwl","raw","srw","3fr","fff","iiq",
        "kdc","dcr","mos","mrw","erf","mef","mdc","nrw","x3f","bay","cap","eip"};
    return formats.contains(suffix.toLower());
}
bool is_raw_file(const QString& path) {
    const auto suffix=QFileInfo(path).suffix().toLower();
    if(is_raw_suffix(suffix))return true;
    // Ordinary raster documents stay on the established image decoder path.
    if(QStringList{"png","jpg","jpeg","tif","tiff","bmp","webp","gif","huelith"}.contains(suffix))return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>512LL*1024*1024)return false;
    const auto bytes=file.readAll();LibRaw decoder;
    return decoder.open_buffer(const_cast<char*>(bytes.constData()),std::size_t(bytes.size()))==LIBRAW_SUCCESS;
}
namespace {
bool bayer_layout(LibRaw& decoder) {
    const auto& d=decoder.imgdata;
    if (!d.idata.filters || d.idata.filters==9 || d.idata.colors!=3) return false;
    std::array<int,3> counts{};
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        char c=d.idata.cdesc[decoder.COLOR(y+d.sizes.top_margin,x+d.sizes.left_margin)];
        if(c!='R'&&c!='G'&&c!='B')return false;
        ++counts[c=='R'?0:c=='G'?1:2];
    }
    return counts==std::array<int,3>{1,2,1};
}
QJsonObject capabilities(LibRaw& decoder) {
    const auto& d=decoder.imgdata;
    const bool bayer=bayer_layout(decoder),xtrans=d.idata.filters==9;
    const bool dng=(bayer||xtrans)&&d.idata.colors==3;
    return {{"sensorLayout",xtrans?"X-Trans":bayer?"Bayer":"LibRaw special"},
        {"rawDenoiseSupported",bayer},{"rawDngSupported",dng},
        {"rawDenoiseReason",bayer?QString{}:QStringLiteral("此传感器不兼容内置 Bayer AI RAW 降噪模型；可普通显影后在主窗口使用 RGB AI 降噪。")},
        {"rawDngReason",dng?QString{}:QStringLiteral("此 RAW 的传感器布局暂不支持保留原始数据的 DNG 转存；可保存工程或导出 TIFF。")}};
}
RawResult generic_develop(LibRaw& decoder,const QByteArray& bytes,const RawOptions& options,
                          const std::atomic_bool& cancelled,const RawProgress& progress) {
    if(options.denoise)throw EditError(QStringLiteral("此传感器不支持 Bayer AI RAW 降噪；请关闭 AI RAW 降噪后显影，或在主窗口使用 RGB AI 降噪。"));
    auto& d=decoder.imgdata;
    QJsonObject metadata=capabilities(decoder);
    metadata.insert("camera",QString::fromUtf8(d.idata.model));metadata.insert("cameraMake",QString::fromUtf8(d.idata.make));
    metadata.insert("lens",QString::fromUtf8(d.lens.Lens));metadata.insert("lensMake",QString::fromUtf8(d.lens.LensMake));
    metadata.insert("iso",double(d.other.iso_speed));metadata.insert("aperture",double(d.other.aperture));
    metadata.insert("shutter",double(d.other.shutter));metadata.insert("focalLength",double(d.other.focal_len));
    Matrix matrix{}; QJsonArray xyz,gains;
    // Three-channel CFA sources retain the same camera-domain WB/profile contract as Bayer.
    bool camera_domain=d.idata.colors==3 && d.idata.filters;
    if(camera_domain){
        try{matrix=color_matrix(d.color);}catch(const EditError&){camera_domain=false;}
    }
    if(camera_domain){
        for(int y=0;y<3;++y){gains.append(double(d.color.cam_mul[y]/d.color.cam_mul[1]));
            for(int x=0;x<3;++x)xyz.append(double(d.color.cam_xyz[y][x]));}
    }else{
        const Matrix xyz_srgb{3.2404542,-1.5371385,-.4985314,-.969266,1.8760108,.041556,.0556434,-.2040259,1.0572252};
        const Matrix xyz_rec{1.716651188,-.355670784,-.253366281,-.666684352,1.616481237,.015768546,.017639857,-.042770613,.942103121};
        matrix=multiply(xyz_rec,inverse(xyz_srgb));
        for(auto v:xyz_srgb)xyz.append(v);gains={1.,1.,1.};
    }
    auto& params=d.params;
    params.output_bps=16;params.gamm[0]=params.gamm[1]=1;
    params.no_auto_bright=1;params.adjust_maximum_thr=0;params.use_camera_wb=camera_domain?0:1;
    params.output_color=camera_domain?0:1;params.user_qual=3;
    params.user_flip=0; // Apply sensor orientation exactly once, below.
    if(camera_domain)for(float& v:params.user_mul)v=1;
    struct Context{const std::atomic_bool* cancelled;const RawProgress* progress;};
    Context context{&cancelled,&progress};
    decoder.set_progress_handler([](void* p,LibRaw_progress,int,int)->int{
        return static_cast<Context*>(p)->cancelled->load()?1:0;
    },&context);
    if(progress)progress(0,100);
    const int flip=d.sizes.flip;
    int error=decoder.dcraw_process();check_cancel(cancelled);
    if(error!=LIBRAW_SUCCESS)throw EditError(QString::fromUtf8(libraw_strerror(error)));
    auto* bitmap=decoder.dcraw_make_mem_image(&error);
    std::unique_ptr<libraw_processed_image_t,void(*)(libraw_processed_image_t*)> memory(bitmap,LibRaw::dcraw_clear_mem);
    if(error!=LIBRAW_SUCCESS||!bitmap||bitmap->type!=LIBRAW_IMAGE_BITMAP||bitmap->bits!=16||bitmap->colors<3)
        throw EditError(QStringLiteral("LibRaw 未能生成受支持的线性 RGB 显影结果"));
    auto image=allocate_image(bitmap->width,bitmap->height,QImage::Format_RGBA32FPx4_Premultiplied);
    image.setColorSpace(working_space());
    const auto* input=reinterpret_cast<const uint16_t*>(bitmap->data);
    const double exposure=std::exp2(options.exposure);
    for(int y=0;y<image.height();++y){check_cancel(cancelled);auto* row=reinterpret_cast<float*>(image.scanLine(y));
        for(int x=0;x<image.width();++x){const auto* pixel=input+(std::size_t(y)*image.width()+x)*bitmap->colors;
            for(int c=0;c<3;++c){double v=0;for(int k=0;k<3;++k)v+=matrix[c*3+k]*pixel[k]/65535.;row[4*x+c]=float(v*exposure);}
            row[4*x+3]=1;}
    }
    const int angle=flip==3?180:flip==5?270:flip==6?90:0;
    if(angle)image=image.transformed(QTransform().rotate(angle));
    QJsonArray transform;for(auto v:matrix)transform.append(v);
    metadata.insert("cameraToWorking",transform);metadata.insert("xyzToCamera",xyz);metadata.insert("asShotGains",gains);
    metadata.insert("whiteBalanceBasis",camera_domain?"camera":"linear-sRGB adaptation");
    metadata.insert("width",image.width());metadata.insert("height",image.height());
    metadata.insert("backend","libraw-0.22-linear16");metadata.insert("denoise",false);
    metadata.insert("sourceSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
    if(progress)progress(1,1);
    return {image,metadata,{}};
}
}
QJsonObject raw_capabilities(const QByteArray& source) {
    LibRaw decoder;
    const auto error=decoder.open_buffer(const_cast<char*>(source.constData()),std::size_t(source.size()));
    if(error!=LIBRAW_SUCCESS)throw EditError(QString::fromUtf8(libraw_strerror(error)));
    return capabilities(decoder);
}

bool raw_high_quality(const QJsonObject& recipe) {
    if (!recipe.contains("demosaic"))
        return !recipe.contains("version");
    const auto method = recipe.value("demosaic").toString();
    if (method != "malvar-2004" && method != "bilinear")
        throw EditError(QStringLiteral("RAW 插值版本无效"));
    return method == "malvar-2004";
}
QImage blend_raw_denoise(const QImage& enhanced, const QImage& reference, double amount,
                         const std::atomic_bool* cancelled) {
    if (!std::isfinite(amount) || amount < 0 || amount > 100)
        throw EditError(QStringLiteral("降噪强度必须在 0–100 之间"));
    if (reference.isNull() || amount == 100)
        return enhanced;
    if (reference.size() != enhanced.size() || reference.format() != enhanced.format() ||
        enhanced.format() != QImage::Format_RGBA32FPx4_Premultiplied ||
        reference.colorSpace() != enhanced.colorSpace())
        throw EditError(QStringLiteral("降噪对照图像不一致"));
    if (amount == 0)
        return reference;
    auto result = copy_image(enhanced);
    if (result.isNull())
        throw EditError(QStringLiteral("降噪强度预览内存不足"));
    const float weight = float(amount / 100);
    for (int y = 0; y < result.height(); ++y) {
        if (cancelled)
            check_cancel(*cancelled);
        auto* row = reinterpret_cast<float*>(result.scanLine(y));
        const auto* before = reinterpret_cast<const float*>(reference.constScanLine(y));
        for (int x = 0; x < result.width(); ++x)
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] = std::lerp(before[x * 4 + c], row[x * 4 + c], weight);
    }
    return result;
}
RawResult develop_raw(const QString& path, const RawOptions& options,
                      const std::atomic_bool& cancelled, const RawProgress& progress) {
    if (!std::isfinite(options.exposure) || std::abs(options.exposure) > 20)
        throw EditError(QStringLiteral("RAW 曝光参数无效"));
    check_cancel(cancelled);
    if (options.denoise && !is_ai_worker()) {
        const auto result = run_ai_job({{"operation", "raw"}, {"source", path},
            {"model", options.model_path}, {"custom", options.custom_model},
            {"directml", options.directml}, {"exposure", options.exposure}}, {}, &cancelled, progress);
        return {result.image, result.metadata, result.reference};
    }
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
    normalize_raw_matrix(decoder.imgdata.color);
    const bool neutral_fallback=normalize_raw_neutral(decoder.imgdata.color);
    const auto& data = decoder.imgdata;
    const int source_width=data.sizes.width, source_height=data.sizes.height;
    const int width=(source_width+1)&~1, height=(source_height+1)&~1;
    if (width < 4 || height < 4 || qint64(width)*height > maximum_image_pixels)
        throw EditError(QStringLiteral("RAW 图像尺寸超出支持范围"));
    if (!bayer_layout(decoder) || !data.rawdata.raw_image)
        return generic_develop(decoder, source_bytes, options, cancelled, progress);
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
        const double base_black = data.color.black + data.color.cblack[color_ids[channel]];
        const double white = data.color.maximum;
        if (white <= base_black || data.color.cblack[4] > 64 || data.color.cblack[5] > 64)
            throw EditError(QStringLiteral("RAW 黑白位无效"));
        black_levels.append(base_black);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x) {
                // Pad an odd active extent by repeating the last sample of the same CFA color.
                const int local_y=std::min(y*2+positions[channel].y(),source_height-1-((source_height-1-positions[channel].y())%2));
                const int local_x=std::min(x*2+positions[channel].x(),source_width-1-((source_width-1-positions[channel].x())%2));
                const int sy=local_y+data.sizes.top_margin, sx=local_x+data.sizes.left_margin;
                double black=base_black;
                if(data.color.cblack[4] && data.color.cblack[5])
                    black+=data.color.cblack[6+((sy-data.sizes.top_margin)%data.color.cblack[4])*data.color.cblack[5]+(sx-data.sizes.left_margin)%data.color.cblack[5]];
                if(white<=black)throw EditError(QStringLiteral("RAW 黑电平不小于白电平"));
                const auto raw =
                    data.rawdata.raw_image[std::size_t(sy) * (data.sizes.raw_pitch / 2) + sx];
                packed[std::size_t(channel) * plane + std::size_t(y) * pw + x] =
                    float(std::clamp((raw - black) / (white - black), 0., 1.));
            }
    }
    auto image = allocate_image(width, height, QImage::Format_RGBA32FPx4_Premultiplied);
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
    } else if (!options.high_quality) {
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
    if (!options.denoise && options.high_quality)
        demosaic_malvar(packed, positions, image, cancelled);
    QImage reference;
    if (options.denoise) {
        reference = allocate_image(width, height, QImage::Format_RGBA32FPx4_Premultiplied);
        if (reference.isNull())
            throw EditError(QStringLiteral("降噪对照图像内存不足"));
        reference.setColorSpace(working_space());
        demosaic_malvar(packed, positions, reference, cancelled);
    }
    const auto matrix = color_matrix(data.color);
    QJsonArray camera_to_working, xyz_to_camera, as_shot_gains;
    for (double value : matrix)
        camera_to_working.append(value);
    for (int y = 0; y < 3; ++y) {
        as_shot_gains.append(double(data.color.cam_mul[y] / data.color.cam_mul[1]));
        for (int x = 0; x < 3; ++x)
            xyz_to_camera.append(double(data.color.cam_xyz[y][x]));
    }
    gain *= std::exp2(options.exposure);
    for (int y = 0; y < height; ++y) {
        check_cancel(cancelled);
        auto* row = reinterpret_cast<float*>(image.scanLine(y));
        auto* original =
            reference.isNull() ? nullptr : reinterpret_cast<float*>(reference.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const std::array<double, 3> input{row[x * 4], row[x * 4 + 1], row[x * 4 + 2]};
            for (int c = 0; c < 3; ++c) {
                double value{};
                for (int k = 0; k < 3; ++k)
                    value += matrix[c * 3 + k] * input[k];
                row[x * 4 + c] = float(value * gain);
            }
            if (original) {
                const std::array<double, 3> value{original[x * 4], original[x * 4 + 1],
                                                  original[x * 4 + 2]};
                for (int c = 0; c < 3; ++c) {
                    double sum{};
                    for (int k = 0; k < 3; ++k)
                        sum += matrix[c * 3 + k] * value[k];
                    original[x * 4 + c] = float(sum * std::exp2(options.exposure));
                }
            }
        }
    }
    if(width!=source_width || height!=source_height){
        const auto crop=[&](QImage& value){
            if(value.isNull())return;
            auto cropped=allocate_image(source_width,source_height,value.format());cropped.setColorSpace(value.colorSpace());
            for(int y=0;y<source_height;++y)std::memcpy(cropped.scanLine(y),value.constScanLine(y),std::size_t(source_width)*16);
            value=std::move(cropped);
        };
        crop(image);crop(reference);
    }
    if (data.sizes.flip == 3)
        image = image.transformed(QTransform().rotate(180));
    else if (data.sizes.flip == 5)
        image = image.transformed(QTransform().rotate(270));
    else if (data.sizes.flip == 6)
        image = image.transformed(QTransform().rotate(90));
    if (!reference.isNull()) {
        const int angle = data.sizes.flip == 3   ? 180
                          : data.sizes.flip == 5 ? 270
                          : data.sizes.flip == 6 ? 90
                                                 : 0;
        if (angle)
            reference = reference.transformed(QTransform().rotate(angle));
    }
    if (progress)
        progress(1, 1);
    return {image,
            {{"whiteBalanceSource", neutral_fallback?"daylight-fallback":"as-shot"},
             {"sensorLayout", "Bayer"},
             {"rawDenoiseSupported", true}, {"rawDngSupported", true},
             {"cameraToWorking", camera_to_working},
             {"xyzToCamera", xyz_to_camera},
             {"asShotGains", as_shot_gains},
             {"camera", QString::fromUtf8(data.idata.model)},
             {"cameraMake", QString::fromUtf8(data.idata.make)},
             {"lens", QString::fromUtf8(data.lens.Lens)},
             {"lensMake", QString::fromUtf8(data.lens.LensMake)},
             {"iso", double(data.other.iso_speed)},
             {"aperture", double(data.other.aperture)},
             {"shutter", double(data.other.shutter)},
             {"focalLength", double(data.other.focal_len)},
             {"width", source_width},
             {"height", source_height},
             {"black", black_levels},
             {"white", double(data.color.maximum)},
             {"gain", gain},
             {"sourceSha256",
              QString::fromLatin1(
                  QCryptographicHash::hash(source_bytes, QCryptographicHash::Sha256).toHex())},
             {"modelSha256", QString::fromLatin1(model_hash)},
             {"denoise", options.denoise},
             {"backend", options.denoise        ? (options.directml ? "DirectML" : "CPU")
                         : options.high_quality ? "malvar-2004"
                                                : "bilinear-reference"}},
            reference};
}
} // namespace huelith::editing
