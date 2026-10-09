#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include "raw_dialog.h"
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QMenuBar>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QToolBar>
#include <QToolButton>

namespace huelith::desktop {
void EditorController::configure_photo_tools() {
    auto* toolbar = window_->findChild<QToolBar*>("editingTools");
    auto* panel = window_->findChild<QWidget*>("toolGrid");
    auto* group = window_->findChild<QActionGroup*>("toolActions");
    auto* layout = qobject_cast<QGridLayout*>(panel->layout());
    struct Tool {
        CanvasTool tool;
        const char* label;
        const char* shortcut;
        const char* drawing;
    };
    const Tool tools[]{
        {CanvasTool::bucket, "油漆桶 G", "G",
         "<path d='m3 11 9-9 9 9-9 9Z M5 4l8 8 M20 13q-5 7 0 7t0-7Z'/>"},
        {CanvasTool::eyedropper, "吸管 I", "I", "<path d='m3 21 2-6L17 3l4 4L9 19Z M14 4l6 6'/>"},
        {CanvasTool::clone, "仿制图章 S", "S",
         "<path d='M4 21h16v-5H4Z M8 16l2-6q-6-9 2-9t2 9l2 6'/>"},
        {CanvasTool::heal, "修复画笔 Shift+J", "Shift+J",
         "<path d='M4 19q-3-3 0-6L13 4q3-3 6 0t0 6l-9 9q-3 3-6 0Z M8 9l7 7 M10 12h2'/>"},
        {CanvasTool::spot, "污点修复 J", "J",
         "<path d='m3 17 12-12 5 5L8 22Z M8 12l5 5 M4 2v6 M1 5h6 M19 1v5 M17 3h4'/>"},
        {CanvasTool::blur, "模糊", "",
         "<path d='M12 2C10 8 4 11 4 16a8 8 0 0 0 16 0C20 11 14 8 12 2Z'/>"},
        {CanvasTool::sharpen, "锐化", "", "<path d='m12 3 9 18H3Z'/>"},
        {CanvasTool::dodge, "减淡 O", "O",
         "<ellipse cx='15' cy='8' rx='6' ry='5'/><path d='m10 12-7 10'/>"},
        {CanvasTool::burn, "加深 Shift+O", "Shift+O",
         "<path d='M6 20V9l3-3 2 3 3-5 3 2v5l4 2-4 8Z M8 14h8'/>"},
        {CanvasTool::sponge, "海绵", "",
         "<path d='M3 7q4-5 8-2t10 0v14q-5 4-9 0t-9 2Z M7 10h1 M15 9h1 M12 15h1 M6 17h1'/>"},
        {CanvasTool::crop, "裁剪 C", "C", "<path d='M6 1v17h17 M1 6h17v17 M9 3v12h12'/>"}};
    for (const auto& tool : tools) {
        const auto svg = QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 26 "
                                    "26'><g fill='none' stroke='#d5d9df' stroke-width='1.5' "
                                    "stroke-linecap='round' stroke-linejoin='round'>") +
                         tool.drawing + "</g></svg>";
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QSvgRenderer renderer(svg);
        renderer.render(&painter);
        painter.end();
        pixmap.setDevicePixelRatio(2);
        auto* action = new QAction(QIcon(pixmap), QString::fromUtf8(tool.label), toolbar);
        action->setCheckable(true);
        action->setData(int(tool.tool));
        action->setShortcutContext(Qt::WidgetShortcut);
        action->setProperty("defaultShortcut", tool.shortcut);
        action->setShortcut(QKeySequence(
            QSettings().value("shortcuts/" + action->text(), tool.shortcut).toString()));
        group->addAction(action);
        auto* button = new QToolButton(panel);
        button->setObjectName(QString("toolButton_%1").arg(int(tool.tool)));
        button->setDefaultAction(action);
        button->setAccessibleName(action->text());
        button->setIconSize(QSize(19, 19));
        button->setFixedSize(29, 29);
        connect(action, &QAction::triggered, this, [this, selected = tool.tool] {
            if (auto* c = canvas()) {
                c->set_tool(selected);
                c->setFocus();
            }
        });
    }
    // Reuse existing tool buttons and actions, replacing only the palette layout.
    while (auto* item = layout->takeAt(0)) {
        if (auto* widget = item->widget(); widget && widget->objectName().isEmpty())
            widget->deleteLater();
        delete item;
    }
    int row = 0;
    const QList<QList<int>> categories{{0, 24},      {2, 3, 4, 5, 9, 10},  {7, 8, 14, 15, 13},
                                       {16, 17, 18}, {19, 20, 21, 22, 23}, {6, 12},
                                       {1, 11}};
    for (const auto& category : categories) {
        int column = 0;
        for (const int index : category) {
            auto* button = panel->findChild<QToolButton*>(QString("toolButton_%1").arg(index));
            layout->addWidget(button, row, column);
            if (++column == 2) {
                column = 0;
                ++row;
            }
        }
        if (column)
            ++row;
        auto* separator = new QWidget(panel);
        separator->setFixedHeight(1);
        separator->setStyleSheet("background:#3b4147;");
        layout->addWidget(separator, row++, 0, 1, 2);
    }
    layout->addWidget(panel->findChild<QWidget*>("toolColors"), row, 0, 1, 2);
    auto* quick = new QAction(tr("快速蒙版 Q"), window_);
    quick->setObjectName("quickMaskAction");
    quick->setCheckable(true);
    quick->setShortcut(QKeySequence("Q"));
    quick->setProperty("defaultShortcut", "Q");
    window_->addAction(quick);
    auto* quick_button = new QToolButton(panel);
    quick_button->setObjectName("quickMaskButton");
    quick_button->setDefaultAction(quick);
    quick_button->setText("▣");
    quick_button->setFixedSize(29, 29);
    layout->addWidget(quick_button, row + 1, 0);
    connect(quick, &QAction::triggered, this, [this] {
        if (auto* c = canvas()) {
            c->toggle_quick_mask();
            c->setFocus();
        }
    });
    auto* screen_action = new QAction(tr("切换屏幕模式 F"), window_);
    screen_action->setShortcut(QKeySequence("F"));
    screen_action->setProperty("defaultShortcut", "F");
    window_->addAction(screen_action);
    auto* screen_button = new QToolButton(panel);
    screen_button->setObjectName("screenModeButton");
    screen_button->setDefaultAction(screen_action);
    screen_button->setText("▱");
    screen_button->setFixedSize(29, 29);
    layout->addWidget(screen_button, row + 1, 1);
    struct ScreenState {
        int mode{};
        QByteArray layout;
        bool maximized{};
    };
    const auto screen_state = std::make_shared<ScreenState>();
    connect(screen_action, &QAction::triggered, this, [this, screen_state] {
        auto& state = *screen_state;
        if (state.mode == 0) {
            state.layout = window_->saveState(1);
            state.maximized = window_->isMaximized();
            window_->showFullScreen();
            state.mode = 1;
        } else if (state.mode == 1) {
            window_->menuWidget()->hide();
            window_->statusBar()->hide();
            for (auto* bar : window_->findChildren<QToolBar*>())
                bar->hide();
            for (auto* dock : window_->findChildren<QDockWidget*>())
                dock->hide();
            state.mode = 2;
        } else {
            window_->menuWidget()->show();
            window_->statusBar()->show();
            window_->restoreState(state.layout, 1);
            if (state.maximized)
                window_->showMaximized();
            else
                window_->showNormal();
            state.mode = 0;
        }
        window_->setProperty("screenMode", state.mode);
        if (auto* c = canvas())
            c->setFocus();
    });
    for (auto* menu_action : window_->menus_->actions()) {
        if (menu_action->text().startsWith(tr("选择")))
            menu_action->menu()->addAction(quick);
        if (menu_action->text().startsWith(tr("视图")))
            menu_action->menu()->addAction(screen_action);
    }
    auto* options = window_->findChild<QToolBar*>("viewToolbar");
    auto* scope = new QComboBox(options);
    scope->setObjectName("photoSample");
    scope->addItems({tr("取样：当前图层"), tr("取样：当前及下方"), tr("取样：所有图层")});
    scope->setCurrentIndex(1);
    options->addWidget(scope)->setObjectName("photo_sample");
    connect(scope, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (auto* c = canvas())
            c->photo_brush.sample = static_cast<editing::SampleLayers>(index);
    });
    auto* aligned = new QCheckBox(tr("对齐取样"), options);
    aligned->setObjectName("photoAligned");
    aligned->setChecked(true);
    options->addWidget(aligned)->setObjectName("photo_aligned");
    connect(aligned, &QCheckBox::toggled, this, [this](bool value) {
        if (auto* c = canvas())
            c->clone_aligned = value;
    });
    auto* all = new QCheckBox(tr("所有图层"), options);
    all->setObjectName("photoAllLayers");
    options->addWidget(all)->setObjectName("photo_allLayers");
    connect(all, &QCheckBox::toggled, this, [this](bool value) {
        if (auto* c = canvas())
            c->sample_all_layers = value;
    });
    auto* image_menu = new QMenu(tr("图像(&I)"), window_);
    window_->menus_->insertMenu(window_->menus_->actions().at(2), image_menu);
    auto* adjustments = image_menu->addMenu(tr("调整"));
    const QStringList tone_names{tr("曝光与光线…"), tr("色彩平衡…"), tr("色阶…"), tr("曲线…"),
                                 tr("色相/饱和度…")};
    for (int i = 0; i < tone_names.size(); ++i) {
        auto* action = adjustments->addAction(tone_names[i]);
        if (i == 2)
            action->setShortcut(QKeySequence("Ctrl+L"));
        if (i == 3)
            action->setShortcut(QKeySequence("Ctrl+M"));
        if (i == 4)
            action->setShortcut(QKeySequence("Ctrl+U"));
        connect(action, &QAction::triggered, this, [this, i] { guarded([&] { show_tone(i); }); });
    }
    auto* raw = image_menu->addAction(tr("编辑 RAW…"));
    connect(raw, &QAction::triggered, this, [this] {
        guarded([&] {
            auto* c = canvas();
            if (!c)
                return;
            const auto* layer = editing::find_layer(c->project().state().layers, c->active_layer);
            if (!layer || layer->raw_source.isEmpty())
                throw editing::EditError(tr("当前图层没有 RAW 源，请先打开 RAW 文件"));
            if (editing::layer_locked(c->project().state().layers, layer->id))
                throw editing::EditError(tr("当前图层已锁定"));
            const auto id = layer->id;
            RawDialog dialog({*layer}, window_);
            if (dialog.exec() == QDialog::Accepted)
                c->edit(tr("RAW 显影"), [&](editing::State& state) {
                    *editing::find_layer(state.layers, id) = dialog.results().front();
                });
        });
    });
    auto* crop = image_menu->addAction(tr("裁剪到选区"));
    connect(crop, &QAction::triggered, this, [this] {
        guarded([&] {
            if (auto* c = canvas()) {
                const auto& range = c->project().state().selection;
                if (range.isNull())
                    throw editing::EditError(tr("请先建立选区，或使用裁剪工具 C"));
                QRect bounds;
                for (int y = 0; y < range.height(); ++y)
                    for (int x = 0; x < range.width(); ++x)
                        if (range.constScanLine(y)[x])
                            bounds = bounds.united(QRect(x, y, 1, 1));
                c->edit(tr("裁剪"),
                        [&](editing::State& state) { editing::crop_canvas(state, bounds); });
                c->fit_to_view();
            }
        });
    });
    auto* edit_menu = window_->menus_->actions().at(1)->menu();
    auto* fill = edit_menu->addAction(tr("填充…"));
    fill->setShortcut(QKeySequence("Shift+F5"));
    connect(fill, &QAction::triggered, this, [this] {
        guarded([&] {
            auto* c = canvas();
            if (!c)
                return;
            QDialog dialog(window_);
            dialog.setWindowTitle(tr("填充"));
            QFormLayout form(&dialog);
            QComboBox content;
            content.addItems({tr("前景色"), tr("背景色"), tr("黑色"), tr("白色")});
            form.addRow(tr("内容"), &content);
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            form.addRow(&buttons);
            connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QColor colors[]{c->foreground, c->background, Qt::black, Qt::white};
            c->edit(tr("填充"), [&](editing::State& state) {
                editing::fill_color(state, c->active_layer, QPoint(),
                                    colors[content.currentIndex()], 255, false, false,
                                    c->brush_opacity);
            });
        });
    });
}
} // namespace huelith::desktop
