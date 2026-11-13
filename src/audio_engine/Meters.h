#pragma once

// Meters: read-only values the engine reports for display (the editor's body
// view). They are not parameters: never automated, saved, or sent to the
// engine. The processor sends them to the controller as VST3 output
// parameters once per block, and the editor reads them from the controller.
//
// Every value is 0..1:
//   StomachFill  stomach gas volume / kFullStomach
//   LesOpen      the reflex's LES opening
//   UesOpen      the reflex's UES opening (not the UES's vibration)
//   LesFlow      flow through the LES (50 ms average) / kFullFlow; backflow shows as 0
//   UesFlow      the same through the UES
//   WaveCentre   peristalsis wave centre along the esophagus, 0 = LES end
//   WaveDepth    depth of the contraction; 0 = no wave
//   Level        output peak since the last report (full scale = 1)
//
// IDs start at 100, clear of ParamId. Never reuse one.
//
// Header-only. No VST3 types here.

#include <array>

namespace burp {

enum class MeterId : int {
    StomachFill = 100,
    LesOpen = 101,
    UesOpen = 102,
    LesFlow = 103,
    UesFlow = 104,
    WaveCentre = 105,
    WaveDepth = 106,
    Level = 107,
};

inline constexpr int kMeterCount = 8;
inline constexpr double kFullStomach = 500.0e-6; // m^3, the top of Gas Amount's range
inline constexpr double kFullFlow = 1.5e-3;       // m^3/s

struct MeterInfo {
    MeterId id;
    const char* name;
};

inline constexpr std::array<MeterInfo, kMeterCount> kMeters {{
    { MeterId::StomachFill, "Stomach Fill" },
    { MeterId::LesOpen, "LES Open" },
    { MeterId::UesOpen, "UES Open" },
    { MeterId::LesFlow, "LES Flow" },
    { MeterId::UesFlow, "UES Flow" },
    { MeterId::WaveCentre, "Wave Centre" },
    { MeterId::WaveDepth, "Wave Depth" },
    { MeterId::Level, "Level" },
}};

// One value per meter, in kMeters order.
using MeterValues = std::array<double, kMeterCount>;

} // namespace burp
