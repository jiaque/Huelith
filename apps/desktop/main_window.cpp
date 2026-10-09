#include "main_window.h"
#include "canvas_view.h"
#include "new_document_dialog.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace huelith::desktop {
void apply_dark_theme() {
    QApplication::setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(45, 45, 45));
    palette.setColor(QPalette::WindowText, QColor(225, 225, 225));
    palette.setColor(QPalette::Base, QColor(32, 32, 32));
    palette.setColor(QPalette::AlternateBase, QColor(50, 50, 50));
    palette.setColor(QPalette::Text, QColor(225, 225, 225));
    palette.setColor(QPalette::Button, QColor(53, 53, 53));
    palette.setColor(QPalette::ButtonText, QColor(225, 225, 225));
    palette.setColor(QPalette::Highlight, QColor(55, 117, 190));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(125, 125, 125));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(125, 125, 125));
    QApplication::setPalette(palette);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Huelith — 开发预览"));
    resize(1440, 900);
    setMinimumSize(800, 560);
    setDockOptions(AllowNestedDocks | AllowTabbedDocks);
    pages_ = new QStackedWidget(this);
    auto* welcome = new QWidget(pages_);
    auto* welcome_layout = new QVBoxLayout(welcome);
    welcome_layout->addStretch();
    auto* title = new QLabel(tr("Huelith"), welcome);
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
    documents_->setTabsClosable(true);
    documents_->setMovable(true);
    pages_->addWidget(documents_);
    setCentralWidget(pages_);
    document_status_ = new QLabel(this);
    statusBar()->addWidget(document_status_, 1);
    statusBar()->addPermanentWidget(new QLabel(tr("M0 · 窗口与空白画布原型"), this));
    build_menus();
    build_panels();
    default_layout_ = saveState(1);
    connect(documents_, &QTabWidget::currentChanged, this,
            [this] { refresh_document_state(); });
    connect(documents_, &QTabWidget::tabCloseRequested, this,
            [this](int index) { request_close_document(index); });
    refresh_document_state();
}

void MainWindow::build_menus() {
    auto* file = menuBar()->addMenu(tr("文件(&F)"));
    auto* create = file->addAction(tr("新建图片…"));
    create->setShortcut(QKeySequence::New);
    connect(create, &QAction::triggered, this, [this] { request_new_document(); });
    auto* close = file->addAction(tr("关闭图片"));
    close->setShortcut(QKeySequence::Close);
    connect(close, &QAction::triggered, this,
            [this] { request_close_document(documents_->currentIndex()); });
    file->addSeparator();
    auto* quit = file->addAction(tr("退出"));
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto* view = menuBar()->addMenu(tr("视图(&V)"));
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
    auto* window_menu = menuBar()->addMenu(tr("窗口(&W)"));
    auto* properties_dock = new QDockWidget(tr("属性"), this);
    properties_dock->setObjectName("propertiesDock");
    properties_dock->setMinimumWidth(280);
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
    window_menu->addAction(layers_dock->toggleViewAction());
    auto* reset = window_menu->addAction(tr("重置工作区"));
    connect(reset, &QAction::triggered, this, &MainWindow::reset_workspace);
    auto* toolbar = new QToolBar(tr("画布视图"), this);
    toolbar->setObjectName("viewToolbar");
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
    const auto name = QString::fromUtf8(spec.name.data(), static_cast<qsizetype>(spec.name.size()));
    const auto index = documents_->addTab(canvas, name + " *");
    documents_->setCurrentIndex(index);
    pages_->setCurrentWidget(documents_);
    canvas->fit_to_view();
    refresh_document_state();
    return true;
}

int MainWindow::document_count() const noexcept {
    return documents_->count();
}

CanvasView* MainWindow::active_canvas() const {
    return dynamic_cast<CanvasView*>(documents_->currentWidget());
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
    QMessageBox prompt(QMessageBox::Warning, tr("关闭未保存图片"),
                       tr("当前开发版本尚不支持保存工程。关闭后将丢弃空白图片及其设置。"),
                       QMessageBox::Discard | QMessageBox::Cancel, this);
    prompt.button(QMessageBox::Discard)->setText(tr("丢弃并关闭"));
    prompt.button(QMessageBox::Cancel)->setText(tr("继续编辑"));
    prompt.setDefaultButton(QMessageBox::Cancel);
    return prompt.exec() == QMessageBox::Discard;
}

void MainWindow::request_close_document(int index) {
    if (index < 0 || index >= documents_->count() || !confirm_discard()) {
        return;
    }
    auto* page = documents_->widget(index);
    documents_->removeTab(index);
    page->deleteLater();
    refresh_document_state();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (document_count() != 0 && !confirm_discard()) {
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::reset_workspace() {
    restoreState(default_layout_, 1);
}

void MainWindow::refresh_document_state() {
    const auto* canvas = active_canvas();
    if (!canvas) {
        pages_->setCurrentIndex(0);
        properties_->setText(tr("未打开图片"));
        document_status_->setText(tr("就绪"));
        return;
    }
    const auto& spec = canvas->spec();
    properties_->setText(tr("画布属性\n\n%1 × %2 像素\n%3 ppi\n\n线性 sRGB（原型暂定）\n\n当前仅显示空白画布；高精度图像处理尚未接入。")
                             .arg(spec.width).arg(spec.height).arg(spec.resolution_ppi));
    document_status_->setText(tr("%1 × %2 像素  |  未保存").arg(spec.width).arg(spec.height));
}
} // namespace huelith::desktop
