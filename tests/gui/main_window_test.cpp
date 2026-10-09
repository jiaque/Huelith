#include "main_window.h"
#include "canvas_view.h"
#include "new_document_dialog.h"
#include <gtest/gtest.h>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QMenuBar>
#include <QMouseEvent>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#endif
#include <limits>

using huelith::desktop::MainWindow;

TEST(WindowChrome, IntegratesMenuAndControlsWithoutNativeTitleBar) {
    MainWindow window;
    EXPECT_TRUE(window.windowFlags().testFlag(Qt::FramelessWindowHint));
    auto* header = window.findChild<QWidget*>("windowHeader");
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->height(), 36);
    EXPECT_NE(header->findChild<QMenuBar*>(), nullptr);
    EXPECT_NE(header->findChild<QPushButton*>("closeWindow"), nullptr);
    window.show();
    auto* maximize = header->findChild<QPushButton*>("maximizeWindow");
    ASSERT_NE(maximize, nullptr);
    maximize->click();
    EXPECT_TRUE(window.isMaximized());
    maximize->click();
    EXPECT_FALSE(window.isMaximized());
    QMouseEvent double_click(QEvent::MouseButtonDblClick, QPointF(100, 18), QPointF(100, 18),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(header, &double_click);
    EXPECT_TRUE(window.isMaximized());
    auto* minimize = header->findChild<QPushButton*>("minimizeWindow");
    ASSERT_NE(minimize, nullptr);
    minimize->click();
    EXPECT_TRUE(window.isMinimized());
    window.showNormal();
}

TEST(WindowChrome, CloseButtonPreservesCancelBehavior) {
    MainWindow window;
    window.show();
    ASSERT_TRUE(window.create_document(huelith::CanvasSpec{}));
    auto* close = window.findChild<QPushButton*>("closeWindow");
    ASSERT_NE(close, nullptr);
    QTimer::singleShot(0, &window, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt) {
            prompt->done(QMessageBox::Cancel);
        }
    });
    close->click();
    EXPECT_TRUE(window.isVisible());
    EXPECT_EQ(window.document_count(), 1);
}

TEST(WindowChrome, NativeResizeAndMaximizeRespectWorkArea) {
#ifdef Q_OS_WIN
    if (QApplication::platformName() != "windows") {
        GTEST_SKIP() << "Requires native Windows platform";
    }
    MainWindow window;
    window.show();
    QApplication::processEvents();
    const auto handle = reinterpret_cast<HWND>(window.winId());
    RECT bounds{};
    ASSERT_TRUE(GetWindowRect(handle, &bounds));
    EXPECT_EQ(SendMessageW(handle, WM_NCHITTEST, 0,
                          MAKELPARAM(bounds.right - 2, bounds.bottom - 2)), HTBOTTOMRIGHT);
    EXPECT_EQ(SendMessageW(handle, WM_NCHITTEST, 0,
                          MAKELPARAM(bounds.left + 2, bounds.top + 2)), HTTOPLEFT);
    window.showMaximized();
    QApplication::processEvents();
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    ASSERT_TRUE(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    ASSERT_TRUE(GetWindowRect(handle, &bounds));
    EXPECT_GE(bounds.left, monitor.rcWork.left);
    EXPECT_GE(bounds.top, monitor.rcWork.top);
    EXPECT_LE(bounds.right, monitor.rcWork.right);
    EXPECT_LE(bounds.bottom, monitor.rcWork.bottom);
#else
    GTEST_SKIP() << "Requires Windows";
#endif
}

TEST(Desktop, StartsWithoutDocumentAndRejectsInvalidCreationAtomically) {
    MainWindow window;
    EXPECT_EQ(window.document_count(), 0);
    huelith::CanvasSpec invalid;
    invalid.width = 0;
    EXPECT_FALSE(window.create_document(invalid));
    EXPECT_EQ(window.document_count(), 0);
}

TEST(Desktop, PreservesIndependentDocumentSettingsAndUnicodeNames) {
    MainWindow window;
    huelith::CanvasSpec portrait;
    portrait.name = "人像 样片";
    portrait.width = 800;
    ASSERT_TRUE(window.create_document(portrait));
    ASSERT_TRUE(window.create_document(huelith::CanvasSpec{}));
    auto* tabs = window.findChild<QTabWidget*>("documents");
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), QString::fromUtf8("人像 样片 *"));
    auto* first = dynamic_cast<huelith::desktop::CanvasView*>(tabs->widget(0));
    auto* second = dynamic_cast<huelith::desktop::CanvasView*>(tabs->widget(1));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->set_zoom(2.0);
    EXPECT_EQ(first->spec().width, 800);
    EXPECT_EQ(second->spec().width, 1920);
    EXPECT_NE(first->zoom(), second->zoom());
}

