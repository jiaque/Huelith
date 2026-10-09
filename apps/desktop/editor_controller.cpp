#include "editor_controller.h"
#include "canvas_view.h"
#include "main_window.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <future>

namespace huelith::desktop {
using namespace editing;
struct EditorController::SaveJob {
    QPointer<CanvasView> canvas;
    QString path;
    quint64 revision;
    std::future<void> result;
};
EditorController::~EditorController() = default;

namespace {
Layer& editable(CanvasView* canvas, State& state) {
    auto* layer = find_layer(state.layers, canvas->active_layer);
    if (!layer)
        throw EditError(QStringLiteral("请先选择图层"));
    if (layer_locked(state.layers, canvas->active_layer))
        throw EditError(QStringLiteral("图层已锁定，请先解除锁定"));
    return *layer;
}
QString kind_name(LayerKind kind) {
    const QStringList names{QStringLiteral("像素"),     QStringLiteral("源图片"),
                            QStringLiteral("绘画修复"), QStringLiteral("调整"),
                            QStringLiteral("填充"),     QStringLiteral("组"),
                            QStringLiteral("嵌套合成")};
    return names.at(int(kind));
}
void add_rows(QTreeWidget* tree, QTreeWidgetItem* parent, const std::vector<Layer>& layers,
              const QUuid& selected) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        const auto& layer = *it;
        auto* row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        row->setText(0, layer.name);
        row->setText(1, kind_name(layer.kind));
        row->setText(2, layer.mask.isNull() ? QStringLiteral("—") : QStringLiteral("蒙版"));
        row->setData(0, Qt::UserRole, layer.id);
        row->setCheckState(0, layer.visible ? Qt::Checked : Qt::Unchecked);
        row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled |
                      Qt::ItemIsDropEnabled);
        row->setToolTip(0, layer.linked_path.isEmpty()
                               ? kind_name(layer.kind)
                               : layer.linked_path +
                                     (QFileInfo::exists(layer.linked_path)
                                          ? QString{}
                                          : QStringLiteral("（缺失，使用工程内缓存）")));
        row->setSizeHint(0, QSize(0, 34));
        if (!layer.pixels.isNull())
            row->setIcon(0, QPixmap::fromImage(
                                display_image(layer.pixels.scaled(32, 32, Qt::KeepAspectRatio))));
        if (layer.id == selected) {
            row->setSelected(true);
            tree->setCurrentItem(row, 0, QItemSelectionModel::NoUpdate);
        }
        add_rows(tree, row, layer.children, selected);
        row->setExpanded(true);
    }
}
} // namespace
EditorController::EditorController(MainWindow* window) : QObject(window), window_(window) {
    auto* dock = window_->findChild<QDockWidget*>("layersDock");
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* tabs = new QTabWidget(panel);
    layers_ = new QTreeWidget(tabs);
    layers_->setObjectName("layerTree");
    layers_->setHeaderLabels({tr("图层"), tr("类型"), tr("目标")});
    layers_->setColumnWidth(0, 155);
    layers_->setColumnWidth(1, 70);
    layers_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layers_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(layers_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int column) {
                if (column == 0)
                    layers_->editItem(item, 0);
            });
    layers_->setDragDropMode(QAbstractItemView::InternalMove);
    layers_->setAutoExpandDelay(500);
    layers_->setDropIndicatorShown(true);
    layers_->viewport()->installEventFilter(this);
    layers_->setDefaultDropAction(Qt::MoveAction);
    tabs->addTab(layers_, tr("图层"));
    paths_ = new QListWidget(tabs);
    paths_->setObjectName("pathList");
    tabs->addTab(paths_, tr("路径"));
    history_ = new QListWidget(tabs);
    history_->setObjectName("historyList");
    tabs->addTab(history_, tr("历史"));
    layout->addWidget(tabs);
    auto* buttons = new QToolBar(panel);
    for (const auto& operation :
         {QStringLiteral("新建"), QStringLiteral("组"), QStringLiteral("蒙版"),
          QStringLiteral("调整"), QStringLiteral("删除")}) {
        auto* action = buttons->addAction(operation);
        connect(action, &QAction::triggered, this,
                [this, operation] { guarded([&] { layer_action(operation); }); });
    }
    layout->addWidget(buttons);
    dock->setWidget(panel);
    connect(layers_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (auto* c = canvas()) {
            c->commit_transform();
            c->active_layer = item->data(0, Qt::UserRole).toUuid();
            c->mask_target = column == 2;
            c->mask_view =
                column == 2 && QApplication::keyboardModifiers().testFlag(Qt::AltModifier);
            if (column == 2 && QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
                guarded([&] {
                    c->edit(tr("蒙版启停"), [&](State& s) {
                        auto& l = editable(c, s);
                        l.mask_enabled = !l.mask_enabled;
                    });
                });
            c->update();
            QTimer::singleShot(0, this, [this] { refresh(); });
        }
    });
    connect(layers_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (refreshing_ || column != 0 || !canvas())
            return;
        const auto id = item->data(0, Qt::UserRole).toUuid();
        const auto name = item->text(0);
        const bool visible = item->checkState(0) == Qt::Checked;
        guarded([&] {
            canvas()->edit(tr("图层名称 / 显示"), [&](State& s) {
                auto* l = find_layer(s.layers, id);
                if (!l)
                    return;
                if (layer_locked(s.layers, id))
                    throw EditError(tr("图层已锁定"));
                l->name = name;
                l->visible = visible;
            });
        });
    });
    connect(paths_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (auto* c = canvas()) {
            c->path = c->project().state().paths.value(item->text());
            c->update();
        }
    });
    build_properties();
    configure_menus();
    window_->setAcceptDrops(true);
    window_->installEventFilter(this);
    auto* timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, &QTimer::timeout, this, [this] { autosave(); });
    timer->start();
    auto* save_timer = new QTimer(this);
    save_timer->setInterval(50);
    connect(save_timer, &QTimer::timeout, this, [this] { guarded([&] { finish_save(false); }); });
    save_timer->start();
}
CanvasView* EditorController::canvas() const {
    return window_->active_canvas();
}
void EditorController::guarded(const std::function<void()>& action) {
    try {
        action();
    } catch (const std::exception& e) {
        refresh();
        if (canvas())
            canvas()->preview(canvas()->project().state());
        QMessageBox::warning(window_, tr("操作未完成"), QString::fromUtf8(e.what()));
    }
}
void EditorController::command(const QString&, const std::function<void()>& action) {
    if (canvas())
        guarded(action);
}
void EditorController::bind(CanvasView* c) {
    c->tool_changed = [this] { refresh_tools(); };
    c->changed = [this] {
        QTimer::singleShot(0, this, [this] {
            window_->refresh_document_state();
            refresh();
        });
    };
    c->recovery_file = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                       "/recovery/" + QUuid::createUuid().toString(QUuid::Id128) + ".huelith";
}
void EditorController::refresh() {
    if (refreshing_)
        return;
    refreshing_ = true;
    QSet<QUuid> selected;
    for (const auto* item : layers_->selectedItems())
        selected.insert(item->data(0, Qt::UserRole).toUuid());
    layers_->clear();
    paths_->clear();
    history_->clear();
    auto* c = canvas();
    if (c) {
        const auto& s = c->project().state();
        if (!find_layer(s.layers, c->active_layer)) {
            c->active_layer = s.layers.empty() ? QUuid{} : s.layers.back().id;
            c->mask_target = false;
        }
        add_rows(layers_, nullptr, s.layers, c->active_layer);
        for (QTreeWidgetItemIterator it(layers_); *it; ++it)
            if (selected.contains((*it)->data(0, Qt::UserRole).toUuid()))
                (*it)->setSelected(true);
        paths_->addItems(s.paths.keys());
        history_->addItems(c->project().history_names());
        const auto* layer = find_layer(s.layers, c->active_layer);
        opacity_->setEnabled(layer != nullptr);
        exposure_->setEnabled(layer && layer->kind != LayerKind::group);
        if (layer) {
            blend_->setCurrentIndex(int(layer->blend));
            opacity_->setValue(layer->opacity * 100);
            exposure_->setValue(layer->exposure);
            mask_->setChecked(layer->mask_enabled);
            linked_->setChecked(layer->mask_linked);
            isolated_->setChecked(layer->isolated);
            locked_->setChecked(layer->locked);
            clipping_->setChecked(layer->clipped);
            mask_->setEnabled(!layer->mask.isNull());
            linked_->setEnabled(!layer->mask.isNull());
            isolated_->setVisible(layer->kind == LayerKind::group ||
                                  layer->kind == LayerKind::composition);
        }
        const int index = window_->documents_->indexOf(c);
        if (index >= 0)
            window_->documents_->setTabText(index, s.name + (c->project().dirty() ? " *" : ""));
    }
    refreshing_ = false;
    refresh_tools();
}
void EditorController::build_properties() {
    auto* dock = window_->findChild<QDockWidget*>("propertiesDock");
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->addWidget(window_->properties_);
    auto* form = new QFormLayout();
    opacity_ = new QDoubleSpinBox(panel);
    opacity_->setRange(0, 100);
    opacity_->setSuffix(" %");
    opacity_->setObjectName("layerOpacity");
    form->addRow(tr("不透明度"), opacity_);
    exposure_ = new QDoubleSpinBox(panel);
    exposure_->setRange(-16, 16);
    exposure_->setSingleStep(.1);
    exposure_->setObjectName("layerExposure");
    form->addRow(tr("曝光（基础调整）"), exposure_);
    auto apply = [this](bool exposure) {
        if (refreshing_ || !canvas())
            return;
        guarded([&] {
            auto* c = canvas();
            const double value = exposure ? exposure_->value() : opacity_->value() / 100;
            c->edit(exposure ? tr("调整曝光") : tr("调整不透明度"), [&](State& s) {
                auto& l = editable(c, s);
                if (exposure)
                    l.exposure = value;
                else
                    l.opacity = static_cast<float>(value);
            });
        });
    };
    connect(opacity_, &QDoubleSpinBox::editingFinished, this, [apply] { apply(false); });
    connect(exposure_, &QDoubleSpinBox::editingFinished, this, [apply] { apply(true); });
    for (auto pair : {std::pair{&mask_, tr("启用蒙版")}, std::pair{&linked_, tr("链接蒙版")},
                      std::pair{&isolated_, tr("隔离组")}, std::pair{&locked_, tr("锁定图层")},
                      std::pair{&clipping_, tr("剪贴到下方基底")}}) {
        *pair.first = new QCheckBox(pair.second, panel);
        form->addRow(*pair.first);
    }
    auto flag = [this](QCheckBox* box, int key) {
        connect(box, &QCheckBox::toggled, this, [this, key](bool value) {
            if (refreshing_ || !canvas())
                return;
            guarded([&] {
                auto* c = canvas();
                c->edit(tr("图层属性"), [&](State& s) {
                    auto* l = find_layer(s.layers, c->active_layer);
                    if (!l)
                        return;
                    if (l->locked && key != 3)
                        throw EditError(tr("图层已锁定"));
                    if (key == 0)
                        l->mask_enabled = value;
                    if (key == 1)
                        l->mask_linked = value;
                    if (key == 2)
                        l->isolated = value;
                    if (key == 3)
                        l->locked = value;
                    if (key == 4)
                        l->clipped = value;
                });
            });
        });
    };
    flag(mask_, 0);
    flag(linked_, 1);
    flag(isolated_, 2);
    flag(locked_, 3);
    flag(clipping_, 4);
    blend_ = new QComboBox(panel);
    blend_->addItems({tr("正常"),     tr("变暗"),   tr("正片叠底"), tr("颜色加深"), tr("线性加深"),
                      tr("变亮"),     tr("滤色"),   tr("颜色减淡"), tr("线性减淡"), tr("叠加"),
                      tr("柔光"),     tr("强光"),   tr("亮光"),     tr("线性光"),   tr("点光"),
                      tr("实色混合"), tr("差值"),   tr("排除"),     tr("减去"),     tr("划分"),
                      tr("色相"),     tr("饱和度"), tr("颜色"),     tr("明度")});
    form->insertRow(0, tr("混合模式"), blend_);
    connect(blend_, &QComboBox::currentIndexChanged, this, [this](int value) {
        if (refreshing_ || !canvas())
            return;
        guarded([&] {
            auto* c = canvas();
            c->edit(tr("混合模式"),
                    [&](State& s) { editable(c, s).blend = static_cast<Blend>(value); });
        });
    });
    auto* slider = new QSlider(Qt::Horizontal, panel);
    slider->setRange(0, 100);
    slider->setValue(100);
    slider->setObjectName("opacitySlider");
    form->addRow(tr("不透明度拖动"), slider);
    connect(opacity_, &QDoubleSpinBox::valueChanged, slider, [slider](double value) {
        QSignalBlocker blocked(slider);
        slider->setValue(qRound(value));
    });
    connect(slider, &QSlider::sliderMoved, this, [this](int value) {
        if (auto* c = canvas()) {
            const auto* layer = find_layer(c->project().state().layers, c->active_layer);
            if (!layer || layer->locked)
                return;
            State draft = c->project().state();
            find_layer(draft.layers, c->active_layer)->opacity = float(value) / 100.F;
            c->preview(draft);
            QSignalBlocker blocked(opacity_);
            opacity_->setValue(value);
        }
    });
    connect(slider, &QSlider::sliderReleased, this, [this, slider] {
        if (!canvas())
            return;
        guarded([&] {
            auto* c = canvas();
            c->edit(tr("拖动不透明度"),
                    [&](State& s) { editable(c, s).opacity = float(slider->value()) / 100.F; });
        });
    });
    layout->addLayout(form);
    layout->addStretch();
    dock->setWidget(panel);
}
void EditorController::update_recent(const QString& path) {
    QSettings settings;
    auto list = settings.value("recentProjects").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 12)
        list.removeLast();
    settings.setValue("recentProjects", list);
    recent_->clear();
    for (const auto& item : list) {
        auto* action = recent_->addAction(item);
        connect(action, &QAction::triggered, this, [this, item] { guarded([&] { open(item); }); });
    }
}
bool EditorController::open(const QString& path, bool place) {
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().compare("huelith", Qt::CaseInsensitive) == 0) {
        const auto state = load_project(path);
        if (place && canvas()) {
            Layer nested;
            nested.name = state.name;
            nested.kind = LayerKind::composition;
            nested.children = state.layers;
            for (auto& l : nested.children)
                l = duplicate_layer(l);
            canvas()->edit(tr("置入嵌套合成"), [&](State& s) { s.layers.push_back(nested); });
            canvas()->active_layer = nested.id;
        } else {
            CanvasSpec spec;
            spec.name = state.name.toStdString();
            spec.width = state.size.width();
            spec.height = state.size.height();
            spec.resolution_ppi = state.ppi;
            if (!window_->create_document(spec))
                return false;
            canvas()->load_state(state, true);
            canvas()->filename = path;
        }
    } else {
        Layer layer;
        layer.name = QFileInfo(path).completeBaseName();
        layer.kind = LayerKind::source;
        layer.pixels = import_image(path);
        layer.linked_path = QFileInfo(path).absoluteFilePath();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            layer.linked_hash =
                QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
        if (place && canvas()) {
            layer.transform.translate((canvas()->spec().width - layer.pixels.width()) / 2.,
                                      (canvas()->spec().height - layer.pixels.height()) / 2.);
            canvas()->edit(tr("置入图片"), [&](State& s) { s.layers.push_back(layer); });
            canvas()->active_layer = layer.id;
        } else {
            CanvasSpec spec;
            spec.name = layer.name.toStdString();
            spec.width = layer.pixels.width();
            spec.height = layer.pixels.height();
            if (!window_->create_document(spec))
                return false;
            canvas()->edit(tr("打开图片"), [&](State& s) {
                s.layers = {layer};
                s.snapshots.insert(tr("导入状态"), render(s));
            });
            canvas()->active_layer = layer.id;
        }
    }
    layers_->clearSelection();
    if (canvas())
        canvas()->prepare_inserted_layer();
    update_recent(path);
    refresh();
    return true;
}
bool EditorController::save(CanvasView* c, bool save_as, bool asynchronous) {
    if (save_job_) {
        if (asynchronous)
            throw EditError(tr("已有工程正在保存，请稍候"));
        finish_save(true);
    }
    if (!c)
        return false;
    c->commit_transform();
    QString path = c->filename;
    if (save_as || path.isEmpty())
        path = QFileDialog::getSaveFileName(window_, tr("保存 Huelith 工程"),
                                            path.isEmpty() ? c->project().state().name + ".huelith"
                                                           : path,
                                            tr("Huelith 工程 (*.huelith)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".huelith", Qt::CaseInsensitive))
        path += ".huelith";
    const auto snapshot = c->project().snapshot();
    if (asynchronous) {
        save_job_ = std::make_unique<SaveJob>(SaveJob{
            c, path, snapshot.revision,
            std::async(std::launch::async, [snapshot, path] { save_project(snapshot, path); })});
        window_->statusBar()->showMessage(tr("正在保存工程快照… 可继续编辑"));
    } else {
        save_project(snapshot, path);
        c->filename = path;
        c->project().mark_saved(snapshot.revision);
        update_recent(path);
        refresh();
    }
    return true;
}
void EditorController::finish_save(bool wait) {
    if (!save_job_)
        return;
    if (!wait && save_job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    auto job = std::move(save_job_);
    job->result.get();
    if (job->canvas) {
        job->canvas->filename = job->path;
        job->canvas->project().mark_saved(job->revision);
    }
    update_recent(job->path);
    refresh();
    window_->statusBar()->showMessage(tr("工程快照已保存"), 3000);
}
bool EditorController::confirm_close(CanvasView* c) {
    if (save_job_) {
        bool finished = true;
        guarded([&] {
            try {
                finish_save(true);
            } catch (...) {
                finished = false;
                throw;
            }
        });
        if (!finished)
            return false;
    }

    if (!c || !c->project().dirty())
        return true;
    QMessageBox box(QMessageBox::Question, tr("保存修改"),
                    tr("是否保存“%1”的修改？").arg(c->project().state().name),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, window_);
    box.setDefaultButton(QMessageBox::Save);
    const int answer = box.exec();
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        bool success = false;
        guarded([&] { success = save(c, false, false); });
        return success;
    }
    return answer == QMessageBox::Discard;
}
void EditorController::autosave() {
    for (int i = 0; i < window_->documents_->count(); ++i) {
        auto* c = dynamic_cast<CanvasView*>(window_->documents_->widget(i));
        if (!c || !c->project().dirty())
            continue;
        try {
            QDir().mkpath(QFileInfo(c->recovery_file).absolutePath());
            save_project(c->project().snapshot(), c->recovery_file);
        } catch (const std::exception& e) {
            window_->statusBar()->showMessage(
                tr("自动恢复写入失败：%1").arg(QString::fromUtf8(e.what())));
        }
    }
}
void EditorController::create_layer(int kind) {
    auto* c = canvas();
    if (!c)
        return;
    Layer layer;
    layer.kind = static_cast<LayerKind>(kind);
    layer.name = kind_name(layer.kind);
    if (layer.kind == LayerKind::fill) {
        layer.fill = QColorDialog::getColor(Qt::white, window_);
        if (!layer.fill.isValid())
            return;
    }
    c->edit(tr("新建图层"), [&](State& s) { s.layers.push_back(layer); });
    layers_->clearSelection();
    c->active_layer = layer.id;
    refresh();
}
void EditorController::layer_action(const QString& operation) {
    auto* c = canvas();
    if (!c)
        return;
    if (operation == tr("新建")) {
        create_layer(int(LayerKind::pixels));
        return;
    }
    if (operation == tr("组")) {
        create_layer(int(LayerKind::group));
        return;
    }
    if (operation == tr("调整")) {
        create_layer(int(LayerKind::adjustment));
        return;
    }
    const auto selected = layers_->selectedItems();
    std::vector<QUuid> ids;
    for (const auto* row : selected)
        ids.push_back(row->data(0, Qt::UserRole).toUuid());
    c->edit(operation, [&](State& s) {
        if (operation == tr("删除")) {
            for (const auto& id : ids)
                if (find_layer(s.layers, id))
                    remove_layer(s, id);
            return;
        }
        auto& layer = editable(c, s);
        if (operation == tr("复制图层")) {
            auto copy = duplicate_layer(layer);
            copy.name += tr(" 副本");
            s.layers.push_back(copy);
            layers_->clearSelection();
            c->active_layer = copy.id;
        } else if (operation == tr("蒙版")) {
            if (s.selection.isNull()) {
                layer.mask = QImage(s.size, QImage::Format_Grayscale8);
                layer.mask.fill(QApplication::keyboardModifiers().testFlag(Qt::AltModifier) ? 0
                                                                                            : 255);
            } else
                layer.mask = s.selection;
            c->mask_target = true;
        } else if (operation == tr("反相蒙版")) {
            if (!layer.mask.isNull())
                layer.mask.invertPixels();
        } else if (operation == tr("蒙版转选区")) {
            if (!layer.mask.isNull()) {
                QImage range(s.size, QImage::Format_RGB32);
                range.fill(Qt::black);
                QPainter painter(&range);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.setTransform(layer.mask_transform);
                painter.drawImage(0, 0, layer.mask);
                painter.end();
                s.selection = range.convertToFormat(QImage::Format_Grayscale8);
            }
        } else if (operation == tr("上移"))
            shift_layer(s, layer.id, 1);
        else if (operation == tr("下移"))
            shift_layer(s, layer.id, -1);
        else if (operation == tr("嵌入资源")) {
            layer.linked_path.clear();
            layer.linked_hash.clear();
        } else if (operation == tr("路径转矢量蒙版")) {
            layer.vector_mask = c->path;
            layer.vector_enabled = true;
        } else if (operation == tr("盖印可见副本")) {
            Layer stamped;
            stamped.name = tr("盖印");
            stamped.pixels = render(s);
            s.layers.push_back(stamped);
            c->active_layer = stamped.id;
        } else if (operation == tr("合并可见图层")) {
            Layer merged;
            merged.name = tr("合并");
            merged.pixels = render(s);
            std::erase_if(s.layers, [](const Layer& l) { return l.visible; });
            s.layers.push_back(merged);
            c->active_layer = merged.id;
        }
    });
    refresh();
}
void EditorController::selection_action(const QString& operation) {
    auto* c = canvas();
    if (!c)
        return;
    if (operation == tr("保存路径")) {
        bool ok{};
        const auto name = QInputDialog::getText(window_, operation, tr("名称"), QLineEdit::Normal,
                                                tr("路径"), &ok);
        if (ok && !name.trimmed().isEmpty())
            c->edit(operation, [&](State& s) { s.paths.insert(name, c->path); });
        return;
    }
    if (operation == tr("保存选区")) {
        bool ok{};
        const auto name = QInputDialog::getText(window_, operation, tr("名称"), QLineEdit::Normal,
                                                tr("选区"), &ok);
        if (ok && !name.trimmed().isEmpty())
            c->edit(operation, [&](State& s) {
                if (s.selection.isNull())
                    throw EditError(tr("当前没有选区"));
                s.saved_selections.insert(name, s.selection);
            });
        return;
    }
    if (operation == tr("载入选区")) {
        bool ok{};
        const auto name =
            QInputDialog::getItem(window_, operation, tr("选区"),
                                  c->project().state().saved_selections.keys(), 0, false, &ok);
        if (ok)
            c->edit(operation, [&](State& s) { s.selection = s.saved_selections.value(name); });
        return;
    }
    int radius = 3;
    if (operation == tr("羽化") || operation == tr("扩张") || operation == tr("收缩") ||
        operation == tr("平滑")) {
        bool ok{};
        radius = QInputDialog::getInt(window_, operation, tr("半径（像素）"), 3, 1, 64, 1, &ok);
        if (!ok)
            return;
    }
    c->edit(operation, [&](State& s) {
        if (operation == tr("取消选区"))
            s.selection = {};
        else if (operation == tr("全选")) {
            s.selection = QImage(s.size, QImage::Format_Grayscale8);
            s.selection.fill(255);
        } else if (operation == tr("反选")) {
            if (s.selection.isNull()) {
                s.selection = QImage(s.size, QImage::Format_Grayscale8);
                s.selection.fill(255);
            } else
                s.selection.invertPixels();
        } else if (operation == tr("路径转选区"))
            combine_range(s, path_range(s.size, c->path), c->range_operation);
        else if (operation == tr("亮度范围") || operation == tr("线性渐变范围") ||
                 operation == tr("径向渐变范围")) {
            const auto image = display_image(render(s));
            QImage range(s.size, QImage::Format_Grayscale8);
            for (int y = 0; y < range.height(); ++y)
                for (int x = 0; x < range.width(); ++x) {
                    double value{};
                    if (operation == tr("亮度范围"))
                        value = qGray(image.pixel(x, y)) / 255.;
                    else if (operation == tr("线性渐变范围"))
                        value = double(x) / std::max(1, range.width() - 1);
                    else
                        value = std::max(
                            0., 1. - std::hypot((x - range.width() / 2.) / (range.width() / 2.),
                                                (y - range.height() / 2.) / (range.height() / 2.)));
                    range.scanLine(y)[x] = static_cast<uchar>(value * 255);
                }
            combine_range(s, range, c->range_operation);
        } else
            modify_range(s, radius, operation == tr("扩张") ? 1 : operation == tr("收缩") ? -1 : 0);
    });
}
void EditorController::configure_menus() {
    auto add = [this](QMenu* menu, const QString& title, const QString& shortcut,
                      const std::function<void()>& callback) {
        auto* action = menu->addAction(title);
        if (!shortcut.isEmpty())
            action->setShortcut(QKeySequence(shortcut));
        connect(action, &QAction::triggered, this, [this, callback] { guarded(callback); });
        return action;
    };
    auto* file = window_->menus_->actions().front()->menu();
    add(file, tr("打开…"), "Ctrl+O", [this] {
        const auto paths = QFileDialog::getOpenFileNames(
            window_, tr("打开图片或工程"), {}, tr("图片 / 工程 (*.png *.jpg *.jpeg *.huelith)"));
        for (const auto& p : paths)
            open(p);
    });
    add(file, tr("置入…"), {}, [this] {
        open(QFileDialog::getOpenFileName(window_, tr("置入当前图片"), {},
                                          tr("图片 / 工程 (*.png *.jpg *.jpeg *.huelith)")),
             true);
    });
    add(file, tr("保存工程"), "Ctrl+S", [this] { save(canvas()); });
    add(file, tr("另存工程…"), "Ctrl+Shift+S", [this] { save(canvas(), true); });
    add(file, tr("导出 PNG / JPEG…"), "Ctrl+Alt+Shift+S", [this] {
        if (auto* c = canvas()) {
            const auto path = QFileDialog::getSaveFileName(
                window_, tr("导出图片（不改变工程保存状态）"), c->project().state().name + ".png",
                tr("PNG (*.png);;JPEG (*.jpg)"));
            if (path.isEmpty())
                return;
            QColor matte = Qt::white;
            if (!path.endsWith(".png", Qt::CaseInsensitive)) {
                matte = QColorDialog::getColor(Qt::white, window_, tr("JPEG 透明区域填充色"));
                if (!matte.isValid())
                    return;
            }
            export_image(c->project().state(), path, matte);
        }
    });
    recent_ = file->addMenu(tr("最近打开"));
    const auto recent = QSettings().value("recentProjects").toStringList();
    for (const auto& item : recent)
        add(recent_, item, {}, [this, item] { open(item); });
    add(file, tr("打开自动恢复…"), {}, [this] {
        const auto path = QFileDialog::getOpenFileName(
            window_, tr("选择恢复副本"),
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery",
            tr("Huelith 工程 (*.huelith)"));
        if (open(path)) {
            canvas()->filename.clear();
            canvas()->project().mark_saved(0);
            refresh();
        }
    });
    auto* edit = window_->menus_->addMenu(tr("编辑(&E)"));
    add(edit, tr("撤销"), "Ctrl+Z", [this] {
        if (auto* c = canvas()) {
            c->cancel_transform();
            c->project().undo();
            c->refresh();
        }
    });
    add(edit, tr("重做"), "Ctrl+Shift+Z", [this] {
        if (auto* c = canvas()) {
            c->cancel_transform();
            c->project().redo();
            c->refresh();
        }
    });
    add(edit, tr("复制"), "Ctrl+C", [this] {
        if (auto* c = canvas()) {
            c->commit_transform();
            auto state = c->project().state();
            const auto* selected = find_layer(state.layers, c->active_layer);
            if (!selected)
                return;
            auto layer = *selected;
            layer.visible = true;
            layer.transform = c->current_layer_transform();
            state.layers = {layer};
            auto image = display_image(render(state));
            QRect bounds;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    auto color = image.pixelColor(x, y);
                    if (!state.selection.isNull())
                        color.setAlpha(color.alpha() * state.selection.constScanLine(y)[x] / 255);
                    image.setPixelColor(x, y, color);
                    if (color.alpha())
                        bounds = bounds.united(QRect(x, y, 1, 1));
                }
            if (!bounds.isEmpty())
                QApplication::clipboard()->setImage(image.copy(bounds));
        }
    });
    add(edit, tr("复制合成"), "Ctrl+Shift+C", [this] {
        if (auto* c = canvas()) {
            auto image = display_image(render(c->project().state()));
            if (!c->project().state().selection.isNull()) {
                const auto& mask = c->project().state().selection;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        auto color = image.pixelColor(x, y);
                        color.setAlpha(color.alpha() * mask.constScanLine(y)[x] / 255);
                        image.setPixelColor(x, y, color);
                    }
            }
            QApplication::clipboard()->setImage(image);
        }
    });
    add(edit, tr("粘贴为新图层"), "Ctrl+V", [this] {
        if (auto* c = canvas()) {
            const auto image = QApplication::clipboard()->image();
            if (image.isNull())
                return;
            Layer l;
            l.name = tr("粘贴");
            l.pixels = to_working(image);
            l.transform.translate((c->spec().width - image.width()) / 2.,
                                  (c->spec().height - image.height()) / 2.);
            c->edit(tr("粘贴"), [&](State& s) { s.layers.push_back(l); });
            layers_->clearSelection();
            c->active_layer = l.id;
            c->prepare_inserted_layer();
            refresh();
        }
    });
    auto* layers = window_->menus_->addMenu(tr("图层(&L)"));
    auto* create = layers->addMenu(tr("新建"));
    for (int kind = 0; kind <= int(LayerKind::composition); ++kind)
        add(create, kind_name(static_cast<LayerKind>(kind)), {},
            [this, kind] { create_layer(kind); });
    for (const auto& text : {tr("复制图层"), tr("蒙版"), tr("反相蒙版"), tr("蒙版转选区"),
                             tr("路径转矢量蒙版"), tr("上移"), tr("下移"), tr("嵌入资源"),
                             tr("盖印可见副本"), tr("合并可见图层"), tr("删除")})
        add(layers, text, text == tr("复制图层") ? QString("Ctrl+J") : QString{}, [this, text] {
            if (text == tr("合并可见图层") &&
                QMessageBox::question(
                    window_, text,
                    tr("合并后将失去可编辑结构。可撤销；若需保留原层，请使用盖印副本。")) !=
                    QMessageBox::Yes)
                return;
            layer_action(text);
        });
    add(layers, tr("替换 / 重新定位源…"), {}, [this] {
        auto* c = canvas();
        if (!c)
            return;
        const auto path = QFileDialog::getOpenFileName(window_, tr("替换源"), {},
                                                       tr("图片 (*.png *.jpg *.jpeg)"));
        if (path.isEmpty())
            return;
        const auto image = import_image(path);
        c->edit(tr("替换源"), [&](State& s) {
            auto& l = editable(c, s);
            l.pixels = image;
            l.linked_path = path;
            l.linked_hash.clear();
        });
    });
    add(edit, tr("自由变换"), "Ctrl+T", [this] {
        if (auto* c = canvas()) {
            c->begin_transform();
            c->setFocus();
        }
    });
    add(layers, tr("数值变换…"), {}, [this] {
        auto* c = canvas();
        if (!c)
            return;
        bool ok{};
        const auto text =
            QInputDialog::getText(window_, tr("变换当前内容 / 蒙版"),
                                  tr("平移 X,Y；缩放 X,Y；旋转角度（五个数，用逗号分隔）"),
                                  QLineEdit::Normal, "0,0,1,1,0", &ok);
        if (!ok)
            return;
        const auto fields = text.split(',');
        if (fields.size() != 5)
            throw EditError(tr("请输入五个数值"));
        std::array<double, 5> values{};
        for (int i = 0; i < 5; ++i) {
            values[static_cast<std::size_t>(i)] = fields[i].toDouble(&ok);
            if (!ok)
                throw EditError(tr("无效数值"));
        }
        QTransform transform;
        transform.translate(values[0], values[1]);
        transform.rotate(values[4]);
        transform.scale(values[2], values[3]);
        c->edit(tr("数值变换"), [&](State& s) {
            auto& l = editable(c, s);
            if (c->mask_target)
                l.mask_transform = l.mask_transform * transform;
            else {
                l.transform = l.transform * transform;
                if (l.mask_linked)
                    l.mask_transform = l.mask_transform * transform;
            }
        });
    });
    auto* selection = window_->menus_->addMenu(tr("选择(&S)"));
    for (const auto& text :
         {tr("全选"), tr("取消选区"), tr("反选"), tr("羽化"), tr("扩张"), tr("收缩"), tr("平滑"),
          tr("保存选区"), tr("载入选区"), tr("保存路径"), tr("路径转选区"), tr("亮度范围"),
          tr("线性渐变范围"), tr("径向渐变范围")})
        add(selection, text,
            text == tr("取消选区") ? "Ctrl+D"
            : text == tr("全选")   ? "Ctrl+A"
            : text == tr("反选")   ? "Ctrl+Shift+I"
                                   : QString{},
            [this, text] { selection_action(text); });
    auto* view = window_->menus_->actions().at(1)->menu();
    add(view, tr("旋转视图…"), {}, [this] {
        if (auto* c = canvas()) {
            bool ok{};
            const auto angle = QInputDialog::getDouble(window_, tr("旋转视图"), tr("角度"),
                                                       c->rotation, -180, 180, 1, &ok);
            if (ok) {
                c->rotation = angle;
                c->update();
            }
        }
    });
    for (const auto& text : {tr("网格"), tr("标尺"), tr("吸附")}) {
        auto* action = add(view, text, {}, [this, text] {
            if (auto* c = canvas()) {
                if (text == tr("网格"))
                    c->show_grid = !c->show_grid;
                if (text == tr("标尺"))
                    c->show_rulers = !c->show_rulers;
                if (text == tr("吸附"))
                    c->snap = !c->snap;
                c->update();
            }
        });
        action->setCheckable(true);
        action->setChecked(text != tr("网格"));
    }
    add(view, tr("添加参考线…"), {}, [this] {
        if (auto* c = canvas()) {
            bool ok{};
            const auto text =
                QInputDialog::getText(window_, tr("参考线"), tr("H 或 V，逗号，像素位置"),
                                      QLineEdit::Normal, "V,100", &ok);
            if (!ok)
                return;
            const auto fields = text.split(',');
            if (fields.size() != 2)
                throw EditError(tr("请输入 H,100 或 V,100"));
            const double value = fields[1].toDouble(&ok);
            if (!ok)
                throw EditError(tr("无效位置"));
            c->edit(tr("参考线"), [&](State& s) {
                (fields[0].trimmed().toUpper() == "H" ? s.horizontal_guides : s.vertical_guides)
                    .push_back(value);
            });
        }
    });
    add(view, tr("创建比较快照…"), {}, [this] {
        if (auto* c = canvas()) {
            bool ok{};
            const auto name = QInputDialog::getText(window_, tr("快照"), tr("名称"),
                                                    QLineEdit::Normal, tr("比较快照"), &ok);
            if (ok && !name.isEmpty())
                c->edit(tr("保存快照"), [&](State& s) { s.snapshots.insert(name, render(s)); });
        }
    });
    add(view, tr("查看比较快照…"), {}, [this] {
        if (auto* c = canvas()) {
            QStringList items{tr("当前编辑")};
            items += c->project().state().snapshots.keys();
            bool ok{};
            const auto name = QInputDialog::getItem(
                window_, tr("比较"), tr("基准（导出始终使用当前编辑）"), items, 0, false, &ok);
            if (ok) {
                c->comparison = name == tr("当前编辑") ? QString{} : name;
                c->update();
            }
        }
    });
    add(view, tr("保存工作区布局"), {},
        [this] { QSettings().setValue("workspaceM1", window_->saveState(1)); });
    add(view, tr("恢复已存工作区"), {},
        [this] { window_->restoreState(QSettings().value("workspaceM1").toByteArray(), 1); });
    add(layers, tr("编辑嵌套合成…"), {}, [this] {
        auto* parent = canvas();
        if (!parent)
            return;
        const auto* layer = find_layer(parent->project().state().layers, parent->active_layer);
        if (!layer || layer->kind != LayerKind::composition)
            throw EditError(tr("请选择嵌套合成"));
        const auto id = layer->id;
        const auto revision = parent->project().revision();
        State child;
        child.name = layer->name;
        child.size = parent->project().state().size;
        child.layers = layer->children;
        QDialog dialog(window_);
        dialog.setWindowTitle(tr("嵌套合成 › %1").arg(layer->name));
        dialog.resize(1200, 800);
        auto* layout = new QVBoxLayout(&dialog);
        auto* editor = new MainWindow(&dialog);
        editor->setWindowFlags(Qt::Widget);
        CanvasSpec spec;
        spec.name = child.name.toStdString();
        spec.width = child.size.width();
        spec.height = child.size.height();
        if (!editor->create_document(spec))
            throw EditError(tr("无法打开子合成"));
        auto* child_canvas = editor->active_canvas();
        child_canvas->load_state(child, false);
        layout->addWidget(editor);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QPointer<CanvasView> original = child_canvas;
        if (dialog.exec() == QDialog::Accepted) {
            if (!original || parent->project().revision() != revision)
                throw EditError(tr("源文档已变化，不能提交旧合成"));
            const auto result = original->project().state().layers;
            parent->edit(tr("编辑嵌套合成"), [&](State& state) {
                auto* target = find_layer(state.layers, id);
                if (!target || target->locked)
                    throw EditError(tr("目标不可编辑"));
                target->children = result;
            });
        }
    });
    add(layers, tr("填充颜色 / 渐变…"), {}, [this] {
        auto* c = canvas();
        if (!c)
            return;
        const auto color = QColorDialog::getColor(Qt::white, window_, tr("起始颜色"));
        if (!color.isValid())
            return;
        const bool gradient =
            QMessageBox::question(window_, tr("填充"), tr("使用双色渐变？选择否为纯色。")) ==
            QMessageBox::Yes;
        const auto end =
            gradient ? QColorDialog::getColor(Qt::black, window_, tr("结束颜色")) : color;
        if (!end.isValid())
            return;
        c->edit(tr("填充参数"), [&](State& state) {
            auto& l = editable(c, state);
            if (l.kind != LayerKind::fill)
                throw EditError(tr("请选择填充层"));
            l.fill = color;
            l.fill_end = end;
            l.gradient = gradient;
        });
    });
    add(selection, tr("重命名路径…"), {}, [this] {
        auto* c = canvas();
        auto* item = paths_->currentItem();
        if (!c || !item)
            return;
        const auto old = item->text();
        bool ok{};
        const auto name = QInputDialog::getText(window_, tr("重命名路径"), tr("名称"),
                                                QLineEdit::Normal, old, &ok);
        if (ok && !name.trimmed().isEmpty())
            c->edit(tr("重命名路径"), [&](State& state) {
                if (name != old && state.paths.contains(name))
                    throw EditError(tr("名称已存在"));
                const auto value = state.paths.take(old);
                state.paths.insert(name, value);
            });
    });
    add(selection, tr("隐藏当前路径"), {}, [this] {
        if (auto* c = canvas()) {
            c->path = {};
            c->update();
        }
    });
    add(selection, tr("删除已存路径"), {}, [this] {
        auto* c = canvas();
        auto* item = paths_->currentItem();
        if (!c || !item)
            return;
        const auto name = item->text();
        c->edit(tr("删除路径"), [&](State& state) { state.paths.remove(name); });
        c->path = {};
        c->update();
    });
    for (const auto& operation :
         {tr("左对齐画布"), tr("水平居中画布"), tr("垂直居中画布"), tr("水平分布")})
        add(layers, operation, {}, [this, operation] {
            auto* c = canvas();
            if (!c)
                return;
            std::vector<QUuid> ids;
            for (const auto* item : layers_->selectedItems())
                ids.push_back(item->data(0, Qt::UserRole).toUuid());
            c->edit(operation, [&](State& state) {
                for (std::size_t i = 0; i < ids.size(); ++i) {
                    auto* l = find_layer(state.layers, ids[i]);
                    if (!l || l->locked)
                        throw EditError(tr("选择含锁定图层"));
                    const auto size = l->pixels.isNull() ? state.size : l->pixels.size();
                    const auto bounds = l->transform.mapRect(QRectF(QPointF(), QSizeF(size)));
                    double dx{}, dy{};
                    if (operation == tr("左对齐画布"))
                        dx = -bounds.left();
                    if (operation == tr("水平居中画布"))
                        dx = state.size.width() / 2. - bounds.center().x();
                    if (operation == tr("垂直居中画布"))
                        dy = state.size.height() / 2. - bounds.center().y();
                    if (operation == tr("水平分布"))
                        dx = ids.size() > 1 ? double(i) * (state.size.width() - bounds.width()) /
                                                      double(ids.size() - 1) -
                                                  bounds.left()
                                            : 0;
                    QTransform transform;
                    transform.translate(dx, dy);
                    l->transform = l->transform * transform;
                    if (l->mask_linked)
                        l->mask_transform = l->mask_transform * transform;
                }
            });
        });
    add(selection, tr("变换选区…"), {}, [this] {
        auto* c = canvas();
        if (!c)
            return;
        bool ok{};
        const double scale = QInputDialog::getDouble(window_, tr("变换选区"), tr("以中心缩放比例"),
                                                     1, .01, 100, 3, &ok);
        if (!ok)
            return;
        c->edit(tr("变换选区"), [&](State& state) {
            if (state.selection.isNull())
                throw EditError(tr("当前没有选区"));
            QImage image(state.size, QImage::Format_RGB32);
            image.fill(Qt::black);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.translate(state.size.width() / 2., state.size.height() / 2.);
            painter.scale(scale, scale);
            painter.translate(-state.size.width() / 2., -state.size.height() / 2.);
            painter.drawImage(0, 0, state.selection);
            painter.end();
            state.selection = image.convertToFormat(QImage::Format_Grayscale8);
        });
    });
    add(view, tr("设置菜单快捷键…"), {}, [this] {
        QStringList names;
        QList<QAction*> actions;
        for (auto* action : window_->findChildren<QAction*>())
            if (!action->isSeparator() && !action->menu() && !action->text().isEmpty()) {
                names << action->text();
                actions << action;
            }
        bool ok{};
        const auto name =
            QInputDialog::getItem(window_, tr("快捷键"), tr("命令"), names, 0, false, &ok);
        if (!ok)
            return;
        auto* action = actions.at(names.indexOf(name));
        const auto value =
            QInputDialog::getText(window_, tr("快捷键"), tr("组合键，例如 Ctrl+Shift+K；留空取消"),
                                  QLineEdit::Normal, action->shortcut().toString(), &ok);
        if (!ok)
            return;
        const QKeySequence key(value);
        for (auto* other : actions)
            if (other != action && !key.isEmpty() && other->shortcut() == key)
                throw EditError(tr("与 %1 冲突").arg(other->text()));
        action->setShortcut(key);
        QSettings().setValue("shortcuts/" + name, key.toString());
    });
    const auto top = window_->menus_->actions();
    for (auto* action : top)
        window_->menus_->removeAction(action);
    for (auto* menu : {file, edit, layers, selection, view})
        window_->menus_->addMenu(menu);
    window_->menus_->addAction(top.at(2));
    for (auto* action : window_->findChildren<QAction*>()) {
        const auto value = QSettings().value("shortcuts/" + action->text());
        if (value.isValid())
            action->setShortcut(QKeySequence(value.toString()));
    }
    auto* tools = new QToolBar(tr("工具"), window_);
    tools->setObjectName("editingTools");
    tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tools->setIconSize(QSize(24, 24));
    tools->setFixedWidth(42);
    tools->setMovable(false);
    window_->addToolBar(Qt::LeftToolBarArea, tools);
    const QStringList labels{tr("移动 V"), tr("抓手 H"), tr("矩形 M"),  tr("椭圆"),
                             tr("套索 L"), tr("多边形"), tr("钢笔 P"),  tr("画笔 B"),
                             tr("橡皮 E"), tr("魔棒 W"), tr("颜色范围")};
    tools->setStyleSheet(
        "QToolBar { spacing:2px; } QToolButton { width:28px; height:28px; padding:3px; border:1px "
        "solid transparent; border-radius:3px; } QToolButton:hover { background:#383e44; } "
        "QToolButton:checked { background:#245a86; border-color:#529ad3; }");
    auto* group = new QActionGroup(tools);
    group->setObjectName("toolActions");
    const QStringList drawings{
        R"(<path d="M5 3v16l4-4 4 7 3-2-4-6h7Z"/>)",
        R"(<path d="M8 12V6q0-3 2-1v6-7q2-3 3 0v7-6q2-2 3 0v7-4q2-2 3 1v7q0 6-6 6h-1q-3 0-5-4l-4-5q0-3 3-1l2 2"/>)",
        R"(<rect x="4" y="5" width="16" height="14" stroke-dasharray="2 2"/>)",
        R"(<ellipse cx="12" cy="12" rx="8" ry="7" stroke-dasharray="2 2"/>)",
        R"(<path d="M7 17C-3 10 5 3 13 4s11 10 2 13q-8 3-9-1t5 0q1 5-5 6"/>)",
        R"(<path d="m4 5 16 3-4 12-12-5Z" stroke-dasharray="3 2"/>)",
        R"(<path d="m4 20 3-11L19 3l-6 12Z M4 20l6-8"/><circle cx="11" cy="11" r="2"/>)",
        R"(<path d="M9 14 19 3q3-1 2 2L12 17Z M9 15q-6-1-5 4l-2 2q8 1 8-5"/>)",
        R"(<path d="m4 13 9-10 8 7-10 11H8Z M8 9l8 7 M11 21h10"/>)",
        R"(<path d="m4 21 13-13 3 3L7 24 M14 11l3 3 M6 2v6 M3 5h6 M18 1v4 M16 3h4 M21 17v5 M19 19h4"/>)",
        R"(<circle cx="9" cy="10" r="6"/><circle cx="15" cy="15" r="6"/><path d="m8 12 4-4 M10 15l5-5"/>)"};
    for (int i : {0, 2, 3, 4, 5, 9, 10, 6, 7, 8, 1}) {
        const auto svg =
            ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 26 26'><g fill='none' "
             "stroke='#d5d9df' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'>" +
             drawings[i] + "</g></svg>")
                .toUtf8();
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QSvgRenderer renderer(svg);
        renderer.render(&painter);
        painter.end();
        pixmap.setDevicePixelRatio(2);
        auto* action = tools->addAction(QIcon(pixmap), labels[i]);
        action->setCheckable(true);
        action->setData(i);
        group->addAction(action);
        action->setToolTip(labels[i]);
        connect(action, &QAction::triggered, this, [this, i] {
            if (auto* c = canvas()) {
                c->set_tool(static_cast<CanvasTool>(i));
                c->setFocus();
            }
        });
    }
    auto* options = window_->findChild<QToolBar*>("viewToolbar");
    auto* operation = new QComboBox(options);
    operation->addItems({tr("替换选区"), tr("相加"), tr("相减"), tr("相交")});
    options->addWidget(operation)->setObjectName("selectionOptions");
    connect(operation, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (auto* c = canvas())
            c->range_operation = static_cast<RangeOp>(index);
    });
    auto* radius = new QDoubleSpinBox(options);
    radius->setObjectName("brushRadius");
    radius->setRange(1, 500);
    radius->setValue(20);
    radius->setPrefix(tr("笔刷半径 "));
    options->addWidget(radius)->setObjectName("brushOptions");
    connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (auto* c = canvas())
            c->brush_radius = value;
    });
    for (const auto& entry : {std::pair{"autoSelect", tr("自动选择：图层")},
                              std::pair{"transformControls", tr("显示变换控件")},
                              std::pair{"keepAspect", tr("保持比例")}}) {
        auto* check = new QCheckBox(entry.second, options);
        check->setObjectName(entry.first);
        check->setChecked(true);
        options->addWidget(check)->setObjectName(QString("move_") + entry.first);
        connect(check, &QCheckBox::toggled, this, [this, name = QString(entry.first)](bool value) {
            if (auto* c = canvas()) {
                if (name == "autoSelect")
                    c->auto_select = value;
                else if (name == "transformControls")
                    c->show_transform_controls = value;
                else
                    c->keep_aspect = value;
                c->update();
            }
        });
    }
    auto* apply = options->addAction(tr("确认 ✓"));
    apply->setObjectName("transformApply");
    connect(apply, &QAction::triggered, this, [this] {
        if (auto* c = canvas())
            c->commit_transform();
    });
    auto* cancel = options->addAction(tr("取消 ×"));
    cancel->setObjectName("transformCancel");
    connect(cancel, &QAction::triggered, this, [this] {
        if (auto* c = canvas())
            c->cancel_transform();
    });
    auto* color = options->addAction(tr("前景色"));
    color->setObjectName("brushColor");
    connect(color, &QAction::triggered, this, [this] {
        if (auto* c = canvas()) {
            const auto selected = QColorDialog::getColor(c->foreground, window_, tr("前景色"));
            if (selected.isValid()) {
                c->foreground = selected;
                refresh_tools();
            }
        }
    });
    refresh_tools();
}
void EditorController::refresh_tools() {
    auto* c = canvas();
    const auto tool = c ? c->tool() : CanvasTool::move;
    if (auto* group = window_->findChild<QActionGroup*>("toolActions"))
        for (auto* action : group->actions()) {
            action->setChecked(action->data().toInt() == int(tool));
            action->setEnabled(c != nullptr);
        }
    auto* options = window_->findChild<QToolBar*>("viewToolbar");
    if (!options)
        return;
    for (auto* action : options->actions()) {
        const auto name = action->objectName();
        if (name.startsWith("move_"))
            action->setVisible(c && tool == CanvasTool::move);
        if (name == "selectionOptions")
            action->setVisible(c && (tool == CanvasTool::rectangle || tool == CanvasTool::ellipse ||
                                     tool == CanvasTool::lasso || tool == CanvasTool::polygon ||
                                     tool == CanvasTool::wand || tool == CanvasTool::color_range));
        if (name == "brushOptions" || name == "brushColor")
            action->setVisible(c && (tool == CanvasTool::brush || tool == CanvasTool::eraser));
        if (name == "transformApply" || name == "transformCancel")
            action->setVisible(c && c->transforming());
    }
    if (c)
        if (auto* radius = options->findChild<QDoubleSpinBox*>("brushRadius")) {
            QSignalBlocker blocker(radius);
            radius->setValue(c->brush_radius);
        }
    if (c)
        for (const auto& entry : {std::pair{"autoSelect", c->auto_select},
                                  std::pair{"transformControls", c->show_transform_controls},
                                  std::pair{"keepAspect", c->keep_aspect}})
            if (auto* check = options->findChild<QCheckBox*>(entry.first)) {
                QSignalBlocker blocker(check);
                check->setChecked(entry.second);
            }
}
bool EditorController::eventFilter(QObject* object, QEvent* event) {
    if (object == window_ && event->type() == QEvent::DragEnter) {
        auto* drag = static_cast<QDragEnterEvent*>(event);
        if (drag->mimeData()->hasUrls()) {
            drag->acceptProposedAction();
            return true;
        }
    }
    if (object == window_ && event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        guarded([&] {
            for (const auto& url : drop->mimeData()->urls())
                if (url.isLocalFile())
                    open(url.toLocalFile(), canvas() != nullptr);
        });
        drop->acceptProposedAction();
        return true;
    }
    if (object == layers_->viewport() && event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        auto* destination = layers_->itemAt(drop->position().toPoint());
        if (!canvas())
            return true;
        std::vector<QUuid> ids;
        for (const auto* item : layers_->selectedItems())
            ids.push_back(item->data(0, Qt::UserRole).toUuid());
        QUuid parent;
        int position = 4096;
        if (destination) {
            const auto id = destination->data(0, Qt::UserRole).toUuid();
            const auto* target = find_layer(canvas()->project().state().layers, id);
            const auto rect = layers_->visualItemRect(destination);
            const auto y = drop->position().y();
            if (target &&
                (target->kind == LayerKind::group || target->kind == LayerKind::composition) &&
                y > rect.top() + 8 && y < rect.bottom() - 8)
                parent = id;
            else {
                const auto* parent_item = destination->parent();
                if (parent_item)
                    parent = parent_item->data(0, Qt::UserRole).toUuid();
                const auto& siblings =
                    parent.isNull()
                        ? canvas()->project().state().layers
                        : find_layer(canvas()->project().state().layers, parent)->children;
                for (std::size_t i = 0; i < siblings.size(); ++i)
                    if (siblings[i].id == id)
                        position = int(i) + (y < rect.center().y() ? 1 : 0);
            }
        }
        guarded([&] {
            canvas()->edit(tr("移动图层"), [&](State& state) {
                for (const auto& id : ids)
                    move_layer(state, id, parent, position);
            });
        });
        drop->acceptProposedAction();
        return true;
    }
    return QObject::eventFilter(object, event);
}
} // namespace huelith::desktop
