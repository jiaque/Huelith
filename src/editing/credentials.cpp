#include <QCoreApplication>
#include "huelith/editing/credentials.h"
#include "huelith/editing/project.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

namespace huelith::editing {
namespace {
QByteArray run_tool(const CredentialSettings& settings, QStringList arguments,
                    const std::atomic_bool* cancel, bool allow_missing = false) {
    if (!QFileInfo(settings.tool).isFile())
        throw EditError(QCoreApplication::translate("Huelith", "C2PA 工具缺失，请在内容凭证设置中选择 c2patool"));
    QTemporaryDir directory;
    if (!directory.isValid())
        throw EditError(QCoreApplication::translate("Huelith", "无法建立凭证临时目录"));
    QFile config(directory.filePath("settings.json"));
    if (!config.open(QIODevice::WriteOnly) || config.write("{}") != 2)
        throw EditError(QCoreApplication::translate("Huelith", "无法写入凭证配置"));
    config.close();
    arguments << "--settings" << config.fileName();
    if (!settings.trust_anchors.isEmpty() && !arguments.contains("--manifest")) {
        if (!QFileInfo(settings.trust_anchors).isFile())
            throw EditError(QCoreApplication::translate("Huelith", "信任根文件不存在"));
        arguments << "trust" << "--trust_anchors" << settings.trust_anchors;
    }
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto& key : environment.keys())
        if (key.startsWith("C2PA_", Qt::CaseInsensitive) ||
            key.startsWith("C2PATOOL_", Qt::CaseInsensitive))
            environment.remove(key);
    process.setProcessEnvironment(environment);
    process.setProgram(QFileInfo(settings.tool).absoluteFilePath());
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(10000))
        throw EditError(process.errorString());
    QElapsedTimer timer;
    timer.start();
    QByteArray output, errors;
    for (;;) {
        const bool ended = process.waitForFinished(50);
        output += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if ((cancel && cancel->load()) || timer.elapsed() > 120000 ||
            output.size() > 16 * 1024 * 1024 || errors.size() > 1024 * 1024) {
            process.kill();
            process.waitForFinished(5000);
            throw EditError(cancel && cancel->load()
                                ? QCoreApplication::translate("Huelith", "内容凭证操作已取消")
                                : QCoreApplication::translate("Huelith", "内容凭证操作超时或响应过大"));
        }
        if (ended)
            break;
    }
    if (allow_missing && process.exitStatus() == QProcess::NormalExit && process.exitCode() != 0 &&
        (errors.isEmpty() ? output : errors).trimmed() == "Error: No claim found")
        return "{}";
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw EditError(QCoreApplication::translate("Huelith", "C2PA 处理失败：") +
                        QString::fromUtf8(errors.isEmpty() ? output : errors).left(3000));
    return output;
}
} // namespace
CredentialReport inspect_credentials(const QString& path, const CredentialSettings& settings,
                                     const std::atomic_bool* cancel) {
    const auto bytes = run_tool(settings, {QFileInfo(path).absoluteFilePath()}, cancel, true);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        throw EditError(QCoreApplication::translate("Huelith", "C2PA 返回了无效报告"));
    CredentialReport result;
    result.details = json.object();
    result.present = !result.details.value("active_manifest").toString().isEmpty();
    const auto state = result.details.value("validation_state").toString();
    result.trusted = result.present && state == "Trusted";
    result.valid = result.present && (state == "Valid" || state == "Trusted");
    return result;
}
void sign_credentials(const QString& source, const QString& output, const QString& title,
                      const CredentialSettings& settings, const std::atomic_bool* cancel) {
    // An explicit external signer is required; c2patool's built-in test key must never be selected
    // implicitly.
    if (!QFileInfo(settings.signer).isFile() || settings.signer.contains('"'))
        throw EditError(QCoreApplication::translate("Huelith", "请先配置外部 C2PA 签名程序；不会使用内置测试证书"));
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(output).absoluteFilePath() ||
        QFileInfo::exists(output))
        throw EditError(QCoreApplication::translate("Huelith", "签名输出必须是新的临时文件"));
    QTemporaryDir directory;
    if (!directory.isValid())
        throw EditError(QCoreApplication::translate("Huelith", "无法建立签名临时目录"));
    const QJsonObject manifest{
        {"claim_generator_info", QJsonArray{QJsonObject{{"name", "Huelith"}, {"version", "0.1"}}}},
        {"title", title},
        {"assertions",
         QJsonArray{QJsonObject{
             {"label", "c2pa.actions"},
             {"data",
              QJsonObject{{"actions", QJsonArray{QJsonObject{{"action", "c2pa.edited"}}}}}}}}}};
    QFile file(directory.filePath("manifest.json"));
    const auto bytes = QJsonDocument(manifest).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw EditError(QCoreApplication::translate("Huelith", "无法写入内容凭证声明"));
    file.close();
    run_tool(settings,
             {source, "--manifest", file.fileName(), "--signer-path",
              '"' + QFileInfo(settings.signer).absoluteFilePath() + '"', "--output", output},
             cancel);
    const auto report = inspect_credentials(output, settings, cancel);
    if (!report.valid)
        throw EditError(QCoreApplication::translate("Huelith", "签名后完整性验证未通过，输出已取消"));
}
} // namespace huelith::editing
