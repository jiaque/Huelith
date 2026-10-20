#include "main_window.h"
#include "application_icon.h"
#include "canvas_preferences.h"
#include "canvas_view.h"
#include "editor_controller.h"
#include "new_document_dialog.h"
#include "window_header.h"
#include "window_icons.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDialog>
#include <QPointer>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QFile>
#include <QLabel>
#include <QLibraryInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QProxyStyle>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTranslator>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#endif

namespace huelith::desktop {
namespace {
class EditorStyle final : public QProxyStyle {
  public:
    EditorStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    QIcon standardIcon(StandardPixmap icon, const QStyleOption* option,
                       const QWidget* widget) const override {
        if (icon == SP_TitleBarCloseButton || icon == SP_DockWidgetCloseButton ||
            icon == SP_DialogCloseButton) {
            return window_icon(WindowIcon::close);
        }
        return QProxyStyle::standardIcon(icon, option, widget);
    }
};
} // namespace

void apply_dark_theme() {
    if (!qApp->findChild<QTranslator*>("qtZhCn")) {
        auto* translations = new QTranslator(qApp);
        translations->setObjectName("qtZhCn");
        if (translations->load("qtbase_zh_CN",
                               QCoreApplication::applicationDirPath() + "/translations") ||
            translations->load("qtbase_zh_CN", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QCoreApplication::installTranslator(translations);
    }
    QApplication::setStyle(new EditorStyle); // QApplication takes ownership.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#252a2e"));
    palette.setColor(QPalette::WindowText, QColor(225, 225, 225));
    palette.setColor(QPalette::Base, QColor("#202428"));
    palette.setColor(QPalette::AlternateBase, QColor(50, 50, 50));
    palette.setColor(QPalette::Text, QColor(225, 225, 225));
    palette.setColor(QPalette::Button, QColor("#2b3035"));
    palette.setColor(QPalette::ButtonText, QColor(225, 225, 225));
    palette.setColor(QPalette::Highlight, QColor(55, 117, 190));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(125, 125, 125));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(125, 125, 125));
    QApplication::setPalette(palette);
    auto stylesheet = QStringLiteral(R"(
        QWidget { color: #e2e5e8; font-family: 'Microsoft YaHei UI'; font-size: 13px; }
        QMainWindow, QDialog { background: #252a2e; }
        QWidget#windowHeader { background: #252a2e; border-bottom: 1px solid #383e44; }
        QLabel#appTitle { font-size: 15px; font-weight: 500; }
        QLabel#welcomeTitle { font-size: 28px; }
        QMenuBar { background: transparent; border: none; }
        QMenuBar::item { padding: 8px 13px; background: transparent; }
        QMenuBar::item:selected, QMenu::item:selected { background: #354451; }
        QMenu { background: #292e33; border: 1px solid #41474d; padding: 4px; }
        QMenu::item { padding: 7px 28px; }
        QPushButton { background: #2b3035; border: 1px solid #464d54; border-radius: 3px; padding: 5px 12px; }
        QPushButton:hover { background: #363e46; }
        QPushButton:pressed { background: #254b70; }
        QPushButton:disabled { color: #78818a; }
        QWidget#windowHeader QPushButton { border: none; border-radius: 0; padding: 0; font-family: 'Segoe UI'; font-size: 19px; background: transparent; }
        QWidget#windowHeader QPushButton:hover { background: #3b4249; }
        QWidget#windowHeader QPushButton#closeWindow:hover { background: #c42b32; }
        QToolBar { background: #292e33; border: none; border-bottom: 1px solid #383e44; spacing: 8px; padding: 0px; }
        QToolButton { background: transparent; border: none; padding: 4px 10px; }
        QToolButton:hover { background: #354451; }
        QDockWidget { border: 1px solid #383e44; }
        QDockWidget::title { background: #252a2e; border-bottom: 1px solid #383e44; padding: 9px 12px; }
        QMainWindow::separator { background: #1c2024; width: 3px; height: 3px; }
        QMainWindow::separator:hover { background: #2684ff; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { background: #22272b; padding: 6px 18px; border-right: 1px solid #383e44; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { background: #2b3035; border-bottom: 2px solid #2684ff; }
        QTabBar::tab:hover { background: #30373d; }
        QStatusBar { background: #252a2e; border-top: 1px solid #383e44; min-height: 24px; }
        QStatusBar::item { border: none; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #202428; border: 1px solid #464d54; border-radius: 3px; padding: 5px; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid #2684ff; }
    )");
    const int font_index = read_canvas_preferences().interface_font;
    const int sizes[]{10, 11, 13, 15};
    stylesheet.replace("font-size: 13px", QString("font-size: %1px").arg(sizes[font_index]));
    QFont interface_font("Microsoft YaHei UI");
    interface_font.setPixelSize(sizes[font_index]);
    QApplication::setFont(interface_font);
    qApp->setStyleSheet(stylesheet);
    qApp->setProperty("interfaceFontSize", font_index);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Huelith — 开发预览"));
    setWindowIcon(application_icon());
    setWindowFlag(Qt::FramelessWindowHint);
    resize(1440, 900);
    setMinimumSize(800, 560);
    setDockOptions(AllowNestedDocks | AllowTabbedDocks);
    pages_ = new QStackedWidget(this);
    auto* welcome = new QWidget(pages_);
    auto* welcome_layout = new QVBoxLayout(welcome);
    welcome_layout->addStretch();
    auto* title = new QLabel(tr("Huelith"), welcome);
    title->setObjectName("welcomeTitle");
    title->setAlignment(Qt::AlignCenter);
    auto title_font = title->font();
    title_font.setPointSize(28);
    title->setFont(title_font);
    welcome_layout->addWidget(title);
    auto* subtitle = new QLabel(tr("开始一张新图片"), welcome);
    subtitle->setAlignment(Qt::AlignCenter);
    welcome_layout->addWidget(subtitle);
    auto* create = new QPushButton(tr("新建图片…"), welcome);
    create->setFixedWidth(160);
    connect(create, &QPushButton::clicked, this, [this] { request_new_document(); });
    welcome_layout->addWidget(create, 0, Qt::AlignHCenter);
    welcome_layout->addStretch();
    pages_->addWidget(welcome);
    documents_ = new QTabWidget(pages_);
    documents_->setObjectName("documents");
    documents_->setDocumentMode(true);
    documents_->tabBar()->setDrawBase(false);
    documents_->setTabsClosable(true);
    documents_->setMovable(true);
    pages_->addWidget(documents_);
    setCentralWidget(pages_);
    document_status_ = new QLabel(this);
    statusBar()->addWidget(document_status_, 1);
    statusBar()->addPermanentWidget(new QLabel(tr("M3 · 人像精修验收版"), this));
    menus_ = new QMenuBar(this);
    build_menus();
    build_panels();
    editor_ = new EditorController(this);
    setMenuWidget(new WindowHeader(menus_, this));
    default_layout_ = saveState(1);
    if (read_canvas_preferences().restore_workspace) {
        const auto layout = QSettings().value("workspace/lastLayout").toByteArray();
        if (!layout.isEmpty() && !restoreState(layout, 1))
            restoreState(default_layout_, 1);
    }
    apply_workspace_preferences();
    connect(documents_, &QTabWidget::currentChanged, this, [this] {
        refresh_document_state();
        if(auto* c=active_canvas();c && c->window()!=this) {c->window()->show();c->window()->raise();}
    });
    connect(documents_, &QTabWidget::tabCloseRequested, this,
            [this](int index) { request_close_document(index); });
    refresh_document_state();
}

void MainWindow::build_menus() {
    auto* file = menus_->addMenu(tr("文件(&F)"));
    auto* create = file->addAction(tr("新建图片…"));
    create->setShortcut(QKeySequence::New);
    connect(create, &QAction::triggered, this, [this] { request_new_document(); });
    auto* close = file->addAction(tr("关闭图片"));
    close->setShortcuts({QKeySequence("Ctrl+W"), QKeySequence("Ctrl+F4")});
    connect(close, &QAction::triggered, this,
            [this] { request_close_document(documents_->currentIndex()); });
    file->addSeparator();
    auto* quit = file->addAction(tr("退出"));
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto* view = menus_->addMenu(tr("视图(&V)"));
    auto* fit = view->addAction(tr("适合窗口"));
    fit->setShortcut(QKeySequence("Ctrl+0"));
    connect(fit, &QAction::triggered, this, [this] {
        if (auto* canvas = active_canvas()) {
            canvas->fit_to_view();
        }
    });
    auto* actual = view->addAction(tr("100%"));
    actual->setShortcut(QKeySequence("Ctrl+1"));
    connect(actual, &QAction::triggered, this, [this] {
        if (auto* canvas = active_canvas()) {
            canvas->set_zoom(1.0);
        }
    });
    auto* zoom_in = view->addAction(tr("放大"));
    zoom_in->setShortcut(QKeySequence::ZoomIn);
    connect(zoom_in, &QAction::triggered, this, [this] { change_zoom(1.25); });
    auto* zoom_out = view->addAction(tr("缩小"));
    zoom_out->setShortcut(QKeySequence::ZoomOut);
    connect(zoom_out, &QAction::triggered, this, [this] { change_zoom(0.8); });
}

void MainWindow::build_panels() {
    auto* window_menu = menus_->addMenu(tr("窗口(&W)"));
    auto* arrange = window_menu->addMenu(tr("排列"));
    connect(arrange->addAction(tr("在窗口中浮动当前文档")), &QAction::triggered, this, &MainWindow::float_document);
    connect(arrange->addAction(tr("将所有文档合并到标签栏")), &QAction::triggered, this, &MainWindow::dock_documents);
    auto* properties_dock = new QDockWidget(tr("属性"), this);
    properties_dock->setObjectName("propertiesDock");
    properties_dock->setMinimumWidth(280);
    properties_dock->setMaximumWidth(480);
    properties_ = new QLabel(properties_dock);
    properties_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    properties_->setMargin(16);
    properties_->setWordWrap(true);
    properties_dock->setWidget(properties_);
    addDockWidget(Qt::RightDockWidgetArea, properties_dock);
    window_menu->addAction(properties_dock->toggleViewAction());
    auto* layers_dock = new QDockWidget(tr("图层"), this);
    layers_dock->setObjectName("layersDock");
    layers_dock->setMinimumHeight(240);
    auto* empty_layers = new QLabel(tr("图层编辑将在后续开发阶段接入。"), layers_dock);
    empty_layers->setAlignment(Qt::AlignCenter);
    empty_layers->setWordWrap(true);
    layers_dock->setWidget(empty_layers);
    addDockWidget(Qt::RightDockWidgetArea, layers_dock);
    splitDockWidget(properties_dock, layers_dock, Qt::Vertical);
    resizeDocks({properties_dock, layers_dock}, {320, 320}, Qt::Horizontal);
    window_menu->addAction(layers_dock->toggleViewAction());
    auto* reset = window_menu->addAction(tr("重置工作区"));
    connect(reset, &QAction::triggered, this, &MainWindow::reset_workspace);
    auto* toolbar = new QToolBar(tr("画布视图"), this);
    toolbar->setObjectName("viewToolbar");
    toolbar->setFixedHeight(40);
    toolbar->setMovable(false);
    auto* fit = toolbar->addAction(tr("适合窗口"));
    connect(fit, &QAction::triggered, this, [this] {
        if (auto* canvas = active_canvas()) {
            canvas->fit_to_view();
        }
    });
    addToolBar(Qt::TopToolBarArea, toolbar);
}

bool MainWindow::create_document(const CanvasSpec& spec) {
    if (validate(spec)) {
        return false;
    }
    auto* canvas = new CanvasView(spec, documents_);
    if (editor_)
        editor_->bind(canvas);
    const auto name = QString::fromUtf8(spec.name.data(), static_cast<qsizetype>(spec.name.size()));
    const auto index = documents_->addTab(canvas, name + " *");
    documents_->setCurrentIndex(index);
    pages_->setCurrentWidget(documents_);
    canvas->fit_to_view();
    refresh_document_state();
    return true;
}

bool MainWindow::open_document(const QString& path, bool place) {
    return editor_->open(path, place);
}
void MainWindow::open_documents(const QStringList& paths) {
    editor_->open_files(paths);
}

int MainWindow::document_count() const noexcept {
    return documents_->count();
}

CanvasView* MainWindow::active_canvas() const {
    return canvas_at(documents_->currentIndex());
}

CanvasView* MainWindow::canvas_at(int index) const {
    auto* page = documents_->widget(index);
    if (auto* canvas = dynamic_cast<CanvasView*>(page)) return canvas;
    return page ? dynamic_cast<CanvasView*>(page->property("documentCanvas").value<QObject*>()) : nullptr;
}
int MainWindow::canvas_index(const CanvasView* canvas) const {
    for (int i=0;i<documents_->count();++i) if (canvas_at(i)==canvas) return i;
    return -1;
}
void MainWindow::float_document() {
    auto* canvas=active_canvas(); if(!canvas)return;
    if(canvas->window()!=this) {canvas->window()->show();canvas->window()->raise();return;}
    canvas->finish_text(true);canvas->commit_transform();
    const int index=canvas_index(canvas);const auto title=documents_->tabText(index);
    const QSignalBlocker block(documents_);
    documents_->removeTab(index);
    auto* placeholder=new QWidget(documents_);
    placeholder->setProperty("documentCanvas",QVariant::fromValue(static_cast<QObject*>(canvas)));
    auto* layout=new QVBoxLayout(placeholder);layout->addStretch();
    auto* return_button=new QPushButton(tr("文档已浮动 · 返回标签栏"),placeholder);
    layout->addWidget(return_button,0,Qt::AlignCenter);layout->addStretch();
    connect(return_button,&QPushButton::clicked,this,&MainWindow::dock_documents);
    documents_->insertTab(index,placeholder,title);documents_->setCurrentIndex(index);
    auto* floating=new QDialog(this,Qt::Window);
    floating->setObjectName("floatingDocument");floating->setWindowTitle(title);
    floating->setProperty("documentCanvas",QVariant::fromValue(static_cast<QObject*>(canvas)));
    // A non-modal document window shares the original project and controller.
    auto* content=new QVBoxLayout(floating);content->setContentsMargins(0,0,0,0);content->addWidget(canvas);
    floating->resize(900,650);floating->setMinimumSize(320,240);
    for(auto* action:findChildren<QAction*>()) if(!action->shortcuts().isEmpty()) floating->addAction(action);
    floating->installEventFilter(this);floating->show();canvas->setFocus();refresh_document_state();
}
void MainWindow::dock_documents() {
    const int active=documents_->currentIndex();
    const QSignalBlocker block(documents_);
    for(int i=0;i<documents_->count();++i) {
        auto* canvas=canvas_at(i);auto* placeholder=documents_->widget(i);
        if(!canvas || canvas==placeholder)continue;
        const auto title=documents_->tabText(i);auto* floating=canvas->window();
        floating->removeEventFilter(this);documents_->removeTab(i);
        canvas->setParent(documents_);documents_->insertTab(i,canvas,title);
        placeholder->deleteLater();floating->hide();floating->deleteLater();
    }
    documents_->setCurrentIndex(active);refresh_document_state();if(auto* c=active_canvas())c->setFocus();
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event) {
    if(watched->objectName()=="floatingDocument") {
        auto* canvas=dynamic_cast<CanvasView*>(watched->property("documentCanvas").value<QObject*>());
        const int index=canvas_index(canvas);
        if(event->type()==QEvent::WindowActivate && index>=0) {
            documents_->setCurrentIndex(index);refresh_document_state();
        } else if(event->type()==QEvent::Close) {
            static_cast<QCloseEvent*>(event)->ignore();
            request_close_document(index);return true;
        } else if(event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
            // Escape belongs to the canvas gesture, never QDialog::reject().
            return true;
        }
    }
    return QMainWindow::eventFilter(watched,event);
}

void MainWindow::change_zoom(double multiplier) {
    if (auto* canvas = active_canvas()) {
        canvas->set_zoom(canvas->zoom() * multiplier);
    }
}

void MainWindow::request_new_document() {
    NewDocumentDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        const bool created = create_document(dialog.specification());
        if (!created) {
            QMessageBox::warning(this, tr("无法新建"), tr("图片参数无效，请检查后重试。"));
        }
    }
}

bool MainWindow::confirm_discard() {
    return !editor_ || editor_->confirm_close(active_canvas());
}

void MainWindow::request_close_document(int index) {
    if (index < 0 || index >= documents_->count() ||
        (editor_ &&
         !editor_->confirm_close(canvas_at(index)))) {
        return;
    }
    auto* page = documents_->widget(index);
    auto* canvas = canvas_at(index);
    if (canvas) {
        QFile::remove(canvas->recovery_file);
        if (canvas != page) {
            auto* floating = canvas->window();
            floating->removeEventFilter(this);
            floating->hide();
            floating->deleteLater();
        }
    }
    documents_->removeTab(index);
    page->deleteLater();
    refresh_document_state();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    for (int i = 0; i < documents_->count(); ++i) {
        auto* canvas = canvas_at(i);
        if (editor_ && !editor_->confirm_close(canvas)) {
            event->ignore();
            return;
        }
    }
    for (int i = 0; i < documents_->count(); ++i) {
        if (auto* canvas = canvas_at(i))
            QFile::remove(canvas->recovery_file);
    }
    if (read_canvas_preferences().restore_workspace) {
        QSettings settings;
        settings.setValue("workspace/lastLayout", saveState(1));
        settings.sync();
    }
    event->accept();
}

bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    const auto* native = static_cast<MSG*>(message);
    if (native->message == WM_GETMINMAXINFO) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(native->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(native->lParam);
            limits->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                                     monitor.rcWork.top - monitor.rcMonitor.top};
            limits->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                                 monitor.rcWork.bottom - monitor.rcWork.top};
            const auto dpi = GetDpiForWindow(native->hwnd);
            limits->ptMinTrackSize = {MulDiv(minimumWidth(), static_cast<int>(dpi), 96),
                                      MulDiv(minimumHeight(), static_cast<int>(dpi), 96)};
            *result = 0;
            return true;
        }
    }
    if (native->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
        RECT bounds{};
        if (GetWindowRect(native->hwnd, &bounds)) {
            const int border = MulDiv(6, static_cast<int>(GetDpiForWindow(native->hwnd)), 96);
            const int x = GET_X_LPARAM(native->lParam);
            const int y = GET_Y_LPARAM(native->lParam);
            const bool left = x >= bounds.left && x < bounds.left + border;
            const bool right = x < bounds.right && x >= bounds.right - border;
            const bool top = y >= bounds.top && y < bounds.top + border;
            const bool bottom = y < bounds.bottom && y >= bounds.bottom - border;
            if (top || bottom || left || right) {
                *result = top      ? (left    ? HTTOPLEFT
                                      : right ? HTTOPRIGHT
                                              : HTTOP)
                          : bottom ? (left    ? HTBOTTOMLEFT
                                      : right ? HTBOTTOMRIGHT
                                              : HTBOTTOM)
                          : left   ? HTLEFT
                                   : HTRIGHT;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(event_type, message, result);
}

void MainWindow::reset_workspace() {
    restoreState(default_layout_, 1);
    apply_workspace_preferences();
}
void MainWindow::apply_workspace_preferences() {
    const auto preferences = read_canvas_preferences();
    for (auto* dock : findChildren<QDockWidget*>()) {
        dock->setFeatures(preferences.lock_workspace
                              ? QDockWidget::DockWidgetFeatures(QDockWidget::DockWidgetClosable)
                              : QDockWidget::DockWidgetFeatures(QDockWidget::DockWidgetClosable |
                                                                QDockWidget::DockWidgetMovable |
                                                                QDockWidget::DockWidgetFloatable));
    }
    documents_->setMovable(preferences.movable_tabs);
    documents_->setTabsClosable(preferences.close_tabs);
    documents_->tabBar()->setStyleSheet(
        preferences.large_tabs ? "QTabBar::tab { padding: 12px 22px; }" : QString{});
}

void MainWindow::refresh_document_state() {
    const auto* canvas = active_canvas();
    if (!canvas) {
        pages_->setCurrentIndex(0);
        properties_->setText(tr("未打开图片"));
        document_status_->setText(tr("就绪"));
        if (editor_)
            editor_->refresh();
        return;
    }
    const auto& spec = canvas->spec();
    properties_->setText(tr("%1\n%2 × %3 像素 · %4 ppi\n线性 Rec.2020 · FP32\n当前目标：%5")
                             .arg(canvas->project().state().name)
                             .arg(spec.width)
                             .arg(spec.height)
                             .arg(spec.resolution_ppi)
                             .arg(canvas->mask_target ? tr("蒙版") : tr("图层内容")));
    document_status_->setText(
        tr("%1 × %2 像素 | %3 | 视图旋转 %4°")
            .arg(spec.width)
            .arg(spec.height)
            .arg(canvas->project().dirty() ? tr("有未保存修改") : tr("已保存"))
            .arg(canvas->rotation, 0, 'f', 1));
    if (editor_)
        editor_->refresh();
}
} // namespace huelith::desktop
