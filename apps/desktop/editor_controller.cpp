#include "editor_controller.h"
#include "canvas_view.h"
#include "export_dialog.h"
#include "main_window.h"
#include "raw_dialog.h"
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
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
#include <QGridLayout>
#include <QHeaderView>
#include <QImageWriter>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
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
class LayerTree final : public QTreeWidget {
  public:
    explicit LayerTree(QWidget* parent) : QTreeWidget(parent) {
        setProperty("dragToken", QUuid::createUuid().toByteArray());
    }

  protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        auto* mime = QTreeWidget::mimeData(items);
        mime->setData("application/x-huelith-layer-tree", property("dragToken").toByteArray());
        return mime;
    }
};
class LayerDelegate final : public QStyledItemDelegate {
  public:
    explicit LayerDelegate(std::function<CanvasView*()> canvas, QObject* parent)
        : QStyledItemDelegate(parent), canvas_(std::move(canvas)) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        auto* c = canvas_();
        const auto id = index.siblingAtColumn(0).data(Qt::UserRole).toUuid();
        const auto* layer = c ? find_layer(c->project().state().layers, id) : nullptr;
        if (!layer) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        painter->save();
        painter->fillRect(option.rect, option.state.testFlag(QStyle::State_Selected)
                                           ? QColor("#334e64")
                                           : QColor("#252a2e"));
        const auto box = option.rect;
        const int middle = box.center().y();
        if (index.column() == 0) {
            painter->setPen(QPen(QColor("#cdd1d6"), 1.3));
            painter->setBrush(Qt::NoBrush);
            if (layer->visible) {
                painter->drawEllipse(QRectF(box.left() + 3, middle - 4, 14, 8));
                painter->setBrush(QColor("#cdd1d6"));
                painter->drawEllipse(QPointF(box.left() + 10, middle), 2, 2);
            }
            const QRect thumb(box.left() + 24, middle - 13, 34, 26);
            painter->fillRect(thumb, QColor("#555b60"));
            if (!layer->pixels.isNull())
                painter->drawImage(thumb,
                                   display_image(layer->pixels.scaled(34, 26, Qt::KeepAspectRatio,
                                                                      Qt::SmoothTransformation)));
            if (c->active_layer == id && !c->mask_target) {
                painter->setPen(QPen(Qt::white, 1));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(thumb.adjusted(-1, -1, 1, 1));
            }
            painter->setPen(QColor("#dde1e5"));
            painter->drawText(box.adjusted(67, 0, -4, 0), Qt::AlignVCenter,
                              layer->name + (layer->locked || layer->background
                                                 ? QStringLiteral("  🔒")
                                                 : QString{}));
        } else if (index.column() == 2 && !layer->mask.isNull()) {
            const QRect thumb(box.left() + 5, middle - 13, 32, 26);
            painter->drawImage(thumb, layer->mask);
            painter->setPen(
                QPen(c->active_layer == id && c->mask_target ? Qt::white : Qt::gray, 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(thumb.adjusted(-1, -1, 1, 1));
            if (!layer->mask_enabled) {
                painter->setPen(QPen(Qt::red, 2));
                painter->drawLine(thumb.topLeft(), thumb.bottomRight());
                painter->drawLine(thumb.topRight(), thumb.bottomLeft());
            }
        }
        painter->restore();
    }

  private:
    std::function<CanvasView*()> canvas_;
};
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
    layers_ = new LayerTree(tabs);
    layers_->setObjectName("layerTree");
    layers_->setHeaderLabels({tr("图层"), tr("类型"), tr("目标")});
    layers_->setHeaderHidden(true);
    layers_->setColumnHidden(1, true);
    layers_->setColumnWidth(2, 44);
    layers_->header()->setStretchLastSection(false);
    layers_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layers_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    layers_->setItemDelegate(new LayerDelegate([this] { return canvas(); }, layers_));
    layers_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    layers_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layers_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(
        layers_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
            if (column == 0) {
                const auto point = layers_->viewport()->property("clickPosition").toPoint();
                const auto bounds = layers_->visualItemRect(item);
                const auto* layer = canvas() ? find_layer(canvas()->project().state().layers,
                                                          item->data(0, Qt::UserRole).toUuid())
                                             : nullptr;
                if (layer && point.x() < bounds.left() + 62 &&
                    (layer->kind == LayerKind::source || layer->kind == LayerKind::composition)) {
                    for (auto* action : window_->findChildren<QAction*>())
                        if (action->text() == tr("编辑嵌套合成…")) {
                            action->trigger();
                            return;
                        }
                }
                layers_->editItem(item, 0);
            }
        });
    layers_->setDragDropMode(QAbstractItemView::InternalMove);
    layers_->setAutoExpandDelay(500);
    layers_->setDropIndicatorShown(true);
    layers_->viewport()->installEventFilter(this);
    layers_->setDefaultDropAction(Qt::MoveAction);
    auto* layer_tab = new QWidget(tabs);
    auto* layer_layout = new QVBoxLayout(layer_tab);
    layer_layout->setContentsMargins(0, 0, 0, 0);
    layer_controls_ = new QWidget(layer_tab);
    layer_layout->addWidget(layer_controls_);
    layer_layout->addWidget(layers_);
    tabs->addTab(layer_tab, tr("图层"));
    connect(layers_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (refreshing_ || !canvas())
            return;
        auto* c = canvas();
        c->selected_layers.clear();
        for (auto* item : layers_->selectedItems())
            c->selected_layers.push_back(item->data(0, Qt::UserRole).toUuid());
        c->update();
    });

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
        const QString symbol = operation == tr("新建")   ? QStringLiteral("＋")
                               : operation == tr("组")   ? QStringLiteral("▱")
                               : operation == tr("蒙版") ? QStringLiteral("▣")
                               : operation == tr("调整") ? QStringLiteral("◐")
                                                         : QStringLiteral("⌫");
        auto* action = buttons->addAction(symbol);
        action->setToolTip(operation);
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
            c->mask_view = column == 2 && layer_click_modifiers_.testFlag(Qt::AltModifier);
            if (column == 2 && layer_click_modifiers_.testFlag(Qt::ShiftModifier))
                guarded([&] {
                    c->edit(tr("蒙版启停"), [&](State& s) {
                        auto& l = editable(c, s);
                        l.mask_enabled = !l.mask_enabled;
                    });
                });
            c->update();
            QTimer::singleShot(0, this, [this] { refresh(false); });
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
            c->set_path(c->project().state().paths.value(item->text()));
            c->update();
        }
    });
    connect(history_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (auto* c = canvas()) {
            const auto index = std::size_t(history_->row(item));
            if (QApplication::keyboardModifiers().testFlag(Qt::AltModifier)) {
                c->history_source = render(c->project().history_state(index));
                return;
            }
            c->cancel_transform();
            c->project().seek_history(index);
            c->refresh();
        }
    });
    build_properties();
    configure_menus();
    window_->setAcceptDrops(true);
    window_->installEventFilter(this);
    if (QCoreApplication::applicationName() == "Huelith")
        QTimer::singleShot(0, this, [this] {
            if (window_->isVisible())
                guarded([&] { offer_recovery(); });
        });
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
    if (auto* group = window_->findChild<QActionGroup*>("toolActions"))
        for (auto* action : group->actions())
            c->addAction(action);
    c->tool_changed = [this] { refresh_tools(); };
    c->changed = [this] {
        QTimer::singleShot(0, this, [this] {
            window_->refresh_document_state();
            refresh();
        });
    };
    c->recovery_file =
        recovery_directory() + "/" + QUuid::createUuid().toString(QUuid::Id128) + ".huelith";
}
QString EditorController::recovery_directory() const {
    return QSettings()
        .value("storage/recoveryDirectory",
               QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery")
        .toString();
}
void EditorController::refresh(bool rebuild_tree) {
    if (refreshing_)
        return;
    refreshing_ = true;
    QSet<QUuid> selected;
    if (canvas())
        for (const auto& id : canvas()->selected_layers)
            selected.insert(id);
    if (rebuild_tree)
        layers_->clear();
    paths_->clear();
    history_->clear();
    auto* c = canvas();
    if (c) {
        const auto& s = c->display_state();
        if (!find_layer(s.layers, c->active_layer)) {
            c->active_layer = s.layers.empty() ? QUuid{} : s.layers.back().id;
            c->mask_target = false;
        }
        if (rebuild_tree)
            add_rows(layers_, nullptr, s.layers, c->active_layer);
        for (QTreeWidgetItemIterator it(layers_); *it; ++it)
            if (selected.contains((*it)->data(0, Qt::UserRole).toUuid()))
                (*it)->setSelected(true);
        paths_->addItems(s.paths.keys());
        history_->addItems(c->project().history_names());
        history_->setCurrentRow(int(c->project().history_position()));
        const auto* layer = find_layer(s.layers, c->active_layer);
        opacity_->setEnabled(layer && !layer->background);
        blend_->setEnabled(layer && !layer->background);
        exposure_->setEnabled(layer && layer->kind != LayerKind::group);
        if (layer) {
            blend_->setCurrentIndex(int(layer->blend));
            opacity_->setValue(layer->opacity * 100);
            exposure_->setValue(layer->exposure);
            mask_->setChecked(layer->mask_enabled);
            linked_->setChecked(layer->mask_linked);
            isolated_->setChecked(layer->isolated);
            locked_->setChecked(layer->locked || layer->background);
            locked_->setText(layer->background ? tr("背景锁定（解除后转换为普通图层）")
                                               : tr("锁定图层"));
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
    auto* layer_form = new QFormLayout(layer_controls_);
    layer_form->setContentsMargins(4, 4, 4, 4);
    opacity_ = new QDoubleSpinBox(panel);
    opacity_->setRange(0, 100);
    opacity_->setSuffix(" %");
    opacity_->setObjectName("layerOpacity");
    layer_form->addRow(tr("不透明度"), opacity_);
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
    auto* flags = new QGridLayout;
    layer_form->addRow(flags);
    int flag_index = 0;
    for (auto pair : {std::pair{&mask_, tr("启用蒙版")}, std::pair{&linked_, tr("链接蒙版")},
                      std::pair{&isolated_, tr("隔离组")}, std::pair{&locked_, tr("锁定图层")},
                      std::pair{&clipping_, tr("剪贴到下方基底")}}) {
        *pair.first = new QCheckBox(pair.second, panel);
        flags->addWidget(*pair.first, flag_index / 2, flag_index % 2);
        ++flag_index;
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
                    if (key == 3) {
                        if (!value && l->background) {
                            l->background = false;
                            l->name = tr("图层 0");
                        }
                        l->locked = value;
                    }
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
    layer_form->insertRow(0, tr("混合模式"), blend_);
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
    layer_form->addRow(slider);
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
            canvas()->stage_layer(nested);
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
        layer.linked_path = QFileInfo(path).absoluteFilePath();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            layer.linked_hash =
                QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (QStringList{"cr3", "cr2", "nef", "arw", "dng", "raf", "rw2", "orf"}.contains(suffix)) {
            if (!file.isOpen() || file.size() > 512LL * 1024 * 1024 || !file.seek(0))
                throw EditError(tr("无法读取 RAW 源文件"));
            layer.raw_source = file.readAll();
            RawDialog dialog({layer}, window_);
            if (dialog.exec() != QDialog::Accepted)
                return false;
            layer = dialog.results().front();
        } else {
            layer.pixels = import_image(path);
        }
        if (place && canvas()) {
            layer.transform.translate((canvas()->spec().width - layer.pixels.width()) / 2.,
                                      (canvas()->spec().height - layer.pixels.height()) / 2.);
            canvas()->stage_layer(layer);
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
void EditorController::open_files(const QStringList& paths) {
    std::vector<Layer> raw_sources;
    QStringList ordinary;
    qint64 source_bytes{};
    for (const auto& path : paths) {
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (!QStringList{"cr3", "cr2", "nef", "arw", "dng", "raf", "rw2", "orf"}.contains(suffix)) {
            ordinary.append(path);
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 512LL * 1024 * 1024 ||
            source_bytes + file.size() > 1024LL * 1024 * 1024)
            throw EditError(tr("无法读取 RAW，或本组源文件超过 1 GiB，请减少文件数"));
        source_bytes += file.size();
        Layer layer;
        layer.kind = LayerKind::source;
        layer.name = QFileInfo(path).completeBaseName();
        layer.linked_path = QFileInfo(path).absoluteFilePath();
        layer.raw_source = file.readAll();
        if (layer.raw_source.size() != file.size())
            throw EditError(tr("RAW 源文件读取不完整"));
        layer.linked_hash = QCryptographicHash::hash(layer.raw_source, QCryptographicHash::Sha256);
        raw_sources.push_back(std::move(layer));
    }
    if (!raw_sources.empty()) {
        RawDialog dialog(std::move(raw_sources), window_);
        if (dialog.exec() == QDialog::Accepted) {
            for (const auto& layer : dialog.results()) {
                CanvasSpec spec;
                spec.name = layer.name.toStdString();
                spec.width = layer.pixels.width();
                spec.height = layer.pixels.height();
                if (!window_->create_document(spec))
                    throw EditError(tr("无法创建 RAW 图片文档"));
                canvas()->edit(tr("打开 RAW 图片"), [&](State& state) {
                    state.layers = {layer};
                    state.snapshots.insert(tr("导入状态"), render(state));
                });
                canvas()->active_layer = layer.id;
                canvas()->prepare_inserted_layer();
                update_recent(layer.linked_path);
            }
            layers_->clearSelection();
            refresh();
        }
    }
    for (const auto& path : ordinary)
        open(path);
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

    if (c)
        c->commit_transform();
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
        if (operation == tr("背景转换为普通图层")) {
            layer.background = false;
            layer.name = tr("图层 0");
        } else if (operation == tr("复制图层")) {
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
            window_, tr("打开图片或工程"), {},
            tr("图片 / RAW / 工程 (*.png *.jpg *.jpeg *.tif *.tiff *.cr3 *.cr2 *.nef *.arw *.dng "
               "*.raf *.rw2 *.orf *.huelith)"));
        open_files(paths);
    });
    add(file, tr("置入…"), {}, [this] {
        open(QFileDialog::getOpenFileName(window_, tr("置入当前图片"), {},
                                          tr("图片 / 工程 (*.png *.jpg *.jpeg *.huelith)")),
             true);
    });
    add(file, tr("保存工程"), "Ctrl+S", [this] { save(canvas()); });
    add(file, tr("另存工程…"), "Ctrl+Shift+S", [this] { save(canvas(), true); });
    add(file, tr("导出 PNG / JPEG…"), "Ctrl+Alt+Shift+S", [this] { show_export(); });
    add(file, tr("批量导出与队列…"), {}, [this] {
        if (!export_queue_)
            export_queue_ = new ExportDialog(window_);
        QList<CanvasView*> sources;
        for (int i = 0; i < window_->documents_->count(); ++i)
            if (auto* source = dynamic_cast<CanvasView*>(window_->documents_->widget(i)))
                sources.append(source);
        export_queue_->set_sources(sources);
        export_queue_->show();
        export_queue_->raise();
    });
    recent_ = file->addMenu(tr("最近打开"));
    const auto recent = QSettings().value("recentProjects").toStringList();
    for (const auto& item : recent)
        add(recent_, item, {}, [this, item] { open(item); });
    add(file, tr("打开自动恢复…"), {}, [this] {
        const auto path = QFileDialog::getOpenFileName(
            window_, tr("选择恢复副本"), recovery_directory(), tr("Huelith 工程 (*.huelith)"));
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
    add(edit, tr("复制"), "Ctrl+C", [this] { copy_pixels(false, false); });
    add(edit, tr("复制合成"), "Ctrl+Shift+C", [this] { copy_pixels(true, false); });
    add(edit, tr("剪切"), "Ctrl+X", [this] { copy_pixels(false, true); });
    add(edit, tr("粘贴为新图层"), "Ctrl+V", [this] { paste_pixels(false, false); });
    add(edit, tr("原位粘贴"), "Ctrl+Shift+V", [this] { paste_pixels(true, false); });
    add(edit, tr("贴入选区"), "Ctrl+Alt+Shift+V", [this] { paste_pixels(false, true); });
    auto* layers = window_->menus_->addMenu(tr("图层(&L)"));
    auto* create = layers->addMenu(tr("新建"));
    add(layers, tr("智能效果…"), {}, [this] { show_effects(); });
    for (int kind = 0; kind <= int(LayerKind::composition); ++kind)
        add(create, kind_name(static_cast<LayerKind>(kind)), {},
            [this, kind] { create_layer(kind); });
    for (const auto& text : {tr("复制图层"), tr("背景转换为普通图层"), tr("蒙版"), tr("反相蒙版"),
                             tr("蒙版转选区"), tr("路径转矢量蒙版"), tr("上移"), tr("下移"),
                             tr("嵌入资源"), tr("盖印可见副本"), tr("合并可见图层"), tr("删除")})
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
                window_->refresh_document_state();
            }
        }
    });
    add(view, tr("复位视图旋转"), {}, [this] {
        if (auto* c = canvas()) {
            c->rotation = 0;
            c->update();
            window_->refresh_document_state();
        }
    });
    for (const auto& text : {tr("网格"), tr("标尺"), tr("吸附")}) {
        auto* action = add(view, text,
                           text == tr("标尺")   ? QString("Ctrl+R")
                           : text == tr("网格") ? QString("Ctrl+'")
                                                : QString("Ctrl+Shift+;"),
                           [this, text] {
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
    for (const auto& label : {tr("显示参考线"), tr("锁定参考线")}) {
        auto* action =
            add(view, label, label == tr("显示参考线") ? QString("Ctrl+;") : QString("Ctrl+Alt+;"),
                [this, label] {
                    if (auto* c = canvas()) {
                        if (label == tr("显示参考线"))
                            c->show_guides = !c->show_guides;
                        else
                            c->lock_guides = !c->lock_guides;
                        c->update();
                    }
                });
        action->setCheckable(true);
        action->setChecked(label == tr("显示参考线"));
    }
    add(view, tr("清除参考线"), {}, [this] {
        if (auto* c = canvas())
            c->edit(tr("清除参考线"), [](State& state) {
                state.horizontal_guides.clear();
                state.vertical_guides.clear();
            });
    });
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
        if (!layer || (layer->kind != LayerKind::composition && layer->kind != LayerKind::source))
            throw EditError(tr("请选择嵌套合成"));
        const auto id = layer->id;
        const auto revision = parent->project().revision();
        State child;
        child.name = layer->name;
        child.size = parent->project().state().size;
        child.layers = layer->children;
        const bool source = layer->kind == LayerKind::source;
        if (source) {
            child.size = layer->pixels.size();
            Layer pixels;
            pixels.name = layer->name;
            pixels.pixels = layer->pixels;
            child.layers = {pixels};
        }
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
            original->commit_transform();
            const auto result = original->project().state().layers;
            const auto source_pixels = source ? render(original->project().state()) : QImage{};
            parent->edit(tr("编辑嵌套合成"), [&](State& state) {
                auto* target = find_layer(state.layers, id);
                if (!target || target->locked)
                    throw EditError(tr("目标不可编辑"));
                if (source)
                    target->pixels = source_pixels;
                else
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
            c->set_path({});
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
        c->set_path({});
        c->update();
    });
    for (const auto& operation : {tr("左对齐画布"), tr("水平居中画布"), tr("右对齐"), tr("顶对齐"),
                                  tr("垂直居中画布"), tr("底对齐"), tr("水平分布"), tr("垂直分布")})
        add(layers, operation, {}, [this, operation] { align_layers(operation); });
    add(selection, tr("变换选区…"), {}, [this] {
        if (auto* c = canvas()) {
            c->begin_transform(true);
            c->setFocus();
        }
    });
    add(view, tr("设置菜单快捷键…"), "Ctrl+Alt+Shift+K", [this] { show_shortcuts(); });
    add(view, tr("恢复未保存文档…"), {}, [this] { offer_recovery(); });
    add(view, tr("网格间距…"), {}, [this] {
        if (auto* c = canvas()) {
            bool ok{};
            const double spacing = QInputDialog::getDouble(window_, tr("网格"), tr("间距（像素）"),
                                                           c->grid_spacing, 1, 10000, 1, &ok);
            if (ok) {
                c->grid_spacing = spacing;
                c->update();
            }
        }
    });
    // Move existing actions, preserving their callbacks and persisted shortcut names.
    const auto move_actions = [](QMenu* source, QMenu* destination, const QStringList& names) {
        for (const auto& name : names) {
            for (auto* action : source->actions()) {
                if (action->text() == name) {
                    source->removeAction(action);
                    destination->addAction(action);
                    break;
                }
            }
        }
    };
    const auto subgroup = [&](QMenu* menu, const QString& title, const QStringList& names) {
        auto* child = menu->addMenu(title);
        move_actions(menu, child, names);
        return child;
    };
    subgroup(file, tr("导出"), {tr("导出 PNG / JPEG…"), tr("批量导出与队列…")});
    auto* recovery_menu = subgroup(file, tr("恢复"), {tr("打开自动恢复…")});
    move_actions(view, recovery_menu, {tr("恢复未保存文档…")});
    // Reorder without QMenu::clear(), which would delete owned actions.
    const auto file_actions = file->actions();
    for (auto* action : file_actions)
        file->removeAction(action);
    for (const auto& name : {tr("新建图片…"), tr("打开…"), tr("最近打开"), QString{}, tr("置入…"),
                             tr("关闭图片"), QString{}, tr("保存工程"), tr("另存工程…"), tr("导出"),
                             tr("恢复"), QString{}, tr("退出")}) {
        if (name.isEmpty()) {
            file->addSeparator();
            continue;
        }
        for (auto* action : file_actions)
            if (!action->isSeparator() && action->text() == name)
                file->addAction(action);
    }
    subgroup(edit, tr("选择性粘贴"), {tr("原位粘贴"), tr("贴入选区")});
    auto* transform_menu = edit->addMenu(tr("变换"));
    move_actions(layers, transform_menu, {tr("数值变换…")});
    auto* comparison_menu = edit->addMenu(tr("比较快照"));
    move_actions(view, comparison_menu, {tr("创建比较快照…"), tr("查看比较快照…")});
    edit->addSeparator();
    move_actions(view, edit, {tr("设置菜单快捷键…")});
    subgroup(layers, tr("图层蒙版"), {tr("蒙版"), tr("反相蒙版"), tr("蒙版转选区")});
    subgroup(layers, tr("矢量蒙版"), {tr("路径转矢量蒙版")});
    subgroup(layers, tr("源与嵌套合成"),
             {tr("嵌入资源"), tr("替换 / 重新定位源…"), tr("编辑嵌套合成…")});
    subgroup(layers, tr("排列"), {tr("上移"), tr("下移")});
    subgroup(layers, tr("对齐"),
             {tr("左对齐画布"), tr("水平居中画布"), tr("右对齐"), tr("顶对齐"), tr("垂直居中画布"),
              tr("底对齐")});
    subgroup(layers, tr("分布"), {tr("水平分布"), tr("垂直分布")});
    subgroup(layers, tr("合并"), {tr("盖印可见副本"), tr("合并可见图层")});
    subgroup(selection, tr("修改"), {tr("羽化"), tr("扩张"), tr("收缩"), tr("平滑")});
    subgroup(selection, tr("范围"), {tr("亮度范围"), tr("线性渐变范围"), tr("径向渐变范围")});
    subgroup(selection, tr("路径"),
             {tr("保存路径"), tr("路径转选区"), tr("重命名路径…"), tr("隐藏当前路径"),
              tr("删除已存路径")});
    subgroup(view, tr("显示"), {tr("网格"), tr("显示参考线")});
    subgroup(view, tr("参考线"), {tr("添加参考线…"), tr("锁定参考线"), tr("清除参考线")});
    subgroup(view, tr("网格设置"), {tr("网格间距…")});
    auto* window_menu = window_->menus_->actions().at(2)->menu();
    auto* workspace_menu = window_menu->addMenu(tr("工作区"));
    move_actions(view, workspace_menu, {tr("保存工作区布局"), tr("恢复已存工作区")});
    move_actions(window_menu, workspace_menu, {tr("重置工作区")});
    const auto top = window_->menus_->actions();
    for (auto* action : top)
        window_->menus_->removeAction(action);
    for (auto* menu : {file, edit, layers, selection, view})
        window_->menus_->addMenu(menu);
    window_->menus_->addAction(top.at(2));
    for (auto* action : window_->findChildren<QAction*>()) {
        action->setProperty("defaultShortcut", action->shortcut().toString());
        const auto value = QSettings().value("shortcuts/" + action->text());
        if (value.isValid())
            action->setShortcut(QKeySequence(value.toString()));
    }
    auto* tools = new QToolBar(tr("工具"), window_);
    tools->setObjectName("editingTools");
    tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tools->setIconSize(QSize(19, 19));
    tools->setFixedWidth(66);
    tools->setMovable(false);
    window_->addToolBar(Qt::LeftToolBarArea, tools);
    const QStringList labels{tr("移动 V"),     tr("抓手 H"),    tr("矩形 M"),   tr("椭圆"),
                             tr("套索 L"),     tr("多边形"),    tr("钢笔 P"),   tr("画笔 B"),
                             tr("橡皮 E"),     tr("魔棒 W"),    tr("颜色范围"), tr("缩放 Z"),
                             tr("路径选择 A"), tr("历史画笔 Y")};
    tools->setStyleSheet(
        "QToolBar { spacing:0px; } QToolButton { width:24px; height:24px; padding:2px; border:1px "
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
    auto* tool_panel = new QWidget(tools);
    tool_panel->setObjectName("toolGrid");
    auto* tool_layout = new QGridLayout(tool_panel);
    tool_layout->setContentsMargins(3, 3, 3, 3);
    tool_layout->setHorizontalSpacing(0);
    tool_layout->setVerticalSpacing(2);
    tools->addWidget(tool_panel);
    struct ToolSlot {
        int index;
        int row;
        int column;
        const char* category;
    };
    const ToolSlot tool_slots[]{
        {0, 0, 0, "move"},        {2, 2, 0, "selection"},   {3, 2, 1, "selection"},
        {4, 3, 0, "selection"},   {5, 3, 1, "selection"},   {9, 4, 0, "selection"},
        {10, 4, 1, "selection"},  {7, 6, 0, "painting"},    {8, 6, 1, "painting"},
        {13, 7, 0, "painting"},   {6, 9, 0, "paths"},       {12, 9, 1, "paths"},
        {1, 11, 0, "navigation"}, {11, 11, 1, "navigation"}};
    for (const int row : {1, 5, 8, 10}) {
        auto* separator = new QWidget(tool_panel);
        separator->setFixedHeight(1);
        separator->setStyleSheet("background:#3b4147;");
        tool_layout->addWidget(separator, row, 0, 1, 2);
    }
    for (const auto& slot : tool_slots) {
        const int i = slot.index;
        const auto svg =
            ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 26 26'><g fill='none' "
             "stroke='#d5d9df' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'>" +
             (i == 11
                  ? QString(
                        R"(<circle cx="10" cy="10" r="7"/><path d="m15 15 7 7 M7 10h6 M10 7v6"/>)")
              : i == 12 ? QString(R"(<path d="M6 3v18l5-6 8-1Z"/>)")
              : i == 13 ? QString(R"(<path d="M5 9a7 7 0 1 1 1 10 M2 4v6h6 M10 18l10-10"/>)")
                        : drawings[i]) +
             "</g></svg>")
                .toUtf8();
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QSvgRenderer renderer(svg);
        renderer.render(&painter);
        painter.end();
        pixmap.setDevicePixelRatio(2);
        auto* action = new QAction(QIcon(pixmap), labels[i], tools);
        auto* button = new QToolButton(tool_panel);
        button->setObjectName(QString("toolButton_%1").arg(i));
        button->setProperty("category", slot.category);
        button->setAccessibleName(labels[i]);
        button->setIconSize(QSize(19, 19));
        button->setFixedSize(29, 29);
        button->setDefaultAction(action);
        tool_layout->addWidget(button, slot.row, slot.column);
        action->setCheckable(true);
        const QStringList keys{"V", "H", "M", "", "L", "", "P", "B", "E", "W", "", "Z", "A", "Y"};
        action->setShortcutContext(Qt::WidgetShortcut);
        action->setShortcut(QKeySequence(keys[i]));
        action->setProperty("defaultShortcut", keys[i]);
        const auto saved = QSettings().value("shortcuts/" + action->text());
        if (saved.isValid())
            action->setShortcut(QKeySequence(saved.toString()));
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
    const auto choose_color = [this](bool foreground) {
        if (auto* c = canvas()) {
            const auto selected = QColorDialog::getColor(
                foreground ? c->foreground : c->background, window_,
                foreground ? tr("前景色") : tr("背景色"), QColorDialog::DontUseNativeDialog);
            if (selected.isValid()) {
                (foreground ? c->foreground : c->background) = selected;
                refresh_tools();
            }
        }
    };
    auto* colors = new QWidget(tool_panel);
    colors->setObjectName("toolColors");
    colors->setFixedSize(58, 64);
    tool_layout->addWidget(colors, 13, 0, 1, 2);
    tool_layout->setRowMinimumHeight(12, 6);
    const auto color_button = [&](const char* name, const QString& label, const QRect& bounds) {
        auto* button = new QToolButton(colors);
        button->setObjectName(name);
        button->setAccessibleName(label);
        button->setToolTip(label);
        button->setGeometry(bounds);
        return button;
    };
    auto* background_button =
        color_button("backgroundSwatch", tr("背景色：点击选择"), QRect(23, 33, 31, 29));
    auto* foreground_button =
        color_button("foregroundSwatch", tr("前景色：点击选择"), QRect(5, 18, 31, 29));
    auto* defaults =
        color_button("resetColors", tr("默认前景色和背景色（D）"), QRect(0, 0, 22, 18));
    auto* swap = color_button("swapColors", tr("交换前景色和背景色（X）"), QRect(36, 0, 22, 21));
    const auto color_icon = [](const QByteArray& svg) {
        QPixmap pixmap(40, 40);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QSvgRenderer renderer(svg);
        renderer.render(&painter);
        painter.end();
        pixmap.setDevicePixelRatio(2);
        return QIcon(pixmap);
    };
    defaults->setIcon(color_icon(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20"><rect x="8" y="8" width="10" height="10" fill="white" stroke="#959ba2"/><rect x="2" y="2" width="10" height="10" fill="black" stroke="#d5d9df"/></svg>)"));
    swap->setIcon(color_icon(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20"><path d="M3 5h10v10 M10 12l3 3 3-3 M6 2 3 5l3 3" fill="none" stroke="#d5d9df" stroke-width="1.5"/></svg>)"));
    connect(foreground_button, &QToolButton::clicked, this, [choose_color] { choose_color(true); });
    connect(background_button, &QToolButton::clicked, this,
            [choose_color] { choose_color(false); });
    connect(defaults, &QToolButton::clicked, this, [this] {
        if (auto* c = canvas()) {
            c->foreground = Qt::black;
            c->background = Qt::white;
            refresh_tools();
            c->setFocus();
        }
    });
    connect(swap, &QToolButton::clicked, this, [this] {
        if (auto* c = canvas()) {
            std::swap(c->foreground, c->background);
            refresh_tools();
            c->setFocus();
        }
    });
    auto* options = window_->findChild<QToolBar*>("viewToolbar");
    for (int mode = 0; mode < 2; ++mode) {
        auto* button = new QToolButton(options);
        button->setObjectName(mode ? "zoomOutMode" : "zoomInMode");
        button->setStyleSheet(
            "QToolButton:checked { background:#3a4148; border:1px solid #737b85; }");
        button->setText(mode ? tr("缩小 −") : tr("放大 +"));
        button->setToolTip(tr("点击选择缩放方向；Alt 临时反向；空格拖动平移"));
        button->setCheckable(true);
        options->addWidget(button)->setObjectName("zoomOptionsMode");
        connect(button, &QToolButton::clicked, this, [this, mode] {
            if (auto* c = canvas()) {
                c->zoom_out = mode != 0;
                refresh_tools();
                c->setFocus();
            }
        });
    }
    auto* zoom_value = new QDoubleSpinBox(options);
    zoom_value->setObjectName("zoomPercent");
    zoom_value->setRange(.1, 3200);
    zoom_value->setDecimals(1);
    zoom_value->setSuffix(" %");
    zoom_value->setKeyboardTracking(false);
    zoom_value->setToolTip(tr("显示缩放比例；不改变图像尺寸"));
    options->addWidget(zoom_value)->setObjectName("zoomOptionsPercent");
    connect(zoom_value, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (auto* c = canvas())
            c->set_zoom(value / 100);
    });
    auto* actual_size = options->addAction(tr("100%"));
    actual_size->setObjectName("zoomOptionsActual");
    connect(actual_size, &QAction::triggered, this, [this] {
        if (auto* c = canvas())
            c->set_zoom(1);
    });
    for (auto* menu_action : window_->findChild<QMenuBar*>()->actions()) {
        if (!menu_action->text().startsWith(tr("编辑")))
            continue;
        auto* preferences = menu_action->menu()->addMenu(tr("首选项"));
        auto* navigation = preferences->addAction(tr("工具与画面操控…"));
        connect(navigation, &QAction::triggered, this, [this] {
            QDialog dialog(window_);
            dialog.setObjectName("navigationPreferences");
            dialog.setWindowTitle(tr("首选项 · 工具与画面操控"));
            QFormLayout form(&dialog);
            QCheckBox wheel(tr("使用滚轮缩放")), center(tr("将缩放点击位置置于中心"));
            wheel.setObjectName("wheelZoomPreference");
            center.setObjectName("centerZoomPreference");
            wheel.setChecked(QSettings().value("navigation/wheelZoom", false).toBool());
            center.setChecked(QSettings().value("navigation/centerClicked", false).toBool());
            form.addRow(&wheel);
            form.addRow(&center);
            form.addRow(
                new QLabel(tr("关闭滚轮缩放时：滚轮平移，Alt+滚轮缩放。\n开启时：滚轮缩放，Alt+"
                              "滚轮平移。\n空格拖动平移；Ctrl+0适合窗口，Ctrl+1显示100%。"),
                           &dialog));
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            form.addRow(&buttons);
            connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() == QDialog::Accepted) {
                QSettings settings;
                settings.setValue("navigation/wheelZoom", wheel.isChecked());
                settings.setValue("navigation/centerClicked", center.isChecked());
            }
        });
        break;
    }
    auto* range_operations = new QWidget(options);
    range_operations->setObjectName("selectionModeButtons");
    auto* range_layout = new QHBoxLayout(range_operations);
    range_layout->setContentsMargins(0, 0, 0, 0);
    range_layout->setSpacing(2);
    auto* range_group = new QButtonGroup(range_operations);
    range_group->setExclusive(true);
    const QStringList range_names{tr("新选区"), tr("添加到选区（Shift）"), tr("从选区减去（Alt）"),
                                  tr("与选区交叉（Shift+Alt）")};
    const std::array<QByteArray, 4> range_icons{
        "<rect x='5' y='5' width='14' height='14' fill='#b8b8b8'/>",
        "<path d='M3 3h12v6h6v12H9v-6H3Z' fill='#b8b8b8'/>",
        "<path d='M3 3h12v6H9v6H3Z' fill='#b8b8b8'/><rect x='9' y='9' width='12' height='12' "
        "fill='none' stroke='#b8b8b8'/>",
        "<rect x='3' y='3' width='12' height='12' fill='none' stroke='#b8b8b8'/><rect x='9' y='9' "
        "width='12' height='12' fill='none' stroke='#b8b8b8'/><rect x='9' y='9' width='6' "
        "height='6' fill='#b8b8b8'/>"};
    for (int index = 0; index < 4; ++index) {
        auto* button = new QToolButton(range_operations);
        button->setObjectName(QString("selectionMode%1").arg(index));
        button->setToolTip(range_names[index]);
        button->setAccessibleName(range_names[index]);
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setFixedSize(28, 28);
        button->setIconSize(QSize(20, 20));
        button->setStyleSheet("QToolButton:checked {background:#3a4148; border:1px solid #666a70; "
                              "padding:0;} QToolButton {padding:0;}");
        QPixmap icon(40, 40);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        QSvgRenderer renderer(
            QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>") +
            range_icons[index] + "</svg>");
        renderer.render(&painter);
        painter.end();
        icon.setDevicePixelRatio(2);
        button->setIcon(QIcon(icon));
        range_group->addButton(button, index);
        range_layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, index] {
            if (auto* c = canvas()) {
                c->range_operation = static_cast<RangeOp>(index);
                c->setFocus();
            }
        });
    }
    options->addWidget(range_operations)->setObjectName("selectionOptions");
    auto* radius = new QDoubleSpinBox(options);
    radius->setObjectName("brushDiameter");
    radius->setRange(2, 1000);
    radius->setValue(40);
    radius->setPrefix(tr("大小 "));
    radius->setSuffix(tr(" 像素"));
    radius->setDecimals(0);
    radius->setAccessibleName(tr("笔刷直径"));
    options->addWidget(radius)->setObjectName("brushOptions");
    connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (auto* c = canvas())
            c->brush_radius = value / 2;
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
        if (auto* c = canvas()) {
            if (c->cropping())
                c->commit_crop();
            else
                c->commit_transform();
        }
    });
    auto* cancel = options->addAction(tr("取消 ×"));
    cancel->setObjectName("transformCancel");
    connect(cancel, &QAction::triggered, this, [this] {
        if (auto* c = canvas()) {
            c->cancel_crop();
            c->cancel_transform();
        }
    });
    auto* color = options->addAction(tr("前景色"));
    color->setObjectName("brushColor");
    connect(color, &QAction::triggered, this, [choose_color] { choose_color(true); });
    const auto number = [&](const char* name, const QString& label, double minimum, double maximum,
                            double initial, const std::function<void(CanvasView&, double)>& apply) {
        auto* field = new QDoubleSpinBox(options);
        field->setObjectName(name);
        field->setRange(minimum, maximum);
        field->setValue(initial);
        field->setPrefix(label);
        field->setMaximumWidth(125);
        options->addWidget(field)->setObjectName(name);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, apply](double value) {
            if (auto* c = canvas())
                apply(*c, value);
        });
    };
    number("brushHardness", tr("硬度 "), 0, 100, 80,
           [](CanvasView& c, double v) { c.brush_hardness = v / 100; });
    number("brushOpacity", tr("不透明度 "), 0, 100, 100,
           [](CanvasView& c, double v) { c.brush_opacity = v / 100; });
    number("brushFlow", tr("流量 "), 1, 100, 100,
           [](CanvasView& c, double v) { c.brush_flow = v / 100; });
    number("rangeTolerance", tr("容差 "), 0, 255, 32,
           [](CanvasView& c, double v) { c.color_tolerance = int(v); });
    number("move_hitTolerance", tr("命中容差 "), 0, 10, 3,
           [](CanvasView& c, double v) { c.hit_tolerance = v; });
    for (const auto& name :
         {QString("rangeAntialias"), QString("rangeContiguous"), QString("brushPressure")}) {
        auto* check = new QCheckBox(name == "rangeAntialias"    ? tr("抗锯齿")
                                    : name == "rangeContiguous" ? tr("连续")
                                                                : tr("压感"),
                                    options);
        check->setObjectName(name);
        check->setChecked(true);
        options->addWidget(check)->setObjectName(name);
        connect(check, &QCheckBox::toggled, this, [this, name](bool value) {
            if (auto* c = canvas()) {
                if (name == "rangeAntialias")
                    c->antialias = value;
                else if (name == "rangeContiguous")
                    c->contiguous = value;
                else
                    c->pressure_enabled = value;
            }
        });
    }
    auto* reference = new QComboBox(options);
    reference->addItems({tr("对齐画布"), tr("对齐选中图层")});
    options->addWidget(reference)->setObjectName("move_alignReference");
    connect(reference, &QComboBox::currentIndexChanged, this,
            [this](int value) { align_to_canvas_ = value == 0; });
    for (const auto& operation :
         {tr("左对齐画布"), tr("水平居中画布"), tr("右对齐"), tr("顶对齐"), tr("垂直居中画布"),
          tr("底对齐"), tr("水平分布"), tr("垂直分布")}) {
        const int index = operation == tr("左对齐画布")     ? 0
                          : operation == tr("水平居中画布") ? 1
                          : operation == tr("右对齐")       ? 2
                          : operation == tr("顶对齐")       ? 3
                          : operation == tr("垂直居中画布") ? 4
                          : operation == tr("底对齐")       ? 5
                          : operation == tr("水平分布")     ? 6
                                                            : 7;
        QPixmap icon(20, 20);
        icon.fill(Qt::transparent);
        {
            QPainter p(&icon);
            p.setPen(QPen(QColor("#cbd0d5"), 1.3));
            if (index < 3) {
                const int x = 3 + index * 7;
                p.drawLine(x, 2, x, 18);
                p.drawLine(5, 7, 15, 7);
                p.drawLine(7, 13, 13, 13);
            } else if (index < 6) {
                const int y = 3 + (index - 3) * 7;
                p.drawLine(2, y, 18, y);
                p.drawLine(7, 5, 7, 15);
                p.drawLine(13, 7, 13, 13);
            } else if (index == 6) {
                for (int x : {3, 10, 17})
                    p.drawLine(x, 5, x, 15);
            } else {
                for (int y : {3, 10, 17})
                    p.drawLine(5, y, 15, y);
            }
        }
        auto* action = options->addAction(QIcon(icon), operation);
        action->setToolTip(operation);
        action->setObjectName("move_alignment");
        if (auto* button = qobject_cast<QToolButton*>(options->widgetForAction(action))) {
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setFixedSize(26, 26);
            button->setIconSize(QSize(18, 18));
            button->setStyleSheet("QToolButton { padding:3px; border:none; } QToolButton:hover { "
                                  "background:#354451; }");
        }
        connect(action, &QAction::triggered, this,
                [this, operation] { guarded([&] { align_layers(operation); }); });
    }
    auto* warp = new QCheckBox(tr("网格变形"), options);
    warp->setObjectName("warpMode");
    options->addWidget(warp)->setObjectName("move_warpMode");
    connect(warp, &QCheckBox::toggled, this, [this](bool value) {
        if (auto* c = canvas()) {
            c->warp_mode = value;
            if (value)
                c->begin_transform();
            c->update();
        }
    });
    configure_photo_tools();
    refresh_tools();
}
void EditorController::refresh_tools() {
    auto* c = canvas();
    const auto tool = c ? c->tool() : CanvasTool::move;
    if (auto* quick = window_->findChild<QAction*>("quickMaskAction")) {
        quick->setEnabled(c != nullptr);
        quick->setChecked(c && c->quick_mask);
    }
    for (const auto& entry :
         {std::pair{"foregroundSwatch", c ? c->foreground : QColor(Qt::black)},
          std::pair{"backgroundSwatch", c ? c->background : QColor(Qt::white)}}) {
        if (auto* button = window_->findChild<QToolButton*>(entry.first)) {
            button->setEnabled(c != nullptr);
            button->setStyleSheet(QString("QToolButton { background:%1; border:2px solid #b8bec6; "
                                          "border-radius:0; padding:0; } "
                                          "QToolButton:hover { border-color:#529ad3; }")
                                      .arg(entry.second.name()));
            button->setToolTip(button->accessibleName() + " " + entry.second.name());
        }
    }
    for (const auto* name : {"resetColors", "swapColors"})
        if (auto* button = window_->findChild<QToolButton*>(name))
            button->setEnabled(c != nullptr);
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
        if (name.startsWith("zoomOptions"))
            action->setVisible(c && (tool == CanvasTool::zoom || tool == CanvasTool::hand));
        if (name == "selectionOptions")
            action->setVisible(c && (tool == CanvasTool::rectangle || tool == CanvasTool::ellipse ||
                                     tool == CanvasTool::lasso || tool == CanvasTool::polygon ||
                                     tool == CanvasTool::wand || tool == CanvasTool::color_range));
        if (name.startsWith("brush"))
            action->setVisible(c && c->painting_tool());
        if (name == "photo_sample")
            action->setVisible(c && c->retouch_tool());
        if (name == "photo_aligned")
            action->setVisible(c && (tool == CanvasTool::clone || tool == CanvasTool::heal));
        if (name == "photo_allLayers")
            action->setVisible(c && tool == CanvasTool::bucket);
        if (name.startsWith("range"))
            action->setVisible(c &&
                               (tool == CanvasTool::wand || tool == CanvasTool::color_range ||
                                name == "rangeAntialias" &&
                                    (tool == CanvasTool::rectangle || tool == CanvasTool::ellipse ||
                                     tool == CanvasTool::lasso || tool == CanvasTool::polygon)));
        if (tool == CanvasTool::bucket &&
            (name == "brushOpacity" || name == "rangeTolerance" || name == "rangeContiguous"))
            action->setVisible(c != nullptr);
        if (name == "transformApply" || name == "transformCancel")
            action->setVisible(c && (c->transforming() || tool == CanvasTool::crop));
    }
    if (c) {
        if (auto* value = options->findChild<QDoubleSpinBox*>("zoomPercent")) {
            const QSignalBlocker blocker(value);
            value->setValue(c->zoom() * 100);
        }
        for (int mode = 0; mode < 2; ++mode)
            if (auto* button =
                    options->findChild<QToolButton*>(mode ? "zoomOutMode" : "zoomInMode")) {
                const QSignalBlocker blocker(button);
                button->setChecked(c->zoom_out == (mode != 0));
            }
        for (int index = 0; index < 4; ++index)
            if (auto* button =
                    options->findChild<QToolButton*>(QString("selectionMode%1").arg(index))) {
                const QSignalBlocker blocker(button);
                button->setChecked(index == int(c->range_operation));
            }
        for (const auto& entry : {std::pair{"brushHardness", c->brush_hardness * 100},
                                  std::pair{"brushOpacity", c->brush_opacity * 100},
                                  std::pair{"brushFlow", c->brush_flow * 100},
                                  std::pair{"rangeTolerance", double(c->color_tolerance)},
                                  std::pair{"move_hitTolerance", c->hit_tolerance}})
            if (auto* spin = options->findChild<QDoubleSpinBox*>(entry.first)) {
                QSignalBlocker blocker(spin);
                spin->setValue(entry.second);
            }
        for (const auto& entry :
             {std::pair{"rangeAntialias", c->antialias},
              std::pair{"rangeContiguous", c->contiguous},
              std::pair{"brushPressure", c->pressure_enabled}, std::pair{"warpMode", c->warp_mode},
              std::pair{"photoAligned", c->clone_aligned},
              std::pair{"photoAllLayers", c->sample_all_layers}})
            if (auto* check = options->findChild<QCheckBox*>(entry.first)) {
                QSignalBlocker blocker(check);
                check->setChecked(entry.second);
            }
        if (auto* scope = options->findChild<QComboBox*>("photoSample")) {
            QSignalBlocker blocker(scope);
            scope->setCurrentIndex(int(c->photo_brush.sample));
        }
    }
    if (c)
        if (auto* radius = options->findChild<QDoubleSpinBox*>("brushDiameter")) {
            QSignalBlocker blocker(radius);
            radius->setValue(c->brush_radius * 2);
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
    if (object == layers_->viewport() && event->type() == QEvent::MouseButtonPress && canvas()) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        layer_click_modifiers_ = mouse->modifiers();
        layers_->viewport()->setProperty("clickPosition", mouse->position().toPoint());
        auto* item = layers_->itemAt(mouse->position().toPoint());
        if (item) {
            const auto bounds = layers_->visualItemRect(item);
            const auto id = item->data(0, Qt::UserRole).toUuid();
            const double x = mouse->position().x() - bounds.left();
            if (x >= 0 && x < 20) {
                guarded([&] {
                    canvas()->edit(tr("图层可见性"), [&](State& state) {
                        auto* layer = find_layer(state.layers, id);
                        layer->visible = !layer->visible;
                    });
                });
                return true;
            }
            if (x >= 24 && x < 62 && mouse->modifiers().testFlag(Qt::ControlModifier)) {
                guarded([&] {
                    canvas()->edit(tr("载入图层透明度"), [&](State& state) {
                        const auto* layer = find_layer(state.layers, id);
                        State isolated = state;
                        Layer copy = *layer;
                        copy.transform *= parent_matrix(state, id);
                        copy.mask_transform *= parent_matrix(state, id);
                        isolated.layers = {copy};
                        const auto pixels = render(isolated);
                        QImage range(state.size, QImage::Format_Grayscale8);
                        for (int y = 0; y < range.height(); ++y) {
                            const auto* row =
                                reinterpret_cast<const float*>(pixels.constScanLine(y));
                            for (int xx = 0; xx < range.width(); ++xx)
                                range.scanLine(y)[xx] =
                                    uchar(std::clamp(row[xx * 4 + 3] * 255.F, 0.F, 255.F));
                        }
                        combine_range(state, range,
                                      mouse->modifiers().testFlag(Qt::ShiftModifier) ? RangeOp::add
                                      : mouse->modifiers().testFlag(Qt::AltModifier)
                                          ? RangeOp::subtract
                                          : RangeOp::replace);
                    });
                });
                return true;
            }
        }
    }
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
    if (object == layers_->viewport() &&
        (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (drag->mimeData()->data("application/x-huelith-layer-tree") ==
            layers_->property("dragToken").toByteArray()) {
            if (event->type() == QEvent::DragEnter || drag->source() != layers_) {
                drag->acceptProposedAction();
                return true;
            }
        }
    }
    if (object == layers_->viewport() && event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        if (drop->mimeData()->data("application/x-huelith-layer-tree") !=
            layers_->property("dragToken").toByteArray())
            return false;
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
                for (const auto& id : root_targets(state, ids))
                    move_layer(state, id, parent, position);
            });
        });
        drop->acceptProposedAction();
        return true;
    }
    return QObject::eventFilter(object, event);
}
} // namespace huelith::desktop
