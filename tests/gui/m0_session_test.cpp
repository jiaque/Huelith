#include "main_window.h"
#include "validation_session.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QScreen>
#include <QWindow>
#include <gtest/gtest.h>
using huelith::desktop::MainWindow;

TEST(M0Session, RealScreensChangeDpiAndRestoreGeometry) {
    if (QGuiApplication::platformName() != "windows")
        GTEST_SKIP() << "requires native Windows screens";
    const auto screens = QGuiApplication::screens();
    if (screens.size() < 2)
        GTEST_SKIP() << "two physical screens required";
    huelith::desktop::MainWindow window;
    window.show();
    QApplication::processEvents();
    for (auto* screen : screens) {
        window.windowHandle()->setScreen(screen);
        const auto area = screen->availableGeometry();
        window.setGeometry(area.x() + 40, area.y() + 40, 900, 600);
        QApplication::processEvents();
        ASSERT_EQ(window.screen(), screen);
        EXPECT_DOUBLE_EQ(window.devicePixelRatioF(), screen->devicePixelRatio());
        const auto saved = window.saveGeometry();
        huelith::desktop::MainWindow restored;
        ASSERT_TRUE(restored.restoreGeometry(saved));
        restored.show();
        QApplication::processEvents();
        EXPECT_TRUE(screen->availableGeometry().intersects(restored.frameGeometry()));
        EXPECT_EQ(restored.screen(), screen);
        const auto folder = qEnvironmentVariable("HUELITH_M0_DPI_DIR");
        if (!folder.isEmpty())
            EXPECT_TRUE(window.grab().save(folder + "/dpi-" +
                                           QString::number(screen->devicePixelRatio()) + ".png"));
        restored.hide();
    }
    window.hide();
}
TEST(M0Session, ChineseInputCancelAndSingleUndoCommit) {
    huelith::validation::Document doc({"test", "original", 0});
    MainWindow parent;
    {
        huelith::desktop::ValidationSession dialog(doc, huelith::desktop::SessionKind::portrait,
                                                   &parent);
        EXPECT_EQ(dialog.windowModality(), Qt::WindowModal);
        EXPECT_TRUE(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
        auto* text = dialog.findChild<QLineEdit*>("sessionText");
        ASSERT_NE(text, nullptr);
        text->selectAll();
        QInputMethodEvent event;
        event.setCommitString(QString::fromUtf8("中文人像草稿"));
        QApplication::sendEvent(text, &event);
        EXPECT_EQ(text->text(), QString::fromUtf8("中文人像草稿"));
        dialog.reject();
        EXPECT_EQ(doc.snapshot().state.block, "original");
    }
    {
        huelith::desktop::ValidationSession dialog(doc, huelith::desktop::SessionKind::raw,
                                                   &parent);
        auto* spin = dialog.findChild<QDoubleSpinBox*>("sessionParameter");
        ASSERT_NE(spin, nullptr);
        spin->setValue(12.5);
        dialog.accept();
        EXPECT_EQ(dialog.result(), QDialog::Accepted);
        EXPECT_EQ(doc.snapshot().state.parameter, 12.5);
        ASSERT_TRUE(doc.undo());
        EXPECT_EQ(doc.snapshot().state.parameter, 0);
        EXPECT_FALSE(doc.undo());
    }
}

TEST(M0Session, StaleWindowCannotCommitAndNativeScreenshot) {
    huelith::validation::Document doc({"test", "original", 0});
    MainWindow parent;
    huelith::desktop::ValidationSession dialog(doc, huelith::desktop::SessionKind::portrait,
                                               &parent);
    auto other = doc.begin_session();
    other.draft.parameter = 7;
    ASSERT_TRUE(doc.commit(other));
    dialog.accept();
    EXPECT_NE(dialog.result(), QDialog::Accepted);
    EXPECT_EQ(doc.snapshot().state.parameter, 7);
    dialog.show();
    QApplication::processEvents();
    const auto path = qEnvironmentVariable("HUELITH_M0_SESSION_SCREENSHOT");
    if (!path.isEmpty())
        EXPECT_TRUE(dialog.grab().save(path));
    dialog.reject();
}
