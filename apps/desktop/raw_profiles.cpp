#include "color_band_slider.h"
#include "huelith/editing/raw_profile.h"
#include "huelith/editing/white_balance.h"
#include "raw_dialog.h"
#include "huelith/editing/embedded_preview.h"
#include "tone_panel.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>

namespace huelith::desktop {
namespace {
QJsonArray library() {
    auto result = editing::builtin_raw_profiles();
    const auto custom =
        QJsonDocument::fromJson(QSettings().value("raw/profiles").toByteArray()).array();
    for (const auto& value : custom)
        try {
            result.append(editing::validate_raw_profile(value.toObject()));
        } catch (const std::exception&) {
        }
    return result;
}
bool matching(const QJsonObject& profile, const QJsonObject& recipe) {
    return !profile.contains("matrix1") ||
           profile.value("camera").toString().compare(
               recipe.value("metadata").toObject().value("camera").toString(),
               Qt::CaseInsensitive) == 0;
}
QString identity(const QJsonObject& profile) {
    return profile.value("id").toString("huelith.color");
}
} // namespace
struct RawDialog::ProfileWork {
    std::future<std::vector<std::pair<QString, QImage>>> thumbnails;
    QMap<QString, QImage> thumbnail_cache;
    QString thumbnail_key, pending_key;
    std::future<std::vector<std::pair<int, editing::ToneSettings>>> automatic;
    std::vector<QJsonObject> before;
    std::atomic_bool cancelled{};
    ~ProfileWork() {
        cancelled.store(true);
    }
};
QWidget* RawDialog::setup_profiles(QWidget* parent) {
    auto* wrapper = new QWidget(parent);
    wrapper->setFixedWidth(360);
    auto* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(8, 3, 8, 3);
    auto* commands = new QHBoxLayout;
    commands->addWidget(new QLabel(tr("编辑"), wrapper));
    commands->addStretch();
    auto* automatic = new QPushButton(tr("自动"), wrapper);
    automatic->setObjectName("rawAutoTone");
    automatic->setAutoDefault(false);
    commands->addWidget(automatic);
    connect(automatic, &QPushButton::clicked, this, &RawDialog::auto_tone);
    auto* camera_reference=new QPushButton(tr("相机亮度"),wrapper);camera_reference->setObjectName("rawCameraBrightness");
    camera_reference->setAutoDefault(false);camera_reference->setToolTip(tr("使用内嵌相机预览估计曝光起点；不复制机内色彩风格，可撤销"));commands->addWidget(camera_reference);
    connect(camera_reference,&QPushButton::clicked,this,[this]{
        if(job_||commit_job_||closing_||proxy_source_.isNull())return;
        try{const auto embedded=editing::embedded_preview(sources_[current_]);
            const auto& recipe=sources_[current_].raw_recipe;
            const auto base=editing::apply_raw_profile(editing::apply_white_balance(proxy_source_,recipe),recipe);
            const double exposure=editing::preview_exposure_reference(base,embedded.image);
            history_timer_->stop();record_history();auto tone=editing::tone_from_json(recipe["tone"].toObject());tone.exposure=exposure;
            sources_[current_].raw_recipe.insert("tone",editing::tone_json(tone));select(current_);display();record_history();
            message_->setText(tr("相机预览亮度参考：曝光 %1 EV；仅调整亮度起点，不匹配机内风格").arg(exposure,0,'f',2));
        }catch(const std::exception& e){message_->setText(QString::fromUtf8(e.what()));}
    });
    auto* monochrome = new QPushButton(tr("黑白"), wrapper);
    monochrome->setObjectName("rawMonochrome");
    monochrome->setCheckable(true);
    monochrome->setAutoDefault(false);
    commands->addWidget(monochrome);
    connect(monochrome, &QPushButton::clicked, this, [this] {
        if (loading_ || commit_job_ || closing_)
            return;
        const bool was = identity(sources_[current_].raw_recipe.value("profile").toObject()) ==
                         "huelith.monochrome";
        choose_profile(editing::builtin_raw_profiles()[was ? 0 : 1].toObject());
    });
    layout->addLayout(commands);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("描述文件"), wrapper));
    profile_choice_ = new QComboBox(wrapper);
    profile_choice_->setObjectName("rawProfileChoice");
    row->addWidget(profile_choice_, 1);
    auto* browse = new QToolButton(wrapper);
    browse->setText(QString::fromUtf8("▦"));
    browse->setToolTip(tr("浏览描述文件"));
    browse->setObjectName("rawProfileBrowse");
    row->addWidget(browse);
    layout->addLayout(row);
    connect(browse, &QToolButton::clicked, this,
            [this] { browse_profiles(profile_browser_->isHidden()); });
    connect(profile_choice_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!loading_ && index >= 0)
            choose_profile(profile_choice_->itemData(index).toJsonObject());
    });
    auto* amount_row = new QWidget(wrapper);
    auto* amount_layout = new QHBoxLayout(amount_row);
    amount_layout->setContentsMargins(0, 0, 0, 0);
    amount_layout->addWidget(new QLabel(tr("数量"), amount_row));
    auto* amount_slider = new ColorBandSlider(-1, 0, amount_row);
    amount_slider->setObjectName("rawProfileAmountSlider");
    amount_slider->setRange(0, 200);
    amount_slider->set_default(100);
    amount_layout->addWidget(amount_slider, 1);
    profile_amount_ = new QDoubleSpinBox(amount_row);
    profile_amount_->setObjectName("rawProfileAmount");
    profile_amount_->setRange(0, 200);
    profile_amount_->setDecimals(0);
    amount_layout->addWidget(profile_amount_);
    layout->addWidget(amount_row);
    connect(amount_slider, &QSlider::valueChanged, profile_amount_,
            [this](int value) { profile_amount_->setValue(value); });
    connect(amount_slider, &QSlider::sliderPressed, this, [this] {
        history_timer_->stop();
        record_history();
        parameter_gesture_ = true;
    });
    connect(amount_slider, &QSlider::sliderReleased, this, [this] {
        parameter_gesture_ = false;
        history_timer_->stop();
        record_history();
    });
    connect(
        profile_amount_, &QDoubleSpinBox::valueChanged, this, [this, amount_slider](double amount) {
            const QSignalBlocker blocker(amount_slider);
            amount_slider->setValue(qRound(amount));
            if (loading_ || commit_job_ || closing_)
                return;
            auto profile = sources_[current_].raw_recipe.value("profile").toObject();
            profile.insert("amount", amount);
            sources_[current_].raw_recipe.insert("profile", editing::validate_raw_profile(profile));
            if (!parameter_gesture_)
                history_timer_->start();
            display();
        });
    profile_browser_ = new QWidget(parent);
    profile_browser_->setObjectName("rawProfileBrowser");
    profile_browser_->setFixedWidth(360);
    auto* browser_layout = new QVBoxLayout(profile_browser_);
    browser_layout->setContentsMargins(8, 0, 8, 4);
    auto* header = new QHBoxLayout;
    auto* back = new QPushButton(tr("返回编辑"), profile_browser_);
    back->setObjectName("rawProfileBack");
    back->setAutoDefault(false);
    header->addWidget(back);
    connect(back, &QPushButton::clicked, this, [this] { browse_profiles(false); });
    auto* import = new QPushButton(tr("导入…"), profile_browser_);
    import->setObjectName("rawProfileImport");
    import->setAutoDefault(false);
    header->addWidget(import);
    browser_layout->addLayout(header);
    connect(import, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("导入 RAW 描述文件"), {},
                                                       tr("Huelith RAW 描述文件 (*.hrawprofile)"));
        if (path.isEmpty())
            return;
        try {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
                throw editing::EditError(tr("无法读取描述文件"));
            const auto profile = editing::read_raw_profile(file.readAll());
            auto values =
                QJsonDocument::fromJson(QSettings().value("raw/profiles").toByteArray()).array();
            bool replaced{};
            for (int i = 0; i < values.size(); ++i)
                if (identity(values[i].toObject()) == identity(profile)) {
                    values[i] = profile;
                    replaced = true;
                    break;
                }
            if (!replaced) {
                if (values.size() >= 64)
                    throw editing::EditError(tr("自定义描述文件最多64个"));
                values.append(profile);
            }
            QSettings settings;
            settings.setValue("raw/profiles", QJsonDocument(values).toJson(QJsonDocument::Compact));
            settings.sync();
            if (settings.status() != QSettings::NoError)
                throw editing::EditError(tr("无法保存描述文件库"));
            refresh_profiles();
            browse_profiles(true);
            message_->setText(matching(profile, sources_[current_].raw_recipe)
                                  ? tr("描述文件已导入")
                                  : tr("已导入；只有匹配相机型号的照片才能使用此描述文件"));
        } catch (const std::exception& e) {
            message_->setText(QString::fromUtf8(e.what()));
        }
    });
    profile_filter_ = new QComboBox(profile_browser_);
    profile_filter_->setObjectName("rawProfileFilter");
    profile_filter_->addItems(
        {tr("全部"), tr("收藏"), tr("Huelith RAW"), tr("创意"), tr("相机匹配 / 自定义")});
    browser_layout->addWidget(profile_filter_);
    profile_list_ = new QListWidget(profile_browser_);
    profile_list_->setObjectName("rawProfileList");
    profile_list_->setViewMode(QListView::IconMode);
    profile_list_->setResizeMode(QListView::Adjust);
    profile_list_->setMovement(QListView::Static);
    profile_list_->setIconSize(QSize(144, 96));
    profile_list_->setGridSize(QSize(160, 126));
    browser_layout->addWidget(profile_list_, 1);
    connect(profile_filter_, &QComboBox::currentIndexChanged, this, [this] { refresh_profiles(); });
    connect(profile_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        choose_profile(item->data(Qt::UserRole).toJsonObject());
    });
    auto* favorite = new QPushButton(tr("★ 收藏 / 取消收藏"), profile_browser_);
    favorite->setAutoDefault(false);
    favorite->setObjectName("rawProfileFavorite");
    browser_layout->addWidget(favorite);
    connect(favorite, &QPushButton::clicked, this, [this] {
        if (auto* item = profile_list_->currentItem()) {
            auto ids = QSettings().value("raw/profileFavorites").toStringList();
            const auto id = identity(item->data(Qt::UserRole).toJsonObject());
            if (ids.contains(id))
                ids.removeAll(id);
            else
                ids.append(id);
            QSettings().setValue("raw/profileFavorites", ids);
            refresh_profiles();
        }
    });
    profile_browser_->hide();
    auto* shortcut = new QShortcut(QKeySequence("Ctrl+U"), this);
    connect(shortcut, &QShortcut::activated, this, &RawDialog::auto_tone);
    profile_work_ = std::make_shared<ProfileWork>();
    auto* timer = new QTimer(this);
    timer->setInterval(25);
    connect(timer, &QTimer::timeout, this, [this] {
        const auto work = profile_work_;
        if (work->thumbnails.valid() &&
            work->thumbnails.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                const auto thumbnails = work->thumbnails.get();
                work->thumbnail_cache.clear();
                work->thumbnail_key = work->pending_key;
                for (const auto& [id, image] : thumbnails) {
                    work->thumbnail_cache.insert(id, image);
                    for (int i = 0; i < profile_list_->count(); ++i)
                        if (identity(profile_list_->item(i)->data(Qt::UserRole).toJsonObject()) ==
                            id)
                            profile_list_->item(i)->setIcon(QPixmap::fromImage(image));
                }
                if (!closing_ &&
                    work->thumbnail_key !=
                        cache_keys_[current_] +
                            QString::fromUtf8(
                                QJsonDocument(
                                    sources_[current_].raw_recipe.value("whiteBalance").toObject())
                                    .toJson(QJsonDocument::Compact)))
                    browse_profiles(!profile_browser_->isHidden());
            } catch (const std::exception& e) {
                if (!closing_)
                    message_->setText(QString::fromUtf8(e.what()));
            }
        }
        if (work->automatic.valid() &&
            work->automatic.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                const auto values = work->automatic.get();
                bool same = !closing_ && sources_.size() == work->before.size();
                for (std::size_t i = 0; same && i < sources_.size(); ++i)
                    same = sources_[i].raw_recipe == work->before[i];
                if (same) {
                    history_timer_->stop();
                    record_history();
                    for (const auto& [index, tone] : values) {
                        sources_[index].raw_recipe.insert("tone", editing::tone_json(tone));
                        sources_[index].raw_recipe.insert("exposure", tone.exposure);
                    }
                    record_history();
                    select(current_);
                    message_->setText(tr("已按各照片内容自动调整光线"));
                } else if (!closing_)
                    message_->setText(tr("自动调整期间设置已改变，旧结果未应用"));
            } catch (const std::exception& e) {
                if (!closing_)
                    message_->setText(QString::fromUtf8(e.what()));
            }
            setProperty("autoToneBusy", false);
            findChild<QPushButton*>("rawAutoTone")->setEnabled(true);
        }
    });
    timer->start();
    refresh_profiles();
    return wrapper;
}
void RawDialog::refresh_profiles() {
    if (!profile_choice_ || current_ < 0 || current_ >= int(sources_.size()))
        return;
    const auto recipe = sources_[current_].raw_recipe;
    const auto selected = recipe.value("profile").toObject();
    const auto id = identity(selected);
    const auto favorites = QSettings().value("raw/profileFavorites").toStringList();
    auto values = library();
    bool found = false;
    for (const auto& p : values)
        found |= identity(p.toObject()) == id;
    if (!found && !selected.isEmpty())
        values.append(selected);
    const QSignalBlocker blocker(profile_choice_), amount_blocker(profile_amount_);
    profile_choice_->clear();
    const auto thumbnail_key =
        cache_keys_[current_] +
        QString::fromUtf8(
            QJsonDocument(recipe.value("whiteBalance").toObject()).toJson(QJsonDocument::Compact));
    profile_list_->clear();
    for (const auto& value : values) {
        const auto profile = value.toObject();
        if (!matching(profile, recipe))
            continue;
        const auto name = profile.value("name").toString();
        const auto key = identity(profile);
        profile_choice_->addItem(name, profile);
        if (key == id)
            profile_choice_->setCurrentIndex(profile_choice_->count() - 1);
        const int filter = profile_filter_->currentIndex();
        const bool is_favorite = favorites.contains(key);
        if ((filter == 1 && !is_favorite) || (filter == 2 && profile.value("group") != "raw") ||
            (filter == 3 && profile.value("group") != "creative") ||
            (filter == 4 && key.startsWith("huelith.")))
            continue;
        auto* item = new QListWidgetItem((is_favorite ? QString::fromUtf8("★ ") : QString{}) + name,
                                         profile_list_);
        item->setData(Qt::UserRole, profile);
        if (profile_work_ && thumbnail_key == profile_work_->thumbnail_key)
            item->setIcon(QPixmap::fromImage(profile_work_->thumbnail_cache.value(key)));
        if (key == id)
            profile_list_->setCurrentItem(item);
    }
    profile_amount_->setValue(selected.value("amount").toDouble(100));
    auto* amount_slider = findChild<QSlider*>("rawProfileAmountSlider");
    const QSignalBlocker slider_blocker(amount_slider);
    amount_slider->setValue(qRound(profile_amount_->value()));
    profile_amount_->parentWidget()->setVisible(selected.value("adjustable").toBool());
    const QSignalBlocker mono_blocker(findChild<QPushButton*>("rawMonochrome"));
    findChild<QPushButton*>("rawMonochrome")->setChecked(id == "huelith.monochrome");
}
void RawDialog::choose_profile(QJsonObject profile) {
    if (loading_ || commit_job_ || closing_)
        return;
    try {
        profile = editing::validate_raw_profile(profile);
        if (!matching(profile, sources_[current_].raw_recipe))
            throw editing::EditError(tr("描述文件与相机不匹配"));
        for (auto* item : files_->selectedItems())
            if (!item->isHidden() && !matching(profile, sources_[files_->row(item)].raw_recipe))
                throw editing::EditError(
                    tr("所选照片包含其他相机型号，不能共同应用此相机描述文件"));
        history_timer_->stop();
        record_history();
        sources_[current_].raw_recipe.insert("profile", profile);
        display();
        record_history();
        refresh_profiles();
    } catch (const std::exception& e) {
        message_->setText(QString::fromUtf8(e.what()));
    }
}
void RawDialog::browse_profiles(bool open) {
    profile_browser_->setVisible(open);
    findChild<QScrollArea*>("rawEditingScroll")->setVisible(!open);
    if (!open || proxy_source_.isNull() || profile_work_->thumbnails.valid())
        return;
    refresh_profiles();
    const auto source = proxy_source_;
    const auto recipe = sources_[current_].raw_recipe;
    const auto profiles = library();
    profile_work_->pending_key =
        cache_keys_[current_] +
        QString::fromUtf8(
            QJsonDocument(recipe.value("whiteBalance").toObject()).toJson(QJsonDocument::Compact));
    profile_work_->thumbnails = std::async(std::launch::async, [source, recipe, profiles] {
        const auto thumb = source.scaled(192, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const auto balanced = editing::apply_white_balance(thumb, recipe);
        std::vector<std::pair<QString, QImage>> result;
        for (const auto& value : profiles) {
            const auto profile = value.toObject();
            if (!matching(profile, recipe))
                continue;
            auto one = recipe;
            one.insert("profile", profile);
            result.emplace_back(identity(profile),
                                editing::display_image(editing::apply_raw_profile(balanced, one)));
        }
        return result;
    });
}
void RawDialog::auto_tone() {
    if (job_ || commit_job_ || closing_ || profile_work_->automatic.valid())
        return;
    std::vector<std::pair<int, std::function<editing::RawResult()>>> images;
    auto work = profile_work_;
    work->before.clear();
    for (const auto& source : sources_)
        work->before.push_back(source.raw_recipe);
    auto selected = files_->selectedItems();
    if (selected.isEmpty() && files_->currentItem())
        selected.append(files_->currentItem());
    for (auto* item : selected) {
        if (item->isHidden())
            continue;
        const int index = files_->row(item);
        if (!cache_.contains(cache_keys_[index])) {
            message_->setText(tr("请先更新所选照片预览，再执行自动调整"));
            return;
        }
        images.emplace_back(index, cache_.read_task(cache_keys_[index]));
    }
    if (images.empty())
        return;
    history_timer_->stop();
    record_history();
    const auto recipes = work->before;
    setProperty("autoToneBusy", true);
    findChild<QPushButton*>("rawAutoTone")->setEnabled(false);
    work->automatic = std::async(std::launch::async, [images = std::move(images), recipes] {
        std::vector<std::pair<int, editing::ToneSettings>> result;
        for (const auto& [index, read] : images) {
            const auto& recipe = recipes[index];
            auto proxy = read().image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            proxy = editing::apply_raw_profile(editing::apply_white_balance(proxy, recipe), recipe);
            result.emplace_back(
                index, editing::automatic_raw_tone(
                           proxy, editing::tone_from_json(recipe.value("tone").toObject())));
        }
        return result;
    });
}
void RawDialog::finish_profiles(){
    if(!profile_work_)return;
    if(profile_work_->automatic.valid())profile_work_->automatic.wait();
    if(profile_work_->thumbnails.valid())profile_work_->thumbnails.wait();
}
} // namespace huelith::desktop
