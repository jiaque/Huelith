#include "huelith/editing/raw_recipe.h"
#include "raw_dialog.h"
#include "tone_panel.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

namespace huelith::desktop {
namespace {
constexpr auto mime = "application/x-huelith-raw-settings";
// Keep an in-process copy only when Windows refuses clipboard ownership.
// Normal system clipboard replacement must still invalidate our old copy.
QByteArray local_settings_copy;
bool local_settings_only{};
editing::RawGroups all_groups() {
    editing::RawGroups result;
    result.fill(true);
    return result;
}
QJsonArray presets() {
    return QJsonDocument::fromJson(QSettings().value("raw/presets").toByteArray()).array();
}
void write_presets(const QJsonArray& values) {
    QSettings settings;
    settings.setValue("raw/presets", QJsonDocument(values).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw editing::EditError(QStringLiteral("无法保存 RAW 预设库"));
}
} // namespace
void RawDialog::setup_settings_menu(QMenuBar* bar) {
    auto* menu = bar->addMenu(tr("设置"));
    auto* copy = menu->addAction(tr("复制编辑设置"));
    copy->setObjectName("rawCopySettings");
    copy->setShortcut(QKeySequence("Ctrl+Alt+C"));
    connect(copy, &QAction::triggered, this, [this] {
        if (job_ || commit_job_)
            return;
        auto* data = new QMimeData;
        local_settings_copy = QJsonDocument(editing::raw_preset(
            tr("已复制设置"), sources_[current_].raw_recipe, all_groups())).toJson();
        data->setData(mime, local_settings_copy);
        QApplication::clipboard()->setMimeData(data);
        const auto* current = QApplication::clipboard()->mimeData();
        local_settings_only = !current || current->data(mime) != local_settings_copy;
        message_->setText(local_settings_only
                              ? tr("已在本应用内复制编辑设置；系统剪贴板当前不可写。")
                              : tr("已复制编辑设置"));
    });
    auto* paste = menu->addAction(tr("粘贴编辑设置…"));
    paste->setObjectName("rawPasteSettings");
    paste->setShortcut(QKeySequence("Ctrl+Alt+V"));
    connect(paste, &QAction::triggered, this, [this] { manage_settings(2); });
    menu->addSeparator();
    for (int mode = 0; mode < 2; ++mode) {
        auto* action = menu->addAction(mode == 0 ? tr("预设…") : tr("快照…"));
        action->setObjectName(mode == 0 ? "rawPresets" : "rawSnapshots");
        action->setShortcut(QKeySequence(mode == 0 ? "Shift+P" : "Ctrl+Shift+S"));
        connect(action, &QAction::triggered, this, [this, mode] { manage_settings(mode); });
    }
}
void RawDialog::apply_recipe_settings(const QJsonObject& preset) {
    history_timer_->stop();
    record_history();
    tone_->finish_target(true);
    tone_->set_target_mode(-1);
    const auto groups = editing::raw_preset_groups(preset);
    auto next = editing::transfer_raw_settings(preset.value("recipe").toObject(),
                                               sources_[current_].raw_recipe, groups);
    if (next == sources_[current_].raw_recipe)
        return;
    sources_[current_].raw_recipe = std::move(next);
    if (groups[7])
        cache_keys_[current_].clear();
    record_history();
    select(current_);
}
void RawDialog::manage_settings(int initial_mode) {
    if (job_ || commit_job_)
        return;
    QJsonObject clipboard;
    try {
        if (initial_mode == 2) {
            const auto* current = QApplication::clipboard()->mimeData();
            const auto bytes = local_settings_only ? local_settings_copy
                               : current ? current->data(mime) : QByteArray{};
            clipboard = editing::read_raw_preset(bytes);
        }
    } catch (const std::exception& e) {
        message_->setText(QString::fromUtf8(e.what()));
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName("rawSettingsDialog");
    dialog.setWindowTitle(tr("RAW 设置"));
    dialog.resize(620, 620);
    auto* root = new QVBoxLayout(&dialog);
    auto* tabs = new QTabBar(&dialog);
    tabs->setObjectName("rawSettingsTabs");
    tabs->addTab(tr("预设"));
    tabs->addTab(tr("快照"));
    tabs->setCurrentIndex(initial_mode == 1 ? 1 : 0);
    tabs->setVisible(initial_mode != 2);
    root->addWidget(tabs);
    auto* list = new QListWidget(&dialog);
    list->setObjectName("rawSettingsList");
    root->addWidget(list, 1);
    list->setVisible(initial_mode != 2);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName("rawSettingsName");
    name->setMaxLength(120);
    name->setPlaceholderText(tr("输入名称"));
    root->addWidget(name);
    name->setVisible(initial_mode != 2);
    auto* grid = new QGridLayout;
    std::array<QCheckBox*, 14> checks{};
    const auto labels = editing::raw_group_names();
    for (int i = 0; i < 14; ++i) {
        auto* check = new QCheckBox(labels[i], &dialog);
        check->setObjectName(QString("rawPresetGroup%1").arg(i));
        check->setChecked(i < 8 || i == 10 || i == 11);
        checks[i] = check;
        grid->addWidget(check, i % 7, i / 7);
    }
    root->addLayout(grid);
    auto* groups = new QHBoxLayout;
    for (bool on : {true, false}) {
        auto* button = new QPushButton(on ? tr("全选") : tr("全不选"), &dialog);
        button->setObjectName(on ? "rawPresetAll" : "rawPresetNone");
        groups->addWidget(button);
        connect(button, &QPushButton::clicked, &dialog, [checks, on] {
            for (auto* check : checks)
                check->setChecked(on);
        });
    }
    root->addLayout(groups);
    auto* status = new QLabel(&dialog);
    status->setObjectName("rawSettingsStatus");
    status->setWordWrap(true);
    root->addWidget(status);
    auto* row = new QHBoxLayout;
    root->addLayout(row);
    const auto collection = [&] {
        return tabs->currentIndex() == 0
                   ? presets()
                   : sources_[current_].raw_recipe.value("snapshots").toArray();
    };
    const auto selected_groups = [&] {
        auto result = all_groups();
        if (tabs->currentIndex() == 0 || initial_mode == 2)
            for (int i = 0; i < 14; ++i)
                result[i] = checks[i]->isChecked();
        return result;
    };
    const auto refresh = [&](int selected) {
        const QSignalBlocker block(list);
        list->clear();
        for (const auto& value : collection())
            list->addItem(value.toObject().value("name").toString());
        list->setCurrentRow(std::min(selected, list->count() - 1));
        const auto values = collection();
        const int index = list->currentRow();
        if (index >= 0 && index < values.size()) {
            const auto entry = values[index].toObject();
            name->setText(entry.value("name").toString());
            const auto selected_groups = editing::raw_preset_groups(entry);
            for (int i = 0; i < 14; ++i)
                checks[i]->setChecked(selected_groups[i]);
        }
        for (auto* check : checks)
            check->setEnabled(tabs->currentIndex() == 0);
        if (tabs->currentIndex() == 1)
            for (auto* check : checks)
                check->setChecked(true);
    };
    const auto save_collection = [&](const QJsonArray& values) {
        if (tabs->currentIndex() == 0)
            write_presets(values);
        else {
            history_timer_->stop();
            record_history();
            sources_[current_].raw_recipe.insert("snapshots", values);
            record_history();
        }
    };
    const auto safe = [&](const std::function<void()>& operation) {
        try {
            operation();
            status->clear();
        } catch (const std::exception& e) {
            status->setText(QString::fromUtf8(e.what()));
        }
    };
    const auto button = [&](const QString& label, const char* id,
                            const std::function<void()>& callback) {
        auto* result = new QPushButton(label, &dialog);
        result->setObjectName(QLatin1String(id));
        result->setAutoDefault(false);
        result->setVisible(initial_mode != 2);
        row->addWidget(result);
        connect(result, &QPushButton::clicked, &dialog, [&, callback] { safe(callback); });
    };
    button(tr("新建"), "rawPresetNew", [&] {
        auto values = collection();
        const auto entry =
            editing::raw_preset(name->text(), sources_[current_].raw_recipe, selected_groups());
        for (const auto& value : values)
            if (value.toObject().value("name") == entry.value("name"))
                throw editing::EditError(tr("此名称已存在，请更换名称或使用更新"));
        values.append(entry);
        save_collection(values);
        refresh(values.size() - 1);
    });
    button(tr("更新"), "rawPresetUpdate", [&] {
        auto values = collection();
        const int index = list->currentRow();
        if (index < 0)
            return;
        values[index] = editing::raw_preset(values[index].toObject().value("name").toString(),
                                            sources_[current_].raw_recipe, selected_groups());
        save_collection(values);
        refresh(index);
    });
    button(tr("重命名"), "rawPresetRename", [&] {
        auto values = collection();
        const int index = list->currentRow();
        if (index < 0)
            return;
        auto entry = values[index].toObject();
        const auto text = name->text().trimmed();
        if (text.isEmpty())
            throw editing::EditError(tr("请输入名称"));
        for (int i = 0; i < values.size(); ++i)
            if (i != index && values[i].toObject().value("name") == text)
                throw editing::EditError(tr("此名称已存在"));
        entry.insert("name", text);
        values[index] = entry;
        save_collection(values);
        refresh(index);
    });
    button(tr("删除"), "rawPresetDelete", [&] {
        auto values = collection();
        const int index = list->currentRow();
        if (index < 0)
            return;
        values.removeAt(index);
        save_collection(values);
        refresh(index);
    });
    button(tr("导入…"), "rawPresetImport", [&] {
        const auto path = QFileDialog::getOpenFileName(&dialog, tr("导入 RAW 预设"), {},
                                                       tr("Huelith RAW 预设 (*.hrawpreset)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw editing::EditError(file.errorString());
        auto entry = editing::read_raw_preset(file.read(16 * 1024 * 1024 + 1));
        auto values = collection();
        QString base = entry.value("name").toString(), candidate = base;
        int suffix = 2;
        const auto exists = [&] {
            for (const auto& value : values)
                if (value.toObject().value("name") == candidate)
                    return true;
            return false;
        };
        while (exists())
            candidate = base + QString(" (%1)").arg(suffix++);
        entry.insert("name", candidate);
        values.append(entry);
        save_collection(values);
        refresh(values.size() - 1);
    });
    button(tr("导出…"), "rawPresetExport", [&] {
        const int index = list->currentRow();
        if (index < 0)
            return;
        const auto path = QFileDialog::getSaveFileName(&dialog, tr("导出 RAW 预设"), {},
                                                       tr("Huelith RAW 预设 (*.hrawpreset)"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        const auto data = QJsonDocument(collection()[index].toObject()).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            throw editing::EditError(file.errorString());
    });
    connect(tabs, &QTabBar::currentChanged, &dialog, [&] { refresh(0); });
    connect(list, &QListWidget::currentRowChanged, &dialog, [&](int index) {
        const auto values = collection();
        if (index < 0 || index >= values.size())
            return;
        const auto entry = values[index].toObject();
        name->setText(entry.value("name").toString());
        const auto selection = editing::raw_preset_groups(entry);
        for (int i = 0; i < 14; ++i)
            checks[i]->setChecked(selection[i]);
    });
    auto* footer = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, &dialog);
    root->addWidget(footer);
    footer->button(QDialogButtonBox::Apply)->setObjectName("rawPresetApply");
    connect(footer->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
        safe([&] {
            QJsonObject entry = clipboard;
            if (initial_mode != 2) {
                const int index = list->currentRow();
                if (index < 0)
                    return;
                entry = collection()[index].toObject();
            }
            auto groups_to_apply = selected_groups();
            const auto saved = editing::raw_preset_groups(entry);
            for (int i = 0; i < 14; ++i)
                groups_to_apply[i] = groups_to_apply[i] && saved[i];
            apply_recipe_settings(editing::raw_preset(
                entry.value("name").toString(), entry.value("recipe").toObject(), groups_to_apply));
            dialog.accept();
        });
    });
    connect(footer, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (initial_mode != 2)
        refresh(0);
    dialog.exec();
}
} // namespace huelith::desktop
