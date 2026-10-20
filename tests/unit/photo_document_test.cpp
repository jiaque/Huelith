#include "huelith/editing/photo_document.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
std::shared_ptr<const huelith::FloatImage> source() {
    return std::make_shared<const huelith::FloatImage>(1, 1,
        std::vector<huelith::LinearPixel>{{0.4F, 0.2F, 0.1F, 0.5F}});
}
}

TEST(Exposure, PreservesAlphaAndUnboundedLinearHighlights) {
    const auto pixel = huelith::expose({0.8F, -0.1F, 0.25F, 0.5F}, 2.0F);
    EXPECT_FLOAT_EQ(pixel.red, 1.6F);
    EXPECT_FLOAT_EQ(pixel.green, -0.2F);
    EXPECT_FLOAT_EQ(pixel.blue, 0.5F);
    EXPECT_FLOAT_EQ(pixel.alpha, 0.5F);
}

TEST(PhotoHistory, UndoRedoAndBranchKeepImmutableSource) {
    const auto image = source();
    huelith::PhotoDocument document(image);
    EXPECT_FALSE(document.can_undo());
    EXPECT_TRUE(document.set_exposure(1.0));
    EXPECT_TRUE(document.set_exposure(2.0));
    EXPECT_EQ(document.undo_value(), 1.0);
    ASSERT_TRUE(document.undo());
    EXPECT_EQ(document.exposure(), 1.0);
    ASSERT_TRUE(document.redo());
    EXPECT_EQ(document.exposure(), 2.0);
    ASSERT_TRUE(document.undo());
    EXPECT_TRUE(document.set_exposure(-1.0));
    EXPECT_FALSE(document.can_redo());
    EXPECT_EQ(document.source(), image);
    EXPECT_FLOAT_EQ(image->pixels()[0].red, 0.4F);
}

TEST(PhotoHistory, RejectsInvalidOrDuplicateCommandsWithoutChangingHistory) {
    huelith::PhotoDocument document(source());
    EXPECT_FALSE(document.set_exposure(0.0));
    EXPECT_FALSE(document.can_undo());
    for (const double value : {6.0, -6.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
        EXPECT_THROW(document.set_exposure(value), std::invalid_argument);
        EXPECT_EQ(document.exposure(), 0.0);
        EXPECT_FALSE(document.can_undo());
    }
}

TEST(FloatImage, RejectsInvalidStorageAndNonFinitePixels) {
    EXPECT_FALSE(huelith::supported_photo_size(-1, 3));
    EXPECT_FALSE(huelith::supported_photo_size(65536, 65536));
    EXPECT_THROW(huelith::FloatImage(2, 1, {{0, 0, 0, 1}}), std::invalid_argument);
    EXPECT_THROW(huelith::FloatImage(1, 1, {{0, 0, 0, 2}}), std::invalid_argument);
    EXPECT_THROW(huelith::FloatImage(1, 1, {{std::numeric_limits<float>::infinity(), 0, 0, 1}}), std::invalid_argument);
}
