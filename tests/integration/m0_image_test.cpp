#include "huelith/validation/image_math.h"
#include <QColorSpace>
#include <QImage>
#include <cmath>
#include <gtest/gtest.h>
#include <qfloat16.h>
#include <stdexcept>

namespace huelith::validation {
TEST(M0Color, PremultipliedBlendMatchesIndependentReference) {
    const Pixel source{0.4F, 0.1F, 0.2F, 0.5F};
    const Pixel back{0.1F, 0.2F, 0.3F, 0.5F};
    const auto normal = composite(source, back, BlendMode::normal);
    EXPECT_FLOAT_EQ(normal[0], 0.45F);
    EXPECT_FLOAT_EQ(normal[3], 0.75F);
    const auto multiply = composite(source, back, BlendMode::multiply);
    EXPECT_NEAR(multiply[0], 0.29F, 1e-6F);
    const auto screen = composite(source, back, BlendMode::screen);
    EXPECT_NEAR(screen[0], 0.46F, 1e-6F);
    EXPECT_EQ(composite({99, 99, 99, 0}, back, BlendMode::normal), back);
    EXPECT_GT(composite({2, 2, 2, 1}, back, BlendMode::normal)[0], 1);
}
TEST(M0Color, HalfPrecisionGradientHasBoundedRelativeError) {
    for (int i = -1000; i <= 16000; ++i) {
        const float original = static_cast<float>(i) / 1000.0F;
        const float roundtrip = static_cast<float>(qfloat16(original));
        ASSERT_LE(std::abs(roundtrip - original), 0.00001F + 0.0005F * std::abs(original));
    }
    // FP16 storage is not safe for unconstrained neural-network activations.
    EXPECT_FALSE(std::isfinite(static_cast<float>(qfloat16(100000.0F))));
}
TEST(M0Color, IccProfileAndLinearConversionPreserveFloatHighlights) {
    const QColorSpace space(QColorSpace::SRgb);
    ASSERT_TRUE(QColorSpace::fromIccProfile(space.iccProfile()).isValid());
    QImage image(2, 1, QImage::Format_RGBA32FPx4);
    image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    auto* pixels = reinterpret_cast<float*>(image.scanLine(0));
    pixels[0] = 0.21404114F;
    pixels[1] = 0.0F;
    pixels[2] = 2.0F;
    pixels[3] = 1.0F;
    pixels[4] = 0.1F;
    pixels[5] = 0.2F;
    pixels[6] = 0.3F;
    pixels[7] = 0.5F;
    const auto encoded = image.convertedToColorSpace(space, QImage::Format_RGBA32FPx4);
    ASSERT_FALSE(encoded.isNull());
    const auto* rgb = reinterpret_cast<const float*>(encoded.constScanLine(0));
    EXPECT_NEAR(rgb[0], 0.5F, 0.001F);
    EXPECT_GT(rgb[2], 1.0F);
    EXPECT_FLOAT_EQ(rgb[7], 0.5F);
    const auto returned = encoded.convertedToColorSpace(QColorSpace(QColorSpace::SRgbLinear),
                                                        QImage::Format_RGBA32FPx4);
    const auto* linear = reinterpret_cast<const float*>(returned.constScanLine(0));
    EXPECT_NEAR(linear[0], pixels[0], 0.001F);
    EXPECT_NEAR(linear[2], 2.0F, 0.005F);
}
TEST(M0Color, LinearRec2020RoundTripPreservesOutOfSrgbGamut) {
    const QColorSpace working(QColorSpace::Primaries::Bt2020,
                              QColorSpace::TransferFunction::Linear);
    const QColorSpace linear_srgb(QColorSpace::SRgbLinear);
    QImage image(1, 1, QImage::Format_RGBA32FPx4);
    image.setColorSpace(working);
    auto* p = reinterpret_cast<float*>(image.scanLine(0));
    p[0] = 1;
    p[1] = 0;
    p[2] = 0;
    p[3] = 1;
    const auto converted = image.convertedToColorSpace(linear_srgb, QImage::Format_RGBA32FPx4);
    ASSERT_FALSE(converted.isNull());
    const auto* q = reinterpret_cast<const float*>(converted.constScanLine(0));
    EXPECT_GT(q[0], 1);
    EXPECT_LT(q[1], 0);
    const auto restored = converted.convertedToColorSpace(working, QImage::Format_RGBA32FPx4);
    const auto* r = reinterpret_cast<const float*>(restored.constScanLine(0));
    EXPECT_NEAR(r[0], 1, 1e-4F);
    EXPECT_NEAR(r[1], 0, 1e-4F);
    EXPECT_NEAR(r[2], 0, 1e-4F);
}
TEST(M0Portrait, RangeBrushAndEraserDoNotMutateSourceRange) {
    const std::vector<float> empty(25, 0.0F);
    const auto added = paint_range(empty, 5, 5, 2.5F, 2.5F, 1.5F, 1, false);
    EXPECT_FLOAT_EQ(added[12], 1);
    EXPECT_FLOAT_EQ(added[0], 0);
    EXPECT_FLOAT_EQ(empty[12], 0);
    const auto erased = paint_range(added, 5, 5, 2.5F, 2.5F, 1.5F, 1, true);
    EXPECT_FLOAT_EQ(erased[12], 0);
    EXPECT_FLOAT_EQ(added[12], 1);
    EXPECT_THROW(static_cast<void>(paint_range(empty, 6, 5, 0, 0, 1, 1, false)),
                 std::invalid_argument);
}
TEST(M0Portrait, FrozenBackgroundDoesNotMove) {
    const auto frozen = protected_displacement({10, 20}, {4, -6}, 1);
    EXPECT_FLOAT_EQ(frozen.x, 10);
    EXPECT_FLOAT_EQ(frozen.y, 20);
    const auto blended = protected_displacement({10, 20}, {4, -6}, 0.5F);
    EXPECT_FLOAT_EQ(blended.x, 12);
    EXPECT_FLOAT_EQ(blended.y, 17);
}
} // namespace huelith::validation
