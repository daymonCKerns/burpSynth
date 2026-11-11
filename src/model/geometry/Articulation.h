#pragma once

// Articulation: turns jaw, tongue and lip controls into the area functions of
// the pharynx and the mouth. This is what gives a burp its vowel.
//
// A simple parametric shape, not measured data. s is the position along the
// whole upper tract (0 = UES, 0.5 = velum, 1 = lips):
//   neutral(s) = 3.0 cm^2, tapering to 2.0 cm^2 over the first 10% (s < 0.1)
//   pharynx(s) = pharynxWidth                for s < 0.5, else 1
//   jaw(s)     = lerp(0.4, 1.6, jawOpening)  for s > 0.65, 1 for s < 0.5,
//                blended smoothly in between
//   tongue(s)  = 1 - 0.9 h exp(-((s - s_t) / 0.12)^2)
//                s_t = lerp(0.3, 0.9, tonguePosition),  h = tongueHeight
//   room(s)    = 1 + 0.6 h exp(-((s - s_o) / 0.15)^2)
//                s_o = s_t + 0.4 if s_t < 0.6, else s_t - 0.45
//                (the tongue's bulk leaves room at the other end of the tract)
//   area(s)    = max(neutral * pharynx * jaw * tongue * room, 0.05 cm^2)
// The last mouth section is the lip opening itself, floored at kClosedArea.
//
// Tongue back and low gives "ah", front and high gives "ee", a small lip
// opening gives "oo". Replace with measured area functions (e.g. Story,
// J. Acoust. Soc. Am. 100, 1996) when more realism is wanted.

#include "model/geometry/AreaFunction.h"

namespace burp {

inline constexpr int kTractSections = 16; // sections in each of pharynx and mouth

struct MouthShape {
    double jawOpening = 0.5;     // 0..1
    double tonguePosition = 0.5; // 0 = back, 1 = front
    double tongueHeight = 0.3;   // 0 = low, 1 = high
    double lipArea = 2.5e-4;     // m^2
    double pharynxWidth = 1.0;   // area scale
};

struct TractShapes {
    AreaFunction pharynx; // UES to velum
    AreaFunction mouth;   // velum to lips
};

TractShapes articulate(const MouthShape& shape);

} // namespace burp
