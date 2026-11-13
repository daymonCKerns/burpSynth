// Articulation: the parametric pharynx and mouth shapes from Articulation.h.
// Each factor in the header's formula is its own small function.

#include "model/geometry/Articulation.h"

#include "model/math/Acoustics.h"
#include "model/math/Smooth.h"
#include "model/math/Units.h"

#include <algorithm>
#include <cmath>

namespace burp {

namespace {

constexpr double kMinTractArea = cm2ToM2(0.05); // m^2, floor on every section

// neutral(s) = 3.0 cm^2, tapering linearly to 2.0 cm^2 at the UES over s < 0.1
double neutralArea(double s)
{
    return cm2ToM2(lerp(2.0, 3.0, std::min(s / 0.1, 1.0)));
}

// pharynx(s) = pharynxWidth for s < 0.5, else 1
double pharynxFactor(double s, double pharynxWidth)
{
    return s < 0.5 ? pharynxWidth : 1.0;
}

// jaw(s) = 1 for s < 0.5, lerp(0.4, 1.6, jawOpening) for s > 0.65, smoothstep blend between
double jawFactor(double s, double jawOpening)
{
    return lerp(1.0, lerp(0.4, 1.6, jawOpening), smoothstep((s - 0.5) / 0.15));
}

// s_t = lerp(0.3, 0.9, tonguePosition)
double tongueCentre(double tonguePosition)
{
    return lerp(0.3, 0.9, tonguePosition);
}

// tongue(s) = 1 - 0.9 h exp(-((s - s_t) / 0.12)^2)
double tongueFactor(double s, double centre, double height)
{
    const double u = (s - centre) / 0.12;
    return 1.0 - 0.9 * height * std::exp(-u * u);
}

// bump(s, c) = exp(-((s - c) / 0.15)^2)
double roomBump(double s, double centre)
{
    const double u = (s - centre) / 0.15;
    return std::exp(-u * u);
}

// w = smoothstep((0.6 - s_t) / 0.15): 1 for s_t <= 0.45, 0 for s_t >= 0.6
double frontRoomWeight(double tongue)
{
    return smoothstep((0.6 - tongue) / 0.15);
}

// room(s) = 1 + 0.6 h (w bump(s, s_t + 0.4) + (1 - w) bump(s, s_t - 0.45))
double roomFactor(double s, double tongue, double height)
{
    const double w = frontRoomWeight(tongue);
    return 1.0 + 0.6 * height * (w * roomBump(s, tongue + 0.4) + (1.0 - w) * roomBump(s, tongue - 0.45));
}

// area(s) = max(neutral * pharynx * jaw * tongue * room, 0.05 cm^2), inputs already clamped
double tractArea(double s, const MouthShape& shape)
{
    const double tongue = tongueCentre(shape.tonguePosition);
    const double area = neutralArea(s) * pharynxFactor(s, shape.pharynxWidth)
                        * jawFactor(s, shape.jawOpening)
                        * tongueFactor(s, tongue, shape.tongueHeight)
                        * roomFactor(s, tongue, shape.tongueHeight);
    return std::max(area, kMinTractArea);
}

} // namespace

TractShapes articulate(const MouthShape& shape)
{
    MouthShape clamped = shape;
    clamped.jawOpening = std::clamp(shape.jawOpening, 0.0, 1.0);
    clamped.tonguePosition = std::clamp(shape.tonguePosition, 0.0, 1.0);
    clamped.tongueHeight = std::clamp(shape.tongueHeight, 0.0, 1.0);

    TractShapes shapes;
    shapes.pharynx.count = kTractSections;
    shapes.mouth.count = kTractSections;
    for (int i = 0; i < kTractSections; ++i) {
        const double fraction = 0.5 * (i + 0.5) / kTractSections;
        shapes.pharynx.area[i] = tractArea(fraction, clamped);
        shapes.mouth.area[i] = tractArea(0.5 + fraction, clamped);
    }

    // The last mouth section is the lip opening itself.
    shapes.mouth.area[kTractSections - 1] = std::max(shape.lipArea, kClosedArea);
    return shapes;
}

} // namespace burp
