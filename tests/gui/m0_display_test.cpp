#include "huelith/validation/gpu_probe.h"
#include <QApplication>
#include <QColorSpace>
#include <QFile>
#include <QLabel>
#include <QScreen>
#include <gtest/gtest.h>

// M0 endpoint check: GPU readback -> floating-point image -> monitor ICC -> QWidget.
TEST(M0Display, GpuImageThroughDisplayProfileRendersExpectedPatches) {
    if (QGuiApplication::platformName() != "windows")
        GTEST_SKIP() << "native Windows display required";
    const auto profile_path = qEnvironmentVariable("HUELITH_M0_DISPLAY_ICC");
    if (profile_path.isEmpty())
        GTEST_SKIP() << "provide the actual monitor ICC from the environment probe";
    QFile file(profile_path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto profile = QColorSpace::fromIccProfile(file.readAll());
    ASSERT_TRUE(profile.isValid());
    constexpr int width{256};
    constexpr int height{128};
    std::vector<huelith::validation::Pixel> source(width * height), background(width * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const float gray = static_cast<float>(x) / 255.0F;
            source[index] = y < 64 ? huelith::validation::Pixel{gray, gray, gray, 1.F}
                                   : huelith::validation::Pixel{.25F, .05F, .1F, .5F};
            background[index] = {.2F, .2F, .2F, 1.F};
        }
    }
    const auto result = huelith::validation::gpu_composite(
        source, background, huelith::validation::BlendMode::normal, false);
    QImage image(width, height, QImage::Format_RGBA32FPx4);
    image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<float*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const auto& pixel = result.pixels[static_cast<std::size_t>(y * width + x)];
            for (std::size_t c = 0; c < 4; ++c)
                row[static_cast<std::size_t>(x) * 4 + c] = pixel[c];
        }
    }
    const auto display = image.convertedToColorSpace(profile, QImage::Format_RGBA8888);
    ASSERT_FALSE(display.isNull());
    QLabel window;
    window.setWindowTitle("Huelith M0 display endpoint");
    window.setFixedSize(width, height);
    window.setPixmap(QPixmap::fromImage(display));
    window.show();
    QApplication::processEvents();
    const auto captured = window.grab().toImage();
    ASSERT_FALSE(captured.isNull());
    const auto ratio = window.devicePixelRatioF();
    for (const auto point :
         {QPoint(0, 20), QPoint(55, 20), QPoint(128, 20), QPoint(254, 20), QPoint(120, 90)}) {
        const auto expected = display.pixelColor(point);
        const auto actual =
            captured.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
        EXPECT_NEAR(actual.red(), expected.red(), 2);
        EXPECT_NEAR(actual.green(), expected.green(), 2);
        EXPECT_NEAR(actual.blue(), expected.blue(), 2);
    }
    // Independent sRGB gamma reference for the actual sRGB monitor profile.
    if (profile == QColorSpace(QColorSpace::SRgb))
        EXPECT_NEAR(display.pixelColor(55, 20).red(), 128, 2);
    const auto output = qEnvironmentVariable("HUELITH_M0_DISPLAY_SCREENSHOT");
    if (!output.isEmpty())
        EXPECT_TRUE(captured.save(output));
}

TEST(M0Display, SavedOffscreenGeometryIsRecoveredToAvailableScreen) {
    QWidget absent;
    absent.resize(640, 420);
    absent.move(30000, 30000);
    const auto saved = absent.saveGeometry();
    QWidget restored;
    ASSERT_TRUE(restored.restoreGeometry(saved));
    restored.show();
    QApplication::processEvents();
    bool visible{};
    for (const auto* screen : QGuiApplication::screens())
        visible |= screen->availableGeometry().contains(restored.frameGeometry().center());
    EXPECT_TRUE(visible);
}
