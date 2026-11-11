#pragma once

// The edge between the host's parameter units and the model's SI units.
// This is the only place plain parameter values (mmHg, cm^2, mL, ms) are
// converted (math/Units.h).
//
// Derived values:
//   pitchBendSemitones = PitchBend * BendRange
//   stomachElastance   = StomachStiffness (as Pa) / 200 mL
//   pitchAlwaysCorrect = PitchSettle at its maximum
//   wallSoftness, gas mix, 0..1 shape controls pass through unchanged
// OutputLevel is not part of the model; the engine applies it.

#include "audio_engine/Params.h"
#include "model/BurpControls.h"

namespace burp {

BurpControls makeControls(const ParamValues& plainValues);

// Linear gain for the OutputLevel parameter: 10^(dB / 20).
double outputGain(const ParamValues& plainValues);

} // namespace burp
