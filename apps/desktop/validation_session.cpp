#include "validation_session.h"
#include "application_icon.h"
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWindow>

namespace huelith::desktop {
namespace {
class SessionHeader final : public QLabel {
  public:
    SessionHeader(const QString& title, QWidget* parent) : QLabel(title, parent) {
        setMinimumHeight(36);
        setMargin(10);
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        QLabel::mousePressEvent(event);
    }
};
} // namespace
ValidationSession::ValidationSession(validation::Document& document, SessionKind kind,
                                     QWidget* parent)
    : QDialog(parent), document_(document), session_(document.begin_session()) {
    setWindowTitle(kind == SessionKind::raw ? tr("RAW 会话 · M0 验证") : tr("人像会话 · M0 验证"));
    setWindowIcon(application_icon());
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowModality(Qt::WindowModal);
    setSizeGripEnabled(true);
    resize(1000, 660);
    setMinimumSize(640, 420);
    auto* layout = new QVBoxLayout(this);
    auto* header = new SessionHeader(windowTitle(), this);
    layout->addWidget(header);
    auto* body = new QHBoxLayout;
    layout->addLayout(body, 1);
    auto* categories = new QListWidget(this);
    categories->setFixedWidth(160);
    categories->addItems(kind == SessionKind::raw
                             ? QStringList{tr("基础"), tr("曲线"), tr("色彩"), tr("细节"),
                                           tr("光学"), tr("局部范围")}
                             : QStringList{tr("脸部"), tr("皮肤"), tr("五官"), tr("妆容"),
                                           tr("头发"), tr("身体"), tr("光照"), tr("修复")});
    body->addWidget(categories);
    auto* center = new QLabel(
        tr("M0 会话隔离验证\n\n在右侧输入中文及修改测试参数。\n确定只提交一次事务；取消或 Esc "
           "不修改主状态。\n\n这里不提供 RAW 显影或人像产品功能。"),
        this);
    center->setAlignment(Qt::AlignCenter);
    center->setWordWrap(true);
    body->addWidget(center, 1);
    auto* settings = new QWidget(this);
    settings->setFixedWidth(230);
    auto* fields = new QVBoxLayout(settings);
    fields->addWidget(new QLabel(tr("中文输入 / 草稿内容"), settings));
    text_ = new QLineEdit(QString::fromUtf8(session_.draft.block), settings);
    text_->setObjectName("sessionText");
    fields->addWidget(text_);
    fields->addWidget(new QLabel(tr("事务测试参数"), settings));
    parameter_ = new QDoubleSpinBox(settings);
    parameter_->setObjectName("sessionParameter");
    parameter_->setRange(-100, 100);
    parameter_->setValue(session_.draft.parameter);
    fields->addWidget(parameter_);
    fields->addStretch();
    body->addWidget(settings);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ValidationSession::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
const validation::Session& ValidationSession::session() const noexcept {
    return session_;
}
void ValidationSession::accept() {
    session_.draft.parameter = parameter_->value();
    session_.draft.block = text_->text().toUtf8();
    if (document_.commit(session_))
        QDialog::accept();
    else {
        auto* error = new QLabel(tr("主状态已变化，本次结果未提交。请取消后重新打开。"), this);
        layout()->addWidget(error);
    }
}
} // namespace huelith::desktop
