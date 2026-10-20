#include "huelith/editing/image_storage.h"
#include "export_dialog.h"
#include "canvas_view.h"
#include "window_header.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace huelith::desktop {
namespace {
QString output_name(QString pattern, QString name, int size, int index) {
    name.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])"), "_");
    pattern.replace("{name}", name);
    pattern.replace("{size}", size == 0 ? QStringLiteral("original") : QString::number(size));
    pattern.replace("{index}", QString::number(index).rightJustified(3, '0'));
    if (pattern.trimmed().isEmpty() ||
        pattern.contains(QRegularExpression(R"([<>:"/\\|?*{}\x00-\x1f])")) ||
        pattern.endsWith('.') || pattern.endsWith(' '))
        throw editing::EditError(QStringLiteral("命名规则包含无效字符或未知占位符"));
    return pattern;
}
QList<int> parse_sizes(QString text) {
    text.replace(QChar(0xff0c), ',');
    QList<int> result;
    for (const auto& part : text.split(',', Qt::SkipEmptyParts)) {
        bool ok{};
        const auto token = part.trimmed();
        const int value = token == QStringLiteral("原图") ? 0 : token.toInt(&ok);
        if ((token != QStringLiteral("原图") && !ok) || value < 0 || value > 65536)
            throw editing::EditError(
                QStringLiteral("尺寸请输入原图或 1–65536 的长边像素，以逗号分隔"));
        if (!result.contains(value))
            result.append(value);
    }
    if (result.isEmpty() || result.size() > 12)
        throw editing::EditError(QStringLiteral("请选择 1–12 个导出尺寸"));
    return result;
}
} // namespace
ExportDialog::ExportDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("exportQueueDialog");
    setWindowTitle(tr("批量导出与任务队列"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    resize(920, 680);
    auto* layout = new QVBoxLayout(this);
    auto* title = new QMenuBar;
    title->addAction(tr("批量导出与任务队列"))->setEnabled(false);
    layout->addWidget(new WindowHeader(title, this));
    auto* setup = new QHBoxLayout;
    files_ = new QListWidget(this);
    files_->setObjectName("exportQueueSources");
    files_->setMaximumWidth(220);
    setup->addWidget(files_);
    auto* form = new QFormLayout;
    directory_ = new QLineEdit(QSettings().value("exportQueue/directory").toString(), this);
    directory_->setObjectName("exportQueueDirectory");
    auto* directory_row = new QHBoxLayout;
    directory_row->addWidget(directory_);
    auto* browse = new QPushButton(tr("选择目录…"), this);
    directory_row->addWidget(browse);
    form->addRow(tr("输出目录"), directory_row);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path =
            QFileDialog::getExistingDirectory(this, tr("输出目录"), directory_->text());
        if (!path.isEmpty())
            directory_->setText(path);
    });
    sizes_ = new QLineEdit(QSettings().value("exportQueue/sizes", "原图, 2048").toString(), this);
    sizes_->setObjectName("exportQueueSizes");
    form->addRow(tr("长边尺寸"), sizes_);
    naming_ = new QLineEdit(
        QSettings().value("exportQueue/naming", "{name}_{size}_{index}").toString(), this);
    naming_->setObjectName("exportQueueNaming");
    form->addRow(tr("命名规则"), naming_);
    format_ = new QComboBox(this);
    format_->setObjectName("exportQueueFormat");
    format_->addItems({tr("PNG · 8 位"), tr("PNG · 16 位"), tr("JPEG · 8 位"), tr("TIFF · 8 位"),
                       tr("TIFF · 16 位")});
    form->addRow(tr("格式预设"), format_);
    profile_ = new QComboBox(this);
    profile_->setObjectName("exportQueueProfile");
    profile_->addItems({"sRGB", "Adobe RGB", "Display P3", "ProPhoto RGB"});
    form->addRow(tr("输出色彩空间 / 嵌入 ICC"), profile_);
    quality_ = new QSpinBox(this);
    quality_->setObjectName("exportQueueQuality");
    quality_->setRange(1, 100);
    quality_->setValue(95);
    form->addRow(tr("JPEG 质量"), quality_);
    alpha_ = new QCheckBox(tr("保留透明度"), this);
    alpha_->setObjectName("exportQueueAlpha");
    alpha_->setChecked(true);
    form->addRow(alpha_);
    matte_ = new QPushButton(tr("背景填充色：#ffffff"), this);
    matte_->setObjectName("exportQueueMatte");
    matte_->setProperty("matte", QColor(Qt::white));
    form->addRow(matte_);
    connect(matte_, &QPushButton::clicked, this, [this] {
        const auto color = QColorDialog::getColor(matte_->property("matte").value<QColor>(), this,
                                                  tr("输出背景色"));
        if (color.isValid()) {
            matte_->setProperty("matte", color);
            matte_->setText(tr("背景填充色：%1").arg(color.name()));
        }
    });
    auto* metadata_row=new QHBoxLayout;
    const QStringList names{tr("EXIF"),tr("IPTC"),tr("XMP"),tr("版权/作者"),tr("GPS 位置")};
    for(int i=0;i<5;++i){metadata_[i]=new QCheckBox(names[i],this);
        metadata_[i]->setObjectName(QString("exportMetadata%1").arg(i));
        metadata_[i]->setChecked(QSettings().value(QString("exportQueue/metadata%1").arg(i),i!=4).toBool());
        metadata_row->addWidget(metadata_[i]);}
    form->addRow(tr("保留元数据"),metadata_row);
    conflict_ = new QComboBox(this);
    conflict_->setObjectName("exportQueueConflict");
    conflict_->addItems(
        {tr("自动编号（默认）"), tr("跳过已有文件"), tr("覆盖已有输出（保护原始素材）")});
    form->addRow(tr("重名处理"), conflict_);
    example_ = new QLabel(this);
    example_->setWordWrap(true);
    form->addRow(tr("目标路径示例"), example_);
    setup->addLayout(form, 1);
    layout->addLayout(setup);
    tasks_ = new QTableWidget(0, 4, this);
    tasks_->setObjectName("exportQueueTasks");
    tasks_->setHorizontalHeaderLabels({tr("导出快照"), tr("目标路径"), tr("状态"), tr("详情")});
    tasks_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tasks_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tasks_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(tasks_, 1);
    message_ = new QLabel(this);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    setup_presets(form);
    auto* buttons = new QHBoxLayout;
    const auto button = [&](const QString& text, const char* name,
                            const std::function<void()>& action) {
        auto* result = new QPushButton(text, this);
        result->setObjectName(name);
        result->setAutoDefault(false);
        connect(result, &QPushButton::clicked, this, action);
        buttons->addWidget(result);
    };
    button(tr("加入队列并开始"), "exportQueueStart", [this] { enqueue(); });
    button(tr("取消选中"), "exportQueueCancel", [this] {
        for (const auto& index : tasks_->selectionModel()->selectedRows())
            queue_.cancel(tasks_->item(index.row(), 0)->data(Qt::UserRole).toULongLong());
        refresh();
    });
    button(tr("取消未完成"), "exportQueueCancelAll", [this] {
        queue_.cancel_all();
        refresh();
    });
    button(tr("仅重试失败"), "exportQueueRetry", [this] {
        queue_.retry_failed();
        refresh();
    });
    button(tr("打开输出位置"), "exportQueueLocation", [this] {
        const auto row = tasks_->currentRow();
        const auto path =
            row < 0 ? directory_->text() : QFileInfo(tasks_->item(row, 1)->text()).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    button(tr("返回编辑"), "exportQueueHide", [this] { hide(); });
    layout->addLayout(buttons);
    for (auto* line : {directory_, sizes_, naming_})
        connect(line, &QLineEdit::textChanged, this, [this] { update_example(); });
    connect(format_, &QComboBox::currentIndexChanged, this, [this] { update_example(); });
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start(150);
    update_example();
    const auto previous =
        QJsonDocument::fromJson(QSettings().value("exportQueue/lastSettings").toByteArray())
            .object();
    if (!previous.isEmpty())
        try {
            apply_output_preset(previous);
        } catch (const std::exception& error) {
            message_->setText(QString::fromUtf8(error.what()));
        }
}
void ExportDialog::set_sources(const QList<CanvasView*>& sources) {
    snapshots_.clear();
    sources_.clear();
    files_->clear();
    for (auto* canvas : sources) {
        sources_.append(canvas);
        auto* item = new QListWidgetItem(canvas->project().state().name, files_);
        item->setCheckState(Qt::Checked);
    }
}
void ExportDialog::set_snapshots(std::vector<editing::Snapshot> snapshots) {
    sources_.clear();
    snapshots_=std::move(snapshots);
    files_->clear();
    for(const auto& snapshot:snapshots_){
        auto* item=new QListWidgetItem(snapshot.state.name,files_);
        item->setCheckState(Qt::Checked);
    }
}
void ExportDialog::update_example() {
    try {
        const auto size = parse_sizes(sizes_->text()).front();
        const auto extension = format_->currentIndex() == 2   ? ".jpg"
                               : format_->currentIndex() >= 3 ? ".tif"
                                                              : ".png";
        example_->setText(
            QDir(directory_->text())
                .filePath(output_name(naming_->text(), tr("照片"), size, 1) + extension));
        quality_->setEnabled(format_->currentIndex() == 2);
        alpha_->setEnabled(format_->currentIndex() != 2);
        matte_->setEnabled(format_->currentIndex() == 2 || !alpha_->isChecked());
    } catch (const std::exception& error) {
        example_->setText(QString::fromUtf8(error.what()));
    }
}
void ExportDialog::enqueue() {
    try {
        if (directory_->text().isEmpty() || !QDir(directory_->text()).exists())
            throw editing::EditError(tr("请选择已存在的输出目录"));
        const auto sizes = parse_sizes(sizes_->text());
        std::vector<std::pair<editing::Snapshot, editing::ExportOptions>> prepared;
        QStringList protected_paths;
        std::function<void(const std::vector<editing::Layer>&)> collect;
        collect = [&](const auto& layers) {
            for (const auto& layer : layers) {
                if (!layer.linked_path.isEmpty())
                    protected_paths.append(layer.linked_path);
                collect(layer.children);
            }
        };
        for (const auto& canvas : sources_)
            if (canvas) {
                if (!canvas->filename.isEmpty())
                    protected_paths.append(canvas->filename);
                collect(canvas->project().state().layers);
            }
        int index = 0;
        for(const auto& snapshot:snapshots_)collect(snapshot.state.layers);
        const auto count=snapshots_.empty()?std::size_t(sources_.size()):snapshots_.size();
        for (std::size_t i = 0; i < count; ++i) {
            auto* canvas = snapshots_.empty()?sources_[qsizetype(i)].data():nullptr;
            if ((snapshots_.empty() && !canvas) || files_->item(int(i))->checkState() != Qt::Checked)
                continue;
            if (canvas && canvas->transforming())
                throw editing::EditError(tr("请先确认或取消当前变换，再提交导出快照"));
            const auto snapshot = canvas?canvas->project().snapshot():snapshots_[i];
            ++index;
            for (const auto length : sizes) {
                editing::ExportOptions options;
                options.metadata={metadata_[0]->isChecked(),metadata_[1]->isChecked(),metadata_[2]->isChecked(),metadata_[3]->isChecked(),metadata_[4]->isChecked()};
                for(int m=0;m<5;++m)QSettings().setValue(QString("exportQueue/metadata%1").arg(m),metadata_[m]->isChecked());
                options.format = format_->currentIndex() == 2   ? "jpg"
                                 : format_->currentIndex() >= 3 ? "tif"
                                                                : "png";
                options.bits =
                    format_->currentIndex() == 1 || format_->currentIndex() == 4 ? 16 : 8;
                options.quality = quality_->value();
                const std::array<QColorSpace::NamedColorSpace, 4> spaces{
                    QColorSpace::SRgb, QColorSpace::AdobeRgb, QColorSpace::DisplayP3,
                    QColorSpace::ProPhotoRgb};
                options.space = QColorSpace(spaces[profile_->currentIndex()]);
                options.alpha = alpha_->isChecked();
                options.matte = matte_->property("matte").value<QColor>();
                options.conflict = static_cast<editing::ExportConflict>(conflict_->currentIndex());
                options.protected_paths = protected_paths;
                options.size =
                    length == 0 ? snapshot.state.size
                                : snapshot.state.size.scaled(length, length, Qt::KeepAspectRatio);
                options.size = options.size.expandedTo(QSize(1, 1));
                if (qint64(options.size.width()) * options.size.height() > huelith::editing::maximum_image_pixels)
                    throw editing::EditError(tr("输出超过 5.12 亿像素预算"));
                options.path =
                    QDir(directory_->text())
                        .filePath(output_name(naming_->text(), snapshot.state.name, length, index) +
                                  "." + options.format);
                prepared.emplace_back(snapshot, std::move(options));
            }
        }
        if (prepared.empty())
            throw editing::EditError(tr("请至少选择一张图片"));
        for (auto& [snapshot, options] : prepared)
            queue_.add(std::move(snapshot), std::move(options));
        QSettings().setValue("exportQueue/directory", directory_->text());
        QSettings().setValue("exportQueue/sizes", sizes_->text());
        QSettings().setValue("exportQueue/naming", naming_->text());
        QSettings().setValue("exportQueue/lastSettings",
                             QJsonDocument(output_preset()).toJson(QJsonDocument::Compact));
        queue_.start();
        message_->setText(tr("已提交 %1 个导出快照；返回编辑后任务继续，后续修改不影响本次输出。")
                              .arg(prepared.size()));
        refresh();
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
}
QJsonObject ExportDialog::output_preset() const {
    return {{"version", 1},
            {"metadata",QJsonArray{metadata_[0]->isChecked(),metadata_[1]->isChecked(),metadata_[2]->isChecked(),metadata_[3]->isChecked(),metadata_[4]->isChecked()}},
            {"sizes", sizes_->text()},
            {"naming", naming_->text()},
            {"format", format_->currentIndex()},
            {"profile", profile_->currentIndex()},
            {"quality", quality_->value()},
            {"conflict", conflict_->currentIndex()},
            {"alpha", alpha_->isChecked()},
            {"matte", matte_->property("matte").value<QColor>().name()}};
}
void ExportDialog::apply_output_preset(const QJsonObject& preset) {
    parse_sizes(preset.value("sizes").toString());
    output_name(preset.value("naming").toString(), "sample", 2048, 1);
    const QColor color(preset.value("matte").toString());
    if (preset.value("version").toInt() != 1 || !color.isValid() || color.alpha() != 255 ||
        !preset.value("alpha").isBool())
        throw editing::EditError(tr("导出预设无效"));
    for (const auto& bound : {std::pair{"format", 4}, std::pair{"profile", 3},
                              std::pair{"conflict", 2}, std::pair{"quality", 100}}) {
        const auto value = preset.value(bound.first);
        const int number = value.toInt(-1);
        if (!value.isDouble() || number < 0 || number > bound.second ||
            value.toDouble() != number || (QString(bound.first) == "quality" && number == 0))
            throw editing::EditError(tr("导出预设参数无效"));
    }
    sizes_->setText(preset.value("sizes").toString());
    const auto metadata=preset.value("metadata").toArray();
    if(!metadata.isEmpty()&&metadata.size()!=5)throw editing::EditError(tr("元数据预设无效"));
    for(int i=0;i<5;++i)metadata_[i]->setChecked(metadata.isEmpty()?i!=4:metadata[i].toBool());
    naming_->setText(preset.value("naming").toString());
    format_->setCurrentIndex(preset.value("format").toInt());
    profile_->setCurrentIndex(preset.value("profile").toInt());
    quality_->setValue(preset.value("quality").toInt());
    conflict_->setCurrentIndex(preset.value("conflict").toInt());
    alpha_->setChecked(preset.value("alpha").toBool());
    matte_->setProperty("matte", color);
    matte_->setText(tr("背景填充色：%1").arg(color.name()));
    update_example();
}
void ExportDialog::setup_presets(QFormLayout* form) {
    auto* list = new QComboBox(this);
    list->setObjectName("exportQueuePreset");
    auto* name = new QLineEdit(this);
    name->setObjectName("exportQueuePresetName");
    name->setMaxLength(120);
    name->setPlaceholderText(tr("输入预设名称"));
    auto* row = new QHBoxLayout;
    auto* save = new QPushButton(tr("保存/更新"), this);
    save->setObjectName("exportQueuePresetSave");
    auto* remove = new QPushButton(tr("删除预设"), this);
    remove->setObjectName("exportQueuePresetDelete");
    row->addWidget(save);
    row->addWidget(remove);
    form->insertRow(0, tr("导出预设"), list);
    form->insertRow(1, name);
    form->insertRow(2, row);
    const auto refresh = [this, list] {
        const QSignalBlocker blocker(list);
        list->clear();
        list->addItem(tr("自定设置"));
        auto base = output_preset();
        base.insert("naming", "{name}_{size}_{index}");
        base.insert("conflict", 0);
        base.insert("quality", 95);
        base.insert("matte", "#ffffff");
        const auto add = [&](QString title, int format, int profile, QString sizes, bool alpha) {
            auto item = base;
            item.insert("format", format);
            item.insert("profile", profile);
            item.insert("sizes", sizes);
            item.insert("alpha", alpha);
            list->addItem(title, QVariant::fromValue(item));
            list->setItemData(list->count() - 1, true, Qt::UserRole + 1);
        };
        add(tr("网络 JPEG · 2048 · sRGB"), 2, 0, "2048", false);
        add(tr("透明 PNG · 原图 · sRGB"), 0, 0, "原图", true);
        add(tr("高精度 TIFF · 原图 · ProPhoto RGB"), 4, 3, "原图", true);
        for (const auto& value :
             QJsonDocument::fromJson(QSettings().value("exportQueue/presets").toByteArray())
                 .array()) {
            const auto preset = value.toObject();
            list->addItem(preset.value("name").toString(), QVariant::fromValue(preset));
        }
    };
    refresh();
    connect(list, &QComboBox::currentIndexChanged, this, [this, list, name, remove](int index) {
        remove->setEnabled(index > 0 && !list->itemData(index, Qt::UserRole + 1).toBool());
        if (index <= 0)
            return;
        try {
            apply_output_preset(list->itemData(index).value<QJsonObject>());
            name->setText(list->currentText());
            message_->setText(tr("已应用导出预设"));
        } catch (const std::exception& error) {
            message_->setText(QString::fromUtf8(error.what()));
        }
    });
    remove->setEnabled(false);
    connect(save, &QPushButton::clicked, this, [this, list, name, refresh] {
        try {
            const auto title = name->text().trimmed();
            if (title.isEmpty())
                throw editing::EditError(tr("请输入预设名称"));
            auto preset = output_preset();
            apply_output_preset(preset);
            preset.insert("name", title);
            auto items =
                QJsonDocument::fromJson(QSettings().value("exportQueue/presets").toByteArray())
                    .array();
            int index = -1;
            for (int i = 0; i < items.size(); ++i)
                if (items[i].toObject().value("name").toString().compare(
                        title, Qt::CaseInsensitive) == 0) {
                    index = i;
                    break;
                }
            if (index >= 0)
                items[index] = preset;
            else {
                if (items.size() >= 64)
                    throw editing::EditError(tr("最多保存64个导出预设"));
                items.append(preset);
            }
            QSettings settings;
            settings.setValue("exportQueue/presets",
                              QJsonDocument(items).toJson(QJsonDocument::Compact));
            settings.sync();
            if (settings.status() != QSettings::NoError)
                throw editing::EditError(tr("无法写入导出预设"));
            refresh();
            list->setCurrentIndex(list->count() - items.size() +
                                  (index >= 0 ? index : items.size() - 1));
            message_->setText(tr("已保存导出预设"));
        } catch (const std::exception& error) {
            message_->setText(QString::fromUtf8(error.what()));
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, list, refresh, name] {
        const int index = list->currentIndex();
        if (index <= 0 || list->itemData(index, Qt::UserRole + 1).toBool())
            return;
        const auto title = list->currentText();
        auto items =
            QJsonDocument::fromJson(QSettings().value("exportQueue/presets").toByteArray()).array();
        for (int i = int(items.size()) - 1; i >= 0; --i)
            if (items[i].toObject().value("name").toString() == title)
                items.removeAt(i);
        QSettings settings;
        settings.setValue("exportQueue/presets",
                          QJsonDocument(items).toJson(QJsonDocument::Compact));
        settings.sync();
        refresh();
        name->clear();
        message_->setText(settings.status() == QSettings::NoError ? tr("已删除导出预设")
                                                                  : tr("无法写入导出预设"));
    });
    connect(alpha_, &QCheckBox::toggled, this, [this] { update_example(); });
}
void ExportDialog::refresh() {
    const auto items = queue_.items();
    const QStringList names{tr("排队"), tr("处理中"), tr("写入中"), tr("完成"),
                            tr("失败"), tr("已取消"), tr("已跳过")};
    tasks_->setRowCount(int(items.size()));
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const QStringList columns{item.document + tr(" · 修订 %1").arg(item.revision), item.path,
                                  names[int(item.status)], item.error};
        for (int column = 0; column < 4; ++column) {
            auto* cell = tasks_->item(int(i), column);
            if (!cell) {
                cell = new QTableWidgetItem;
                tasks_->setItem(int(i), column, cell);
            }
            cell->setText(columns[column]);
            cell->setData(Qt::UserRole, item.id);
        }
    }
}
} // namespace huelith::desktop
