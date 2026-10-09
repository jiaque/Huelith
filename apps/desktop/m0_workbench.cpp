#include "application_icon.h"
#include "main_window.h"
#include "validation_session.h"
#include "window_header.h"
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {
class Workbench final : public QMainWindow {
  public:
    Workbench() {
        setWindowTitle(QStringLiteral("Huelith · M0 验证工作台"));
        setWindowIcon(huelith::desktop::application_icon());
        setWindowFlag(Qt::FramelessWindowHint);
        resize(1100, 740);
        setMinimumSize(720, 480);
        setMenuWidget(new huelith::desktop::WindowHeader(new QMenuBar(this), this));
        auto* body = new QWidget(this);
        auto* layout = new QVBoxLayout(body);
        setCentralWidget(body);
        auto* title = new QLabel(QStringLiteral("M0 基础验证工作台"), body);
        title->setObjectName("welcomeTitle");
        layout->addWidget(title);
        layout->addWidget(new QLabel(
            QStringLiteral("验证专用窗口、取消 / 一次提交、撤销、原型保存与重开。图像算法和 GPU "
                           "另由自动测试验证。"),
            body));
        state_ = new QLabel(body);
        state_->setWordWrap(true);
        layout->addWidget(state_);
        const auto button = [&](const QString& name, auto callback) {
            auto* item = new QPushButton(name, body);
            layout->addWidget(item);
            connect(item, &QPushButton::clicked, this, callback);
        };
        button(QStringLiteral("打开 RAW 验证会话"),
               [this] { session(huelith::desktop::SessionKind::raw); });
        button(QStringLiteral("打开人像验证会话"),
               [this] { session(huelith::desktop::SessionKind::portrait); });
        button(QStringLiteral("撤销一次事务"), [this] {
            static_cast<void>(document_.undo());
            refresh();
        });
        button(QStringLiteral("重做一次事务"), [this] {
            static_cast<void>(document_.redo());
            refresh();
        });
        button(QStringLiteral("保存 M0 原型状态…"), [this] {
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存验证状态"), {},
                                                           "M0 (*.huelith-m0)");
            if (path.isEmpty())
                return;
            const auto snap = document_.snapshot();
            const auto error = huelith::validation::save_snapshot(path, snap);
            if (error.isEmpty())
                document_.mark_saved(snap.revision);
            refresh();
            if (!error.isEmpty())
                state_->setText(error);
        });
        button(QStringLiteral("重开 M0 原型状态…"), [this] {
            const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("打开验证状态"), {},
                                                           "M0 (*.huelith-m0)");
            if (path.isEmpty())
                return;
            auto loaded = huelith::validation::load_snapshot(path);
            if (loaded.error.isEmpty()) {
                document_ = huelith::validation::Document(std::move(loaded.state));
                refresh();
            } else
                state_->setText(loaded.error);
        });
        layout->addStretch();
        QSettings settings;
        const auto geometry = settings.value("m0/geometry").toByteArray();
        if (!geometry.isEmpty())
            restoreGeometry(geometry);
        bool visible{};
        for (const auto* display : QGuiApplication::screens())
            visible |= display->availableGeometry().intersects(frameGeometry());
        if (!visible)
            move(QGuiApplication::primaryScreen()->availableGeometry().topLeft());
        refresh();
    }

  protected:
    void closeEvent(QCloseEvent* event) override {
        QSettings settings;
        settings.setValue("m0/geometry", saveGeometry());
        event->accept();
    }

  private:
    void session(huelith::desktop::SessionKind kind) {
        huelith::desktop::ValidationSession dialog(document_, kind, this);
        dialog.exec();
        refresh();
    }
    void refresh() {
        const auto value = document_.snapshot();
        state_->setText(QStringLiteral("文档：%1\n修订：%2；参数：%3；未保存：%4\n内容：%5")
                            .arg(value.state.document_id)
                            .arg(value.revision)
                            .arg(value.state.parameter)
                            .arg(document_.dirty() ? "是" : "否")
                            .arg(QString::fromUtf8(value.state.block)));
    }
    huelith::validation::Document document_{{"m0-validation", QByteArray("initial"), 0.0}};
    QLabel* state_{};
};
} // namespace
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Huelith");
    QCoreApplication::setApplicationName("M0Validation");
    huelith::desktop::apply_dark_theme();
    Workbench window;
    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(0, &window, [&] { app.exit(window.grab().isNull() ? 1 : 0); });
    else
        window.show();
    return app.exec();
}
