#include "canvas_preferences.h"
#include "editor_controller.h"
#include "main_window.h"
#include "huelith/editing/image_storage.h"
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibraryInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QStorageInfo>
#include <QSysInfo>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif
#include <QVersionNumber>
namespace huelith::desktop {
void EditorController::show_help() {
    QDialog dlg(window_);
    dlg.setWindowTitle(tr("Huelith 帮助与诊断"));
    dlg.resize(620, 490);
    QVBoxLayout layout(&dlg);
    QLabel intro(tr("文件菜单中可以打开 RAW 或分层文档、运行自动化动作、使用生成工作流。保存 "
                    ".huelith 工程可以保留原始图层与参数。"));
    intro.setWordWrap(true);
    layout.addWidget(&intro);
    const auto path = QCoreApplication::applicationFilePath();
    const auto prefs = read_canvas_preferences();
    QFile exe(path);
    QByteArray fingerprint;
    if (exe.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&exe))
            fingerprint = hash.result().toHex();
    }
    const QStorageInfo disk(QDir::tempPath());
    const QString scratchPath=prefs.scratch_dir.isEmpty() ? QDir::tempPath() : prefs.scratch_dir;
    const QStorageInfo scratch(scratchPath);
    const auto pixelUse=editing::pixel_storage_usage();
    QStringList lines{
        tr("版本：%1").arg(QCoreApplication::applicationVersion()),
        tr("正在运行：%1").arg(QDir::toNativeSeparators(path)),
        tr("程序 SHA-256：%1").arg(QString::fromLatin1(fingerprint)),
        tr("系统：%1 · %2").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()),
        tr("Qt：%1 · %2").arg(QLatin1String(qVersion()), QGuiApplication::platformName()),
        tr("RAW 首选设备：%1").arg(prefs.raw_backend == 0 ? "DirectML" : "CPU"),
        tr("RAW 内存 / 磁盘预算：%1 / %2 MiB").arg(prefs.raw_memory_mb).arg(prefs.raw_disk_mb),
        tr("RAW 临时缓存卷可用：%1 GiB").arg(disk.bytesAvailable() / 1073741824., 0, 'f', 1),
        tr("像素暂存目录：%1").arg(QDir::toNativeSeparators(scratchPath)),
        tr("像素暂存卷可用：%1 GiB").arg(scratch.bytesAvailable() / 1073741824.,0,'f',1),
        tr("托管像素常驻 / 暂存使用：%1 / %2 MiB").arg(pixelUse.resident / 1048576.).arg(pixelUse.mapped / 1048576.),
        tr("托管像素内存 / 暂存预算：%1 MiB / %2 GiB").arg(prefs.pixels_mb).arg(prefs.scratch_gib),
        tr("每文档历史额外像素预算：%1 MiB").arg(prefs.history_mb),
        tr("恢复目录：%1").arg(recovery_directory())};
#ifdef Q_OS_WIN
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        for (UINT i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapters1(i, &adapter)))
                break;
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                lines.append(tr("显示适配器：%1 · 专用显存 %2 MiB")
                                 .arg(QString::fromWCharArray(d.Description))
                                 .arg(d.DedicatedVideoMemory >> 20));
        }
    }
#endif
    for (auto* screen : QGuiApplication::screens())
        lines.append(tr("屏幕：%1，工作区 %2 × %3，缩放 %4%")
                         .arg(screen->name())
                         .arg(screen->availableGeometry().width())
                         .arg(screen->availableGeometry().height())
                         .arg(qRound(screen->devicePixelRatio() * 100)));
    QPlainTextEdit info(lines.join("\n"));
    info.setObjectName("helpDiagnostics");
    info.setReadOnly(true);
    layout.addWidget(&info, 1);
    QLabel status;
    status.setWordWrap(true);
    layout.addWidget(&status);
    QHBoxLayout buttons;
    QPushButton copy(tr("复制诊断")), keys(tr("快捷键表…")), updates(tr("检查更新")),
        release(tr("打开发布页"));
    for (auto* b : {&copy, &keys, &updates, &release})
        buttons.addWidget(b);
    layout.addLayout(&buttons);
    connect(&copy, &QPushButton::clicked, &dlg, [&] {
        QApplication::clipboard()->setText(info.toPlainText());
        status.setText(tr("已复制"));
    });
    connect(&keys, &QPushButton::clicked, &dlg, [&] { show_shortcuts(); });
    connect(&release, &QPushButton::clicked, &dlg,
            [] { QDesktopServices::openUrl(QUrl("https://github.com/jiaque/Huelith/releases")); });
    QNetworkAccessManager net;
    connect(&updates, &QPushButton::clicked, &dlg, [&] {
        updates.setEnabled(false);
        status.setText(tr("正在查询 GitHub Releases…"));
        QNetworkRequest req(QUrl("https://api.github.com/repos/jiaque/Huelith/releases/latest"));
        req.setRawHeader("User-Agent", "Huelith");
        req.setTransferTimeout(15000);
        auto* reply = net.get(req);
        connect(reply, &QNetworkReply::finished, &dlg, [&, reply] {
            updates.setEnabled(true);
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                status.setText(tr("查询失败：") + reply->errorString());
                return;
            }
            auto tag = QJsonDocument::fromJson(reply->readAll()).object()["tag_name"].toString();
            auto v = QVersionNumber::fromString(tag.startsWith('v') ? tag.mid(1) : tag);
            auto here = QVersionNumber::fromString(QCoreApplication::applicationVersion());
            if (v.isNull()) {
                status.setText(tr("无法识别发布版本：") + tag);
                return;
            }
            status.setText(QVersionNumber::compare(v.normalized(), here.normalized()) > 0
                               ? tr("有新版本 %1，可在发布页下载。").arg(tag)
                               : tr("最新发布：%1，未发现更高版本。").arg(tag));
        });
    });
    dlg.exec();
}
} // namespace huelith::desktop
