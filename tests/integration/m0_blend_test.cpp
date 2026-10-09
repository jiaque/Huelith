#include "huelith/validation/artistic_blend.h"
#include <cmath>
#include <gtest/gtest.h>

namespace huelith::validation {
TEST(M0Blend, All24ModesHaveIndependentGrayReferenceAndAlphaInvariants) {
    constexpr std::array expected{.8F, .2F,  .16F,   0.F,  0.F, .8F, .84F, 1.F,
                                  1.F, .32F, .3488F, .68F, .5F, .8F, .6F,  1.F,
                                  .6F, .68F, 0.F,    .25F, .2F, .2F, .2F,  .8F};
    for (int i = 0; i < 24; ++i) {
        SCOPED_TRACE(i);
        const auto mode = static_cast<ArtisticMode>(i);
        const auto term = artistic_term({.8F, .8F, .8F}, {.2F, .2F, .2F}, mode);
        EXPECT_NEAR(term[0], expected[static_cast<std::size_t>(i)], 1e-5F);
        const Pixel base{.1F, .2F, .3F, .5F};
        EXPECT_EQ(artistic_composite({0, 0, 0, 0}, base, mode), base);
        EXPECT_EQ(artistic_composite(base, {0, 0, 0, 0}, mode), base);
        for (const auto edge : {-1.F, 0.F, .5F, 1.F, 3.F}) {
            const auto result = artistic_composite({edge, edge, edge, .5F}, base, mode);
            EXPECT_FLOAT_EQ(result[3], .75F);
            for (const auto value : result)
                EXPECT_TRUE(std::isfinite(value));
        }
    }
}
TEST(M0Blend, NonseparableColorPreservesBackdropLuminance) {
    const auto result = artistic_term({.9F, .1F, .3F}, {.1F, .5F, .2F}, ArtisticMode::color);
    EXPECT_NEAR(.3F * result[0] + .59F * result[1] + .11F * result[2], .347F, 1e-5F);
}
TEST(M0Blend, PassThroughOpacityInterpolatesWholeGroupNotEachChild) {
    const Pixel backdrop{.2F, .2F, .2F, 1};
    const Pixel layer{.4F, .1F, .1F, .5F};
    const auto once = artistic_composite(layer, backdrop, ArtisticMode::normal);
    const auto twice = artistic_composite(layer, once, ArtisticMode::normal);
    const auto group = group_influence(backdrop, twice, .5F, 1);
    EXPECT_NEAR(group[0], .425F, 1e-6F);
    EXPECT_EQ(group_influence(backdrop, twice, 1, 0), backdrop);
    EXPECT_EQ(group_influence(backdrop, backdrop, .5F, 1), backdrop);
    auto half = layer;
    for (auto& v : half)
        v *= .5F;
    EXPECT_NE(group,
              artistic_composite(half, artistic_composite(half, backdrop, ArtisticMode::normal),
                                 ArtisticMode::normal));
    const auto isolated = artistic_composite(layer, Pixel{}, ArtisticMode::multiply);
    EXPECT_EQ(isolated, layer);
    EXPECT_NE(artistic_composite(isolated, backdrop, ArtisticMode::normal),
              artistic_composite(layer, backdrop, ArtisticMode::multiply));
}
} // namespace huelith::validation
