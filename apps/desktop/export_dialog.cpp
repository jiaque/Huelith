#include "export_dialog.h"
#include "canvas_view.h"
#include "window_header.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
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
    profile_->addItems({"sRGB", "Adobe RGB", "Display P3"});
    form->addRow(tr("输出色彩空间 / 嵌入 ICC"), profile_);
    quality_ = new QSpinBox(this);
    quality_->setRange(1, 100);
    quality_->setValue(95);
    form->addRow(tr("JPEG 质量（透明处填白）"), quality_);
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
}
void ExportDialog::set_sources(const QList<CanvasView*>& sources) {
    sources_.clear();
    files_->clear();
    for (auto* canvas : sources) {
        sources_.append(canvas);
        auto* item = new QListWidgetItem(canvas->project().state().name, files_);
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
        for (qsizetype i = 0; i < sources_.size(); ++i) {
            auto* canvas = sources_[i].data();
            if (!canvas || files_->item(int(i))->checkState() != Qt::Checked)
                continue;
            if (canvas->transforming())
                throw editing::EditError(tr("请先确认或取消当前变换，再提交导出快照"));
            const auto snapshot = canvas->project().snapshot();
            ++index;
            for (const auto length : sizes) {
                editing::ExportOptions options;
                options.format = format_->currentIndex() == 2   ? "jpg"
                                 : format_->currentIndex() >= 3 ? "tif"
                                                                : "png";
                options.bits =
                    format_->currentIndex() == 1 || format_->currentIndex() == 4 ? 16 : 8;
                options.quality = quality_->value();
                options.space = QColorSpace(profile_->currentIndex() == 1   ? QColorSpace::AdobeRgb
                                            : profile_->currentIndex() == 2 ? QColorSpace::DisplayP3
                                                                            : QColorSpace::SRgb);
                options.conflict = static_cast<editing::ExportConflict>(conflict_->currentIndex());
                options.protected_paths = protected_paths;
                options.size =
                    length == 0 ? snapshot.state.size
                                : snapshot.state.size.scaled(length, length, Qt::KeepAspectRatio);
                options.size = options.size.expandedTo(QSize(1, 1));
                if (qint64(options.size.width()) * options.size.height() > 64'000'000)
                    throw editing::EditError(tr("输出超过 6400 万像素预算"));
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
        queue_.start();
        message_->setText(tr("已提交 %1 个导出快照；返回编辑后任务继续，后续修改不影响本次输出。")
                              .arg(prepared.size()));
        refresh();
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
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
