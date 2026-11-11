#pragma once

// Area function: the cross-sectional area of a tube at evenly spaced points
// along its length. This is the only geometry the 1D acoustics needs.
//
// Sections run from the inlet (stomach side) to the outlet (mouth side) and
// are cell-centred: section i covers positions i/count to (i+1)/count.
// Fixed capacity, so shapes can be built on the audio thread.
//
// Header-only: every function is defined inline in this file.

#include "model/math/Smooth.h"

#include <algorithm>
#include <array>

namespace burp {

struct AreaFunction {
    static constexpr int kMaxSections = 64;

    std::array<double, kMaxSections> area {}; // m^2
    int count = 0;

    // Sets `sections` sections (clamped to kMaxSections) to one area.
    void fill(int sections, double value);

    // Area at a normalised position 0..1, linearly interpolated between
    // section centres and held constant beyond the first and last centre.
    double sample(double position) const;

    // Multiplies every section.
    void scale(double factor);
};

inline void AreaFunction::fill(int sections, double value)
{
    count = std::clamp(sections, 0, kMaxSections);
    std::fill_n(area.begin(), count, value);
}

inline double AreaFunction::sample(double position) const
{
    if (count == 0) {
        return 0.0;
    }

    // Section i is centred at (i + 0.5) / count, so x is the position in
    // section-index units measured from the first centre.
    const double x = position * count - 0.5;
    if (x <= 0.0) {
        return area[0];
    }
    if (x >= count - 1) {
        return area[count - 1];
    }
    const int i = static_cast<int>(x);
    return lerp(area[i], area[i + 1], x - i);
}

inline void AreaFunction::scale(double factor)
{
    for (int i = 0; i < count; ++i) {
        area[i] *= factor;
    }
}

} // namespace burp
