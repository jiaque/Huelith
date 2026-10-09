#include "huelith/document/canvas_spec.h"
#include <gtest/gtest.h>
#include <limits>

TEST(CanvasSpec, AcceptsDefaultsAndUnicodeName) {
    huelith::CanvasSpec spec;
    spec.name = "风景 产品 人像";
    EXPECT_FALSE(huelith::validate(spec));
}

TEST(CanvasSpec, RejectsEmptyNames) {
    huelith::CanvasSpec spec;
    spec.name.clear();
    EXPECT_EQ(huelith::validate(spec), huelith::CanvasSpecError::empty_name);
}

TEST(CanvasSpec, RejectsAsciiWhitespaceOnlyNamesAtTheCoreBoundary) {
    huelith::CanvasSpec spec;
    spec.name = " \t\r\n";
    EXPECT_EQ(huelith::validate(spec), huelith::CanvasSpecError::empty_name);
}

TEST(CanvasSpec, RejectsInvalidDimensionsWithoutAllocatingPixels) {
    for (const auto value : {0, -1, huelith::max_canvas_dimension + 1}) {
        huelith::CanvasSpec spec;
        spec.width = value;
        EXPECT_EQ(huelith::validate(spec), huelith::CanvasSpecError::invalid_dimensions);
        spec.width = 1;
        spec.height = value;
        EXPECT_EQ(huelith::validate(spec), huelith::CanvasSpecError::invalid_dimensions);
    }
    huelith::CanvasSpec spec;
    spec.width = spec.height = huelith::max_canvas_dimension;
    EXPECT_FALSE(huelith::validate(spec));
}

TEST(CanvasSpec, RejectsNonFiniteAndNonPositiveResolution) {
    for (const double value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        huelith::CanvasSpec spec;
        spec.resolution_ppi = value;
        EXPECT_EQ(huelith::validate(spec), huelith::CanvasSpecError::invalid_resolution);
    }
}

TEST(CanvasSpec, RejectsUnknownSerializedEnumValues) {
    huelith::CanvasSpec spec;
    spec.background = static_cast<huelith::CanvasBackground>(99);
    EXPECT_EQ(huelith::validate(spec), huelith::CanvasSpecError::invalid_enum);
    spec.background = huelith::CanvasBackground::white;
    spec.working_space = static_cast<huelith::WorkingSpace>(99);
    EXPECT_EQ(huelith::validate(spec), huelith::CanvasSpecError::invalid_enum);
}
