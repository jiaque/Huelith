#include "huelith/editing/portrait.h"
#include "huelith/editing/portrait_effects.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/ai_worker.h"
#include "huelith/editing/enhancement.h"
#include "huelith/editing/project.h"
#include "huelith/editing/raw_develop.h"
#include "huelith/editing/semantic.h"
#include "huelith/editing/photo_merge.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstring>

namespace huelith::editing {
namespace {
bool worker_process{};
void save_json(const QString& path, const QJsonObject& data) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw EditError(QStringLiteral("无法写入 AI 任务文件"));
}
QJsonObject load_json(const QString& path, qint64 maximum_bytes = 1024 * 1024) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximum_bytes)
        throw EditError(QStringLiteral("AI 任务文件不可读"));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw EditError(QStringLiteral("AI 任务协议无效"));
    return doc.object();
}
}
bool is_ai_worker() { return worker_process; }
void write_ai_image(const QString& path, const QImage& image) {
    if (image.isNull())
        throw EditError(QStringLiteral("AI 输入图像为空"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw EditError(QStringLiteral("无法创建 AI 图像交换文件"));
    QDataStream stream(&file);
    stream << quint32(0x48414931) << qint32(image.width()) << qint32(image.height())
           << qint32(image.format()) << qint64(image.bytesPerLine()) << image.colorSpace().iccProfile();
    if (stream.status() != QDataStream::Ok ||
        file.write(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()) != image.sizeInBytes() ||
        !file.commit())
        throw EditError(QStringLiteral("AI 图像交换写入失败：请检查临时磁盘空间"));
}
QImage read_ai_image(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw EditError(QStringLiteral("AI 图像交换文件缺失"));
    QDataStream stream(&file);
    quint32 magic{};
    qint32 w{}, h{}, format{};
    qint64 stride{};
    QByteArray profile;
    stream >> magic >> w >> h >> format >> stride >> profile;
    if (stream.status() != QDataStream::Ok || magic != 0x48414931 || w < 1 || h < 1 ||
        qint64(w) * h > 512000000 ||
        (format != QImage::Format_RGBA32FPx4_Premultiplied && format != QImage::Format_RGBA32FPx4 &&
         format != QImage::Format_Grayscale8 && format != QImage::Format_RGBA8888))
        throw EditError(QStringLiteral("AI 图像交换格式无效"));
    auto image = allocate_image(w, h, QImage::Format(format));
    if (image.isNull() || image.bytesPerLine() != stride || file.size() - file.pos() != image.sizeInBytes())
        throw EditError(QStringLiteral("AI 图像尺寸或文件长度不匹配"));
    if (file.read(reinterpret_cast<char*>(image.bits()), image.sizeInBytes()) != image.sizeInBytes())
        throw EditError(QStringLiteral("AI 图像读取不完整"));
    image.setColorSpace(QColorSpace::fromIccProfile(profile));
    return image;
}
AiResult run_ai_job(QJsonObject request, const QImage& image, const std::atomic_bool* cancelled,
                    const AiProgress& progress) {
    auto check = [&] {
        if (cancelled && cancelled->load())
            throw EditError(QStringLiteral("AI 处理已取消"));
    };
    check();
    QTemporaryDir directory(QDir::tempPath() + "/Huelith-ai-XXXXXX");
    if (!directory.isValid())
        throw EditError(QStringLiteral("无法创建 AI 临时目录"));
    if (!image.isNull())
        write_ai_image(directory.filePath("input.bin"), image);
    // Portrait masks belong to the bounded payload, not the small job envelope.
    const bool repair = request["operation"].toString() == "portrait-repair";
    if (repair) {
        const auto person = request.take("person").toObject();
        if (QJsonDocument(person).toJson(QJsonDocument::Compact).size() > 64 * 1024 * 1024)
            throw EditError(QStringLiteral("人像修复分析数据超过 64 MiB，请缩小分析预览"));
        save_json(directory.filePath("person.json"), person);
    }
    auto executable = qEnvironmentVariable("HUELITH_AI_WORKER_EXE");
    if (executable.isEmpty())
        executable = QCoreApplication::applicationDirPath() + "/Huelith.exe";
    QString first_error;
    const bool gpu = request.value("directml").toBool(true);
    for (int attempt = 0; attempt < (gpu ? 2 : 1); ++attempt) {
        check();
        request["directml"] = gpu && attempt == 0;
        save_json(directory.filePath("job.json"), request);
        QProcess worker;
        worker.setProcessChannelMode(QProcess::SeparateChannels);
        worker.start(executable, {"--ai-worker", directory.filePath("job.json")});
        if (!worker.waitForStarted(10000))
            throw EditError(QStringLiteral("无法启动 AI 工作进程：%1").arg(worker.errorString()));
        QElapsedTimer heartbeat;
        heartbeat.start();
        QByteArray pending, errors;
        while (worker.state() != QProcess::NotRunning) {
            worker.waitForFinished(25);
            if (cancelled && cancelled->load()) {
                worker.kill();
                worker.waitForFinished(5000);
                check();
            }
            pending += worker.readAllStandardOutput();
            errors += worker.readAllStandardError();
            if (errors.size() > 16384) errors = errors.right(16384);
            qsizetype newline{};
            while ((newline = pending.indexOf('\n')) >= 0) {
                const auto update = QJsonDocument::fromJson(pending.left(newline)).object();
                pending.remove(0, newline + 1);
                if (update.contains("done")) {
                    heartbeat.restart();
                    if (progress) progress(update["done"].toInt(), update["total"].toInt());
                }
            }
            if (heartbeat.elapsed() > 180000) {
                worker.kill();
                worker.waitForFinished(5000);
                errors += "\nAI 推理超过 180 秒无响应";
                break;
            }
        }
        check();
        if (worker.exitStatus() == QProcess::NormalExit && worker.exitCode() == 0) {
            AiResult result;
            result.metadata = load_json(directory.filePath("result.json"), repair ? 64 * 1024 * 1024 : 1024 * 1024);
            result.image = read_ai_image(directory.filePath("output.bin"));
            if (QFile::exists(directory.filePath("reference.bin")))
                result.reference = read_ai_image(directory.filePath("reference.bin"));
            result.metadata["workerIsolated"] = true;
            result.metadata["cpuFallback"] = attempt > 0;
            if (!first_error.isEmpty()) result.metadata["fallbackReason"] = first_error;
            return result;
        }
        const auto error = QString::fromUtf8(errors).trimmed();
        first_error = error.isEmpty() ? QStringLiteral("AI 工作进程异常退出 (%1)").arg(worker.exitCode()) : error;
    }
    throw EditError(QStringLiteral("AI 处理失败；原文档未修改。%1").arg(first_error));
}
int ai_worker_main(const QString& job_path) {
    worker_process = true;
    try {
        const auto request = load_json(job_path);
        const QDir directory = QFileInfo(job_path).absoluteDir();
        const AiProgress progress = [](int done, int total) {
            const auto line = QJsonDocument(QJsonObject{{"done", done}, {"total", total}}).toJson(QJsonDocument::Compact) + '\n';
            std::fwrite(line.constData(), 1, std::size_t(line.size()), stdout);
            std::fflush(stdout);
        };
        progress(0, 1);
        AiResult result;
        if (request["operation"].toString() == "raw") {
            RawOptions options;
            options.denoise = true;
            options.directml = request["directml"].toBool();
            options.custom_model = request["custom"].toBool();
            options.model_path = request["model"].toString();
            options.exposure = request["exposure"].toDouble();
            const std::atomic_bool cancelled{false};
            const auto raw = develop_raw(request["source"].toString(), options, cancelled, progress);
            result = {raw.image, raw.metadata, raw.reference};
        } else if (request["operation"].toString() == "portrait") {
            result = infer_portrait(read_ai_image(directory.filePath("input.bin")), request["models"].toString(), progress);
        } else if (request["operation"].toString() == "portrait-nails") {
            result.metadata["nails"]=portrait_detect_nails(read_ai_image(directory.filePath("input.bin")),request["model"].toString());
            result.image=QImage(1,1,QImage::Format_Grayscale8);result.image.fill(0);
        } else if (request["operation"].toString() == "portrait-repair") {
            const auto source=read_ai_image(directory.filePath("input.bin"));const auto person=load_json(directory.filePath("person.json"),64 * 1024 * 1024);
            QImage evidence;progress(0,2);
            if(!request["model"].toString().isEmpty())evidence=portrait_blemish_evidence(source,person,request["model"].toString());
            progress(1,2);result.metadata=portrait_repair_analysis(source,person,evidence);
            result.image=evidence;
            if(result.image.isNull()){result.image=QImage(1,1,QImage::Format_Grayscale8);result.image.fill(0);}
            progress(2,2);
        } else if (request["operation"].toString() == "merge") {
            result = infer_photo_merge(request, progress);
        } else if (request["operation"].toString() == "semantic") {
            const std::atomic_bool cancelled{false};
            result.image = semantic_labels(read_ai_image(directory.filePath("input.bin")), request["model"].toString(), cancelled);
        } else if (request["operation"].toString() == "instances") {
            const std::atomic_bool cancelled{false};
            const auto instances = semantic_instances(read_ai_image(directory.filePath("input.bin")), request["model"].toString(), cancelled);
            const int width = instances.empty() ? 1 : instances.front().mask.width();
            const int height = instances.empty() ? 1 : instances.front().mask.height();
            result.image = QImage(width, height * std::max(1, int(instances.size())), QImage::Format_Grayscale8);
            result.image.fill(0);
            QJsonArray records;
            int index{};
            for (const auto& item : instances) {
                for (int y = 0; y < height; ++y)
                    std::memcpy(result.image.scanLine(index * height + y), item.mask.constScanLine(y), std::size_t(width));
                records.append(QJsonObject{{"category", item.category}, {"confidence", item.confidence}, {"pixels", item.pixels},
                    {"bounds", QJsonArray{item.bounds.x(), item.bounds.y(), item.bounds.width(), item.bounds.height()}}});
                ++index;
            }
            result.metadata["instances"] = records;
            result.metadata["height"] = height;
        } else if (request["operation"].toString() == "enhance") {
            EnhanceOptions options;
            options.mode = EnhanceMode(request["mode"].toInt());
            options.scale = request["scale"].toInt();
            options.strength = request["strength"].toDouble();
            options.directml = request["directml"].toBool();
            options.quality = request["quality"].toBool();
            options.model = request["model"].toString();
            result = infer_enhancement(read_ai_image(directory.filePath("input.bin")), options, progress);
        } else throw EditError(QStringLiteral("未知 AI 任务"));
        write_ai_image(directory.filePath("output.bin"), result.image);
        if (!result.reference.isNull()) write_ai_image(directory.filePath("reference.bin"), result.reference);
        save_json(directory.filePath("result.json"), result.metadata);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
} // namespace huelith::editing
