#include "credential_workflow.h"
#include "editor_controller.h"
#include "main_window.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <future>
namespace huelith::desktop {
editing::CredentialSettings configured_credentials() {
    QSettings settings;
    return {settings
                .value("credentials/tool",
                       QCoreApplication::applicationDirPath() + "/tools/c2patool/c2patool.exe")
                .toString(),
            settings.value("credentials/signer").toString(),
            settings.value("credentials/trustAnchors").toString()};
}
void EditorController::show_credentials() {
    auto settings = configured_credentials();
    QDialog dialog(window_);
    dialog.setObjectName("contentCredentialsDialog");
    dialog.setWindowTitle(tr("内容凭证 · C2PA"));
    dialog.resize(700, 620);
    QFormLayout form(&dialog);
    QLineEdit tool(settings.tool), signer(settings.signer), trust(settings.trust_anchors);
    const auto row = [&](const QString& title, QLineEdit* field) {
        auto* layout = new QHBoxLayout;
        layout->addWidget(field);
        auto* browse = new QPushButton(tr("选择…"));
        layout->addWidget(browse);
        form.addRow(title, layout);
        connect(browse, &QPushButton::clicked, &dialog, [&, field] {
            const auto path = QFileDialog::getOpenFileName(&dialog, tr("选择文件"), field->text());
            if (!path.isEmpty())
                field->setText(path);
        });
    };
    row(tr("c2patool"), &tool);
    row(tr("外部签名程序"), &signer);
    row(tr("信任根 PEM（可选）"), &trust);
    QLabel note(tr("签名程序负责证书和密钥，可连接本地安全存储或远程签名服务。Huelith "
                   "不保存私钥。未配置信任根时按 C2PA "
                   "工具默认信任策略验证；完整性有效和身份受信任分别显示。"));
    note.setWordWrap(true);
    form.addRow(&note);
    QPushButton inspect(tr("读取并验证图片凭证…"));
    form.addRow(&inspect);
    QLabel status;
    status.setWordWrap(true);
    form.addRow(&status);
    QPlainTextEdit report;
    report.setReadOnly(true);
    form.addRow(&report);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&inspect, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getOpenFileName(
            &dialog, tr("验证内容凭证"), {},
            tr("Images (*.jpg *.jpeg *.png *.tif *.tiff *.webp);;所有文件 (*)"));
        if (path.isEmpty())
            return;
        std::atomic_bool cancelled{};
        const editing::CredentialSettings config{tool.text(), signer.text(), trust.text()};
        auto job = std::async(std::launch::async, [&] {
            return editing::inspect_credentials(path, config, &cancelled);
        });
        QProgressDialog progress(tr("验证签名和内容绑定…"), tr("取消"), 0, 0, &dialog);
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        while (job.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancelled = true;
            QThread::msleep(10);
        }
        try {
            const auto result = job.get();
            status.setText(!result.present  ? tr("文件没有内容凭证")
                           : !result.valid  ? tr("凭证验证失败；查看报告中的失败原因")
                           : result.trusted ? tr("完整性验证通过；签名身份受当前信任策略信任")
                                            : tr("完整性验证通过；签名身份尚未受当前信任策略信任"));
            report.setPlainText(QJsonDocument(result.details).toJson(QJsonDocument::Indented));
        } catch (const std::exception& e) {
            status.setText(QString::fromUtf8(e.what()));
            report.clear();
        }
    });
    if (dialog.exec() == QDialog::Accepted) {
        QSettings config;
        config.setValue("credentials/tool", tool.text());
        config.setValue("credentials/signer", signer.text());
        config.setValue("credentials/trustAnchors", trust.text());
    }
}
} // namespace huelith::desktop
