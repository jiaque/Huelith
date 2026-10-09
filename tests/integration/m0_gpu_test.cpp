#include "huelith/validation/gpu_probe.h"
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>

namespace huelith::validation {
TEST(M0Gpu, RemovedPrivateDeviceCanBeRecreatedWithoutLosingCpuSource) {
    const std::vector<Pixel> source{{.5F, .1F, .2F, .5F}};
    const std::vector<Pixel> backdrop{{.1F, .2F, .3F, 1.F}};
    const auto result = gpu_composite(source, backdrop, BlendMode::normal, false, true);
    EXPECT_TRUE(result.recovered_after_removal);
    EXPECT_NEAR(result.pixels[0][0], .55F, 1e-6F);
    EXPECT_FLOAT_EQ(source[0][0], .5F);
}
TEST(M0Gpu, HardwareTiledCompositeAndFreshDeviceAgreeWithCpu) {
    std::vector<Pixel> source(257 * 259), back(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        const auto alpha = static_cast<float>(i % 101) / 100.0F;
        source[i] = {1.5F * alpha, 0.3F * alpha, -0.1F * alpha, alpha};
        back[i] = {0.2F, 0.1F, 0.05F, 0.5F};
    }
    for (const auto mode : {BlendMode::normal, BlendMode::multiply, BlendMode::screen}) {
        for (const auto half : {false, true}) {
            const auto result = gpu_composite(source, back, mode, half);
            std::cout << result.adapter.toStdString() << " half=" << half
                      << " mode=" << static_cast<int>(mode) << " cold_ms=" << result.milliseconds
                      << '\n';
            ASSERT_EQ(result.pixels.size(), source.size());
            for (std::size_t i = 0; i < source.size(); ++i) {
                const auto expected = composite(source[i], back[i], mode);
                for (std::size_t c = 0; c < 4; ++c) {
                    const auto tolerance =
                        half ? 0.002F : (0.000001F + 0.00001F * std::abs(expected[c]));
                    ASSERT_NEAR(result.pixels[i][c], expected[c], tolerance)
                        << "pixel=" << i << " channel=" << c;
                }
            }
        }
    }
}
} // namespace huelith::validation
