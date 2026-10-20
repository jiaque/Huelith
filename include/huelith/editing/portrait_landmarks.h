#pragma once
#include <array>

namespace huelith::editing::portrait_landmarks {
// OpenSeeFace-66 uses two shared mouth corners (58,62). It is NOT the
// dlib-68 mouth with the last two points removed. See upstream tracker.py,
// FeatureExtractor.update and face_3d, bundled with the exact model revision.
inline constexpr std::array outer_lip{58,48,49,50,51,52,62,53,54,55,56,57};
inline constexpr std::array inner_lip{58,59,60,61,62,63,64,65};
inline constexpr int mouth_right=58,mouth_left=62,upper_lip=50,lower_lip=55;
}
