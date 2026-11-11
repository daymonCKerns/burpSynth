#pragma once

// The plugin's parameters: IDs, names, units, ranges and defaults.
//
// This table is the single source of truth. The plugin controller registers
// host parameters from it, the editor builds its sliders from it, and
// ControlMapping converts the values to the model's SI units. No VST3 types
// here.
//
// The numeric IDs are stored in DAW projects and automation. NEVER change or
// reuse one. New parameters get new numbers.
//
// The host works in normalised values (0..1); "plain" values are in the
// units shown to the user. The mapping is linear.

#include <array>

namespace burp {

enum class ParamId : int {
    // Live: meant to be moved during a burp.
    AbdominalPressure = 0,
    JawOpening = 1,
    TonguePosition = 2,
    TongueHeight = 3,
    LipOpening = 4,
    Velum = 5,
    PitchBend = 6,

    // Character: set per patch.
    GasAmount = 10,
    LesOpeningTime = 11,
    LesOpeningArea = 12,
    UesMass = 13,
    UesDamping = 14,
    UesRestingGap = 15,
    Roughness = 16,
    Breathiness = 17,
    WallSoftness = 18,
    PeristalsisStrength = 19,
    ReflexSpeed = 20,
    GasMix = 21,
    OutputLevel = 22,
    BendRange = 23,
    GlissandoRate = 24,
    PitchSettle = 25,
    ReleaseTime = 26,
    AccentAmount = 27,

    // Body: anatomy, applied when a burp starts.
    BodySize = 40,
    StomachStiffness = 41,
    EsophagusLength = 42,
    EsophagusStiffness = 43,
    PharynxWidth = 44,
};

inline constexpr int kParamCount = 30;

struct ParamInfo {
    ParamId id;
    const char* name;
    const char* units;
    double min;
    double max;
    double defaultValue;
};

// One plain value per parameter, in table order.
using ParamValues = std::array<double, kParamCount>;

// Rows in the order of docs/implementation_plan.md, section 6.
const std::array<ParamInfo, kParamCount>& paramTable();

// Table index of an ID, or -1 if there is no such parameter.
int paramIndex(ParamId id);
int paramIndex(int rawId);

double toPlain(const ParamInfo& info, double normalized); // clamps to 0..1 first
double toNormalized(const ParamInfo& info, double plain);

// Every parameter at its default.
ParamValues defaultParamValues();

} // namespace burp