TEST(Desktop, ResetWorkspaceRestoresClosedPanelWithoutChangingDocuments) {
    MainWindow window;
    ASSERT_TRUE(window.create_document(huelith::CanvasSpec{}));
    auto* dock = window.findChild<QDockWidget*>("propertiesDock");
    ASSERT_NE(dock, nullptr);
    dock->hide();
    window.reset_workspace();
    EXPECT_FALSE(dock->isHidden());
    EXPECT_EQ(window.document_count(), 1);
}

TEST(Desktop, WhitespaceNameCannotBeSubmitted) {
    huelith::desktop::NewDocumentDialog dialog;
    auto* name = dialog.findChild<QLineEdit*>("documentName");
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(name, nullptr);
    ASSERT_NE(buttons, nullptr);
    name->setText("   ");
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    name->setText(QString::fromUtf8("产品"));
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    EXPECT_EQ(dialog.specification().name, "产品");
}

TEST(Desktop, CancelClosePreservesUnsavedDocument) {
    MainWindow window;
    window.show();
    ASSERT_TRUE(window.create_document(huelith::CanvasSpec{}));
    QTimer::singleShot(0, &window, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt) {
            prompt->done(QMessageBox::Cancel);
        }
    });
    EXPECT_FALSE(window.close());
    EXPECT_EQ(window.document_count(), 1);
}

TEST(CanvasView, ZoomRejectsNonFiniteValuesAndDoesNotChangeDocument) {
    huelith::desktop::CanvasView canvas(huelith::CanvasSpec{});
    canvas.set_zoom(2.0);
    canvas.set_zoom(std::numeric_limits<double>::quiet_NaN());
    canvas.set_zoom(-1.0);
    EXPECT_EQ(canvas.zoom(), 2.0);
    canvas.set_zoom(100.0);
    EXPECT_EQ(canvas.zoom(), 32.0);
    EXPECT_EQ(canvas.spec().width, 1920);
}

TEST(Desktop, DiscardClosesOnlyRequestedTab) {
    MainWindow window;
    window.show();
    ASSERT_TRUE(window.create_document(huelith::CanvasSpec{}));
    huelith::CanvasSpec retained;
    retained.name = "保留";
    ASSERT_TRUE(window.create_document(retained));
    auto* tabs = window.findChild<QTabWidget*>("documents");
    ASSERT_NE(tabs, nullptr);
    QTimer::singleShot(0, &window, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt) {
            prompt->done(QMessageBox::Discard);
        }
    });
    tabs->tabCloseRequested(0);
    ASSERT_EQ(window.document_count(), 1);
    EXPECT_EQ(tabs->tabText(0), QString::fromUtf8("保留 *"));
}

TEST(CanvasView, WhiteBackgroundRendersWhiteInsideAndDarkOutside) {
    huelith::CanvasSpec spec;
    spec.width = spec.height = 100;
    spec.background = huelith::CanvasBackground::white;
    huelith::desktop::CanvasView canvas(spec);
    canvas.resize(300, 300);
    canvas.show();
    canvas.set_zoom(1.0);
    QApplication::processEvents();
    const auto image = canvas.grab().toImage();
    EXPECT_EQ(image.pixelColor(image.width() / 2, image.height() / 2), QColor(Qt::white));
    EXPECT_EQ(image.pixelColor(0, 0), QColor("#202428"));
}

TEST(Desktop, RendersWindowAndCanvasWithoutExternalAssets) {
    MainWindow window;
    const auto path = qEnvironmentVariable("HUELITH_SCREENSHOT_PATH");
    if (!path.isEmpty()) {
        window.show();
        QApplication::processEvents();
        EXPECT_TRUE(window.grab().save(path + ".welcome.png"));
    }
    ASSERT_TRUE(window.create_document(huelith::CanvasSpec{}));
    window.show();
    QApplication::processEvents();
    const auto screenshot = window.grab();
    ASSERT_FALSE(screenshot.isNull());
    if (!path.isEmpty()) {
        EXPECT_TRUE(screenshot.save(path));
    }
}

int main(int argc, char* argv[]) {
    // Discovery also creates QApplication; default to offscreen unless explicitly overridden.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication application(argc, argv);
    huelith::desktop::apply_dark_theme();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
