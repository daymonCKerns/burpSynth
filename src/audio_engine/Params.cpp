// Params: the parameter table and the normalised/plain conversions from
// Params.h. The rows must match docs/implementation_plan.md, section 6.

#include "audio_engine/Params.h"

#include <algorithm>
#include <cstddef>

namespace burp {

const std::array<ParamInfo, kParamCount>& paramTable()
{
    static const std::array<ParamInfo, kParamCount> table {{
        {ParamId::AbdominalPressure, "Abdominal Pressure", "mmHg", 0.0, 100.0, 10.0},
        {ParamId::JawOpening, "Jaw Opening", "", 0.0, 1.0, 0.5},
        {ParamId::TonguePosition, "Tongue Position", "", 0.0, 1.0, 0.5},
        {ParamId::TongueHeight, "Tongue Height", "", 0.0, 1.0, 0.3},
        {ParamId::LipOpening, "Lip Opening", "cm2", 0.0, 5.0, 2.5},
        {ParamId::Velum, "Velum", "cm2", 0.0, 1.5, 0.0},
        {ParamId::PitchBend, "Pitch Bend", "", -1.0, 1.0, 0.0},
        {ParamId::GasAmount, "Gas Amount", "mL", 20.0, 500.0, 200.0},
        {ParamId::LesOpeningTime, "LES Opening Time", "ms", 10.0, 300.0, 40.0},
        {ParamId::LesOpeningArea, "LES Opening Area", "cm2", 0.1, 2.0, 0.8},
        {ParamId::UesMass, "UES Mass", "x", 0.5, 8.0, 3.0},
        {ParamId::UesDamping, "UES Damping", "", 0.02, 0.5, 0.12},
        {ParamId::UesRestingGap, "UES Resting Gap", "cm2", -0.05, 0.2, 0.05},
        {ParamId::Roughness, "Roughness", "", 0.0, 1.0, 0.3},
        {ParamId::Breathiness, "Breathiness", "", 0.0, 1.0, 0.3},
        {ParamId::WallSoftness, "Wall Softness", "", 0.0, 1.0, 0.5},
        {ParamId::PeristalsisStrength, "Peristalsis Strength", "", 0.0, 1.0, 0.3},
        {ParamId::ReflexSpeed, "Reflex Speed", "x", 0.5, 2.0, 1.0},
        {ParamId::GasMix, "Gas Mix", "", 0.0, 1.0, 0.0},
        {ParamId::OutputLevel, "Output Level", "dB", -60.0, 12.0, 0.0},
        {ParamId::BendRange, "Bend Range", "st", 0.0, 12.0, 2.0},
        {ParamId::GlissandoRate, "Glissando Rate", "st/s", 1.0, 500.0, 60.0},
        {ParamId::PitchSettle, "Pitch Settle", "ms", 0.0, 2000.0, 150.0},
        {ParamId::ReleaseTime, "Release Time", "ms", 30.0, 300.0, 80.0},
        {ParamId::AccentAmount, "Accent Amount", "mmHg", 0.0, 50.0, 15.0},
        {ParamId::BodySize, "Body Size", "x", 0.7, 1.3, 1.0},
        {ParamId::StomachStiffness, "Stomach Stiffness", "mmHg", 2.0, 30.0, 10.0},
        {ParamId::EsophagusLength, "Esophagus Length", "cm", 18.0, 30.0, 25.0},
        {ParamId::EsophagusStiffness, "Esophagus Stiffness", "", 0.0, 1.0, 0.7},
        {ParamId::PharynxWidth, "Pharynx Width", "x", 0.5, 1.5, 1.0},
    }};
    return table;
}

int paramIndex(ParamId id)
{
    return paramIndex(static_cast<int>(id));
}

int paramIndex(int rawId)
{
    const auto& table = paramTable();
    for (int i = 0; i < kParamCount; ++i) {
        if (static_cast<int>(table[static_cast<std::size_t>(i)].id) == rawId) {
            return i;
        }
    }
    return -1;
}

double toPlain(const ParamInfo& info, double normalized)
{
    return info.min + std::clamp(normalized, 0.0, 1.0) * (info.max - info.min);
}

double toNormalized(const ParamInfo& info, double plain)
{
    return std::clamp((plain - info.min) / (info.max - info.min), 0.0, 1.0);
}

ParamValues defaultParamValues()
{
    const auto& table = paramTable();
    ParamValues values {};
    for (std::size_t i = 0; i < table.size(); ++i) {
        values[i] = table[i].defaultValue;
    }
    return values;
}

} // namespace burp
