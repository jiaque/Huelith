#include "raw_dialog.h"
#include "tone_panel.h"
#include "window_header.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace huelith::desktop {
struct RawDialog::Job {
    std::atomic_bool cancelled{};
    std::atomic_int percentage{};
    int index{};
    QString key;
    std::future<editing::RawResult> result;
};
RawDialog::RawDialog(std::vector<editing::Layer> sources, QWidget* parent)
    : QDialog(parent), sources_(std::move(sources)), cache_(sources_.size()),
      cache_keys_(sources_.size()) {
    setObjectName("rawDialog");
    setWindowTitle(tr("RAW 显影"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumSize(940, 620);
    resize(1280, 800);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    auto* menu = new QMenuBar;
    menu->addAction(tr("RAW 显影"))->setEnabled(false);
    root->addWidget(new WindowHeader(menu, this));
    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(12, 8, 12, 8);
    const auto button = [&](const QString& text, const auto& function) {
        auto* result = new QPushButton(text, this);
        result->setAutoDefault(false);
        toolbar->addWidget(result);
        connect(result, &QPushButton::clicked, this, function);
        return result;
    };
    button(tr("适应窗口"), [this] { view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio); });
    button(tr("100%"), [this] { view_->resetTransform(); });
    compare_ = new QCheckBox(tr("对比：已提交结果"), this);
    toolbar->addWidget(compare_);
    connect(compare_, &QCheckBox::toggled, this, &RawDialog::display);
    toolbar->addStretch();
    button(tr("同步曝光与降噪设置"), [this] { synchronize(); });
    root->addLayout(toolbar);
    auto* body = new QHBoxLayout;
    files_ = new QListWidget(this);
    files_->setObjectName("rawFiles");
    files_->setMaximumWidth(150);
    for (const auto& source : sources_)
        files_->addItem(source.name);
    body->addWidget(files_);
    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(scene_, this);
    view_->setObjectName("rawPreview");
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setBackgroundBrush(QColor("#1c2024"));
    body->addWidget(view_, 1);
    auto* panel = new QWidget(this);
    panel->setFixedWidth(420);
    auto* form = new QFormLayout(panel);
    tone_ = new TonePanel({}, panel);
    exposure_ = tone_->findChild<QDoubleSpinBox*>("toneExposure");
    exposure_->setObjectName("rawExposure");
    form->addRow(tone_);
    denoise_ = new QCheckBox(tr("AI RAW 降噪"), panel);
    denoise_->setObjectName("rawDenoise");
    form->addRow(denoise_);
    backend_ = new QComboBox(panel);
    backend_->setObjectName("rawBackend");
    backend_->addItems({"DirectML", "CPU"});
    form->addRow(tr("计算设备"), backend_);
    model_ = new QLineEdit(QSettings().value("raw/model").toString(), panel);
    model_->setObjectName("rawModel");
    form->addRow(tr("模型"), model_);
    auto* browse = new QPushButton(tr("选择模型…"), panel);
    browse->setAutoDefault(false);
    browse->setObjectName("rawBrowse");
    form->addRow(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto file =
            QFileDialog::getOpenFileName(this, tr("选择 Bayer ONNX 模型"), {}, "ONNX (*.onnx)");
        if (!file.isEmpty()) {
            model_->setText(file);
            QSettings().setValue("raw/model", file);
        }
    });
    compute_ = new QPushButton(tr("更新预览"), panel);
    compute_->setObjectName("rawCompute");
    compute_->setAutoDefault(false);
    form->addRow(compute_);
    connect(compute_, &QPushButton::clicked, this, &RawDialog::start);
    auto* stop = new QPushButton(tr("取消计算"), panel);
    stop->setAutoDefault(false);
    form->addRow(stop);
    connect(stop, &QPushButton::clicked, this, [this] {
        if (job_)
            job_->cancelled.store(true);
    });
    progress_ = new QProgressBar(panel);
    progress_->setObjectName("rawProgress");
    form->addRow(progress_);
    message_ = new QLabel(panel);
    message_->setObjectName("rawStatus");
    message_->setWordWrap(true);
    form->addRow(message_);
    body->addWidget(panel);
    root->addLayout(body, 1);
    auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    apply_ = footer->button(QDialogButtonBox::Ok);
    apply_->setObjectName("rawApply");
    apply_->setAutoDefault(false);
    apply_->setDefault(false);
    apply_->setEnabled(false);
    footer->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    root->addWidget(footer);
    connect(footer, &QDialogButtonBox::accepted, this, &RawDialog::accept);
    connect(footer, &QDialogButtonBox::rejected, this, &RawDialog::reject);
    auto* submit = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(submit, &QShortcut::activated, this, &RawDialog::accept);
    timer_ = new QTimer(this);
    timer_->setInterval(30);
    connect(timer_, &QTimer::timeout, this, &RawDialog::finish);
    connect(files_, &QListWidget::currentRowChanged, this, &RawDialog::select);
    tone_->changed = [this] {
        if (!loading_) {
            sources_[current_].raw_recipe.insert("tone", editing::tone_json(tone_->settings()));
            display();
        }
    };
    const auto invalidate = [this] {
        if (!loading_) {
            sources_[current_].raw_recipe.insert("denoise", denoise_->isChecked());
            sources_[current_].raw_recipe.insert("directml", backend_->currentIndex() == 0);
            cache_[current_] = {};
            cache_keys_[current_].clear();
            apply_->setEnabled(false);
            message_->setText(tr("参数已修改，请更新预览"));
        }
    };
    connect(denoise_, &QCheckBox::toggled, this, invalidate);
    connect(backend_, &QComboBox::currentIndexChanged, this, invalidate);
    connect(model_, &QLineEdit::textChanged, this, invalidate);
    files_->setCurrentRow(0);
    QTimer::singleShot(0, this, &RawDialog::start);
}
RawDialog::~RawDialog() {
    if (job_) {
        job_->cancelled.store(true);
        job_->result.wait();
    }
}
void RawDialog::select(int index) {
    if (index < 0 || index >= int(sources_.size()) || job_)
        return;
    current_ = index;
    loading_ = true;
    const auto& recipe = sources_[current_].raw_recipe;
    auto settings = editing::tone_from_json(recipe.value("tone").toObject());
    if (!recipe.contains("tone"))
        settings.exposure = recipe.value("exposure").toDouble();
    tone_->set_settings(settings);
    denoise_->setChecked(recipe.value("denoise").toBool());
    backend_->setCurrentIndex(recipe.value("directml").toBool(true) ? 0 : 1);
    loading_ = false;
    display();
}
void RawDialog::start() {
    if (job_ || closing_ || sources_.empty())
        return;
    editing::RawOptions options;
    options.denoise = denoise_->isChecked();
    options.directml = backend_->currentIndex() == 0;
    options.model_path = model_->text();
    const auto bytes = sources_[current_].raw_source;
    job_ = std::make_unique<Job>();
    job_->index = current_;
    auto* job = job_.get();
    job_->key =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) +
        QString("/%1/%2/%3/v2").arg(options.denoise).arg(options.directml).arg(options.model_path);
    job_->result = std::async(std::launch::async, [job, bytes, options] {
        QTemporaryFile source;
        if (!source.open() || source.write(bytes) != bytes.size() || !source.flush())
            throw editing::EditError(QStringLiteral("无法创建 RAW 会话缓存"));
        return editing::develop_raw(
            source.fileName(), options, job->cancelled,
            [job](int done, int total) { job->percentage.store(done * 100 / std::max(total, 1)); });
    });
    files_->setEnabled(false);
    denoise_->setEnabled(false);
    backend_->setEnabled(false);
    model_->setEnabled(false);
    findChild<QPushButton*>("rawBrowse")->setEnabled(false);
    compute_->setEnabled(false);
    apply_->setEnabled(false);
    message_->setText(tr("正在处理源图像…"));
    timer_->start();
}
void RawDialog::finish() {
    if (!job_)
        return;
    progress_->setValue(job_->percentage.load());
    if (job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    timer_->stop();
    try {
        const auto result = job_->result.get();
        if (!closing_ && !job_->cancelled.load()) {
            cache_[job_->index] = result.image;
            cache_keys_[job_->index] = job_->key;
            auto& recipe = sources_[job_->index].raw_recipe;
            recipe.insert("version", 1);
            recipe.insert("metadata", result.metadata);
            message_->setText(tr("显影完成 · %1 × %2 · 高精度 RGB")
                                  .arg(result.image.width())
                                  .arg(result.image.height()));
        }
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
    job_.reset();
    if (closing_) {
        QDialog::reject();
        return;
    }
    files_->setEnabled(true);
    denoise_->setEnabled(true);
    backend_->setEnabled(true);
    model_->setEnabled(true);
    findChild<QPushButton*>("rawBrowse")->setEnabled(true);
    compute_->setEnabled(true);
    display();
}
void RawDialog::display() {
    scene_->clear();
    auto image = cache_[current_];
    if (compare_->isChecked())
        image = sources_[current_].pixels;
    else if (!image.isNull())
        image = editing::apply_tone(image, tone_->settings());
    if (!image.isNull()) {
        scene_->addPixmap(QPixmap::fromImage(editing::display_image(image)));
        scene_->setSceneRect(QRectF(image.rect()));
        view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
    }
    apply_->setEnabled(!job_ && std::all_of(cache_.begin(), cache_.end(),
                                            [](const QImage& value) { return !value.isNull(); }));
}
void RawDialog::synchronize() {
    if (job_)
        return;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        if (int(i) == current_)
            continue;
        sources_[i].raw_recipe = sources_[current_].raw_recipe;
        sources_[i].raw_recipe.remove("metadata");
        cache_[i] = {};
        cache_keys_[i].clear();
    }
    message_->setText(tr("设置已同步；请逐张更新预览后提交"));
    apply_->setEnabled(sources_.size() == 1 && !cache_[0].isNull());
}
void RawDialog::accept() {
    if (job_ || !apply_->isEnabled())
        return;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        auto tone = editing::tone_from_json(sources_[i].raw_recipe.value("tone").toObject());
        if (!sources_[i].raw_recipe.contains("tone"))
            tone.exposure = sources_[i].raw_recipe.value("exposure").toDouble();
        sources_[i].pixels = editing::apply_tone(cache_[i], tone);
    }
    QDialog::accept();
}
void RawDialog::reject() {
    if (job_) {
        closing_ = true;
        job_->cancelled.store(true);
        message_->setText(tr("正在取消计算并丢弃临时修改…"));
        return;
    }
    QDialog::reject();
}
} // namespace huelith::desktop
