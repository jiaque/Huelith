#include "semantic_dialog.h"
#include "huelith/editing/project.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace huelith::desktop {
SemanticDialog::SemanticDialog(QImage image, editing::SemanticTarget target, QWidget* parent,
                               bool immediate)
    : QDialog(parent), source_(std::move(image)), immediate_(immediate) {
    if (source_.format() == QImage::Format_RGBA32FPx4_Premultiplied)
        source_ = editing::display_image(source_);
    if (source_.width() > 1280 || source_.height() > 1280)
        source_ = source_.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    setObjectName("semanticDialog");
    setWindowTitle(tr("选择范围"));
    resize(820, 650);
    auto* layout = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout;
    target_ = new QComboBox(this);
    target_->setObjectName("semanticTarget");
    target_->addItems(
        {tr("天空"), tr("草地"), tr("植被"), tr("人物"), tr("主体"), tr("背景"), tr("对象")});
    target_->setCurrentIndex(int(target));
    bar->addWidget(target_);
    person_ = new QComboBox(this);
    person_->setObjectName("semanticPerson");
    person_->setIconSize(QSize(56, 56));
    person_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    person_->setMinimumWidth(180);
    bar->addWidget(person_);
    invert_ = new QCheckBox(tr("反转范围"), this);
    invert_->setObjectName("semanticInvert");
    bar->addWidget(invert_);
    bar->addStretch();
    layout->addLayout(bar);
    preview_ = new QLabel(this);
    preview_->setObjectName("semanticPreview");
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(640, 400);
    preview_->installEventFilter(this);
    layout->addWidget(preview_, 1);
    status_ = new QLabel(this);
    status_->setObjectName("semanticStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &SemanticDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &SemanticDialog::reject);
    connect(target_, &QComboBox::currentIndexChanged, this, [this] {
        immediate_ = false;
        populate_candidates();
        refresh();
    });
    connect(person_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(invert_, &QCheckBox::toggled, this, [this] { refresh(); });
    auto* timer = new QTimer(this);
    timer->setInterval(20);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!worker_.valid() ||
            worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            auto result = worker_.get();
            if (result.objects) {
                instances_ = std::move(result.instances);
                instances_ready_ = true;
            } else
                labels_ = std::move(result.labels);
        } catch (const std::exception& error) {
            (worker_objects_ ? instance_error_ : scene_error_) = QString::fromUtf8(error.what());
        }
        populate_candidates();
        refresh();
        if (immediate_ && property("semanticReady").toBool())
            accept();
    });
    timer->start();
    refresh();
}
void SemanticDialog::request_result() {
    const bool objects = int(target()) >= int(editing::SemanticTarget::people);
    const bool ready = objects ? instances_ready_ : !labels_.isNull();
    const auto& error = objects ? instance_error_ : scene_error_;
    setProperty("semanticReady", ready);
    setProperty("semanticError", !error.isEmpty());
    if (ready || !error.isEmpty() || worker_.valid() || cancelled_)
        return;
    const QString filename = objects ? "maskrcnn-r50-v2.onnx" : "pp-mobileseg-base.onnx";
    QString model = QCoreApplication::applicationDirPath() + "/models/" + filename;
    if (!QFileInfo::exists(model))
        model = QDir::current().absoluteFilePath("build/windows-release/models/" + filename);
    worker_objects_ = objects;
    worker_ = std::async(std::launch::async, [this, model, objects] {
        Result result;
        result.objects = objects;
        if (objects)
            result.instances = editing::semantic_instances(source_, model, cancelled_);
        else
            result.labels = editing::semantic_labels(source_, model, cancelled_);
        return result;
    });
}
void SemanticDialog::populate_candidates() {
    const QSignalBlocker block(person_);
    const auto previous = person_->currentData();
    person_->clear();
    const bool people = target() == editing::SemanticTarget::people;
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        const auto& instance = instances_[i];
        if (people && instance.category != 1)
            continue;
        const auto& r = instance.bounds;
        const QRect crop(qRound(double(r.x()) / instance.mask.width() * source_.width()),
                         qRound(double(r.y()) / instance.mask.height() * source_.height()),
                         qRound(double(r.width()) / instance.mask.width() * source_.width()),
                         qRound(double(r.height()) / instance.mask.height() * source_.height()));
        person_->addItem(QIcon(QPixmap::fromImage(source_.copy(crop).scaled(
                             56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                         tr("%1 %2")
                             .arg(editing::semantic_category_name(instance.category))
                             .arg(person_->count() + 1),
                         int(i));
    }
    if (previous.isValid()) {
        const int index = person_->findData(previous);
        if (index >= 0)
            person_->setCurrentIndex(index);
    }
    setProperty("instanceCount", int(instances_.size()));
    setProperty("candidateCount", person_->count());
}
SemanticDialog::~SemanticDialog() {
    cancelled_ = true;
    if (worker_.valid())
        worker_.wait();
}
editing::SemanticTarget SemanticDialog::target() const {
    return editing::SemanticTarget(target_->currentIndex());
}
void SemanticDialog::reject() {
    cancelled_ = true;
    QDialog::reject();
}
void SemanticDialog::accept() {
    if (buttons_->button(QDialogButtonBox::Ok)->isEnabled())
        QDialog::accept();
}
bool SemanticDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == preview_ && event->type() == QEvent::MouseButtonPress &&
        (target() == editing::SemanticTarget::people ||
         target() == editing::SemanticTarget::object)) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const auto size = preview_->pixmap().size();
        if (mouse->button() != Qt::LeftButton || size.isEmpty())
            return false;
        const QPointF p = mouse->position() - QPointF((preview_->width() - size.width()) * .5,
                                                      (preview_->height() - size.height()) * .5);
        if (p.x() < 0 || p.y() < 0 || p.x() >= size.width() || p.y() >= size.height())
            return true;
        int winner = -1;
        double best{};
        for (int i = 0; i < person_->count(); ++i) {
            const auto& instance = instances_[std::size_t(person_->itemData(i).toInt())];
            const int x = std::min(instance.mask.width() - 1,
                                   int(p.x() / size.width() * instance.mask.width()));
            const int y = std::min(instance.mask.height() - 1,
                                   int(p.y() / size.height() * instance.mask.height()));
            const double coverage = instance.mask.constScanLine(y)[x] / 255.;
            const double score = coverage * instance.confidence;
            if (coverage > .5 && score > best) {
                best = score;
                winner = i;
            }
        }
        if (winner >= 0)
            person_->setCurrentIndex(winner);
        return true;
    }
    return QDialog::eventFilter(watched, event);
}
void SemanticDialog::refresh() {
    request_result();
    const auto selected = target();
    person_->setVisible(selected == editing::SemanticTarget::people ||
                        selected == editing::SemanticTarget::object);
    const auto error = int(selected) >= 3 ? instance_error_ : scene_error_;
    if (!property("semanticReady").toBool()) {
        mask_ = {};
        setProperty("selectedPixels", 0);
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
        preview_->setPixmap(QPixmap::fromImage(
            source_.scaled(760, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        status_->setText(error.isEmpty() ? tr("正在识别范围…可以随时取消。") : error);
        return;
    }
    if (int(selected) >= 3) {
        mask_ = QImage(source_.size(), QImage::Format_Grayscale8);
        mask_.fill(0);
        if (selected == editing::SemanticTarget::people ||
            selected == editing::SemanticTarget::object) {
            if (person_->currentIndex() >= 0)
                mask_ = instances_[std::size_t(person_->currentData().toInt())].mask.scaled(
                    source_.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        } else if (!instances_.empty()) {
            // Prefer people as photographic subjects; otherwise use the largest
            // high-confidence object. Each person remains independently selectable.
            const bool people = std::any_of(instances_.begin(), instances_.end(),
                                            [](const auto& item) { return item.category == 1; });
            for (std::size_t i = 0; i < instances_.size(); ++i) {
                const auto& instance = instances_[i];
                if (people ? instance.category != 1 : i != 0)
                    continue;
                const auto candidate = instance.mask.scaled(mask_.size(), Qt::IgnoreAspectRatio,
                                                            Qt::SmoothTransformation);
                for (int y = 0; y < mask_.height(); ++y) {
                    auto* row = mask_.scanLine(y);
                    const auto* other = candidate.constScanLine(y);
                    for (int x = 0; x < mask_.width(); ++x)
                        row[x] = std::max(row[x], other[x]);
                }
            }
            if (selected == editing::SemanticTarget::background)
                mask_.invertPixels();
        }
    } else
        mask_ = editing::semantic_mask(labels_, selected, source_.size());
    if (invert_->isChecked())
        mask_.invertPixels();
    auto visible = source_.convertToFormat(QImage::Format_RGBA8888);
    qint64 covered{};
    for (int y = 0; y < visible.height(); ++y) {
        auto* row = visible.scanLine(y);
        const auto* mask = mask_.constScanLine(y);
        for (int x = 0; x < visible.width(); ++x) {
            covered += mask[x] > 127;
            const double alpha = mask[x] / 510.;
            row[x * 4] = uchar(row[x * 4] * (1 - alpha) + 255 * alpha);
            row[x * 4 + 1] = uchar(row[x * 4 + 1] * (1 - alpha) + 60 * alpha);
            row[x * 4 + 2] = uchar(row[x * 4 + 2] * (1 - alpha) + 60 * alpha);
        }
    }
    preview_->setPixmap(QPixmap::fromImage(
        visible.scaled(760, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(covered > 0);
    status_->setText(
        covered
            ? tr("红色显示选中范围。点击画面中的人物或对象，或选择缩略图。确认后可继续加减选区。")
            : tr("没有识别到该范围，请选择其他类别或使用手工选区。"));
    setProperty("selectedPixels", covered);
}
} // namespace huelith::desktop
