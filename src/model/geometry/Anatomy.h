#pragma once

// Anatomy: the fixed dimensions of the body, before any articulation.
//
// Reference lengths and areas are rounded adult values. Body size scales
// every length. The nasal profile is an approximate shape (narrow at the
// velum, wide in the middle, narrow at the nostrils), not measured data.
//
// Body settings change the tube grids, so BurpModel applies them only when a
// burp starts from idle.

#include "model/geometry/AreaFunction.h"

namespace burp {

inline constexpr double kPharynxLength = 0.085;       // m, UES to velum
inline constexpr double kMouthLength = 0.085;         // m, velum to lips
inline constexpr double kNasalLength = 0.11;          // m, velum to nostrils
inline constexpr double kEsophagusOpenArea = 3.0e-4;  // m^2, fully inflated
inline constexpr double kNostrilArea = 1.0e-4;        // m^2, both nostrils
inline constexpr double kMaxBodySize = 1.3;           // largest body-size scale
inline constexpr double kMaxEsophagusLength = 0.30;   // m, before body-size scaling

struct BodySettings {
    double bodySize = 1.0;         // scales every length
    double esophagusLength = 0.25; // m, before bodySize
};

class Anatomy {
public:
    void set(const BodySettings& settings);

    double esophagusLength() const; // m, after scaling
    double pharynxLength() const;
    double mouthLength() const;
    double nasalLength() const;

    // Uniform tube at kEsophagusOpenArea.
    AreaFunction esophagusShape() const;

    // Velum end to nostrils; the last section is kNostrilArea.
    AreaFunction nasalShape() const;

private:
    BodySettings settings_;
};

} // namespace burp
