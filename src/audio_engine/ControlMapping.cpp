// ControlMapping: plain parameter values to the model's SI controls, as
// described in ControlMapping.h.

#include "audio_engine/ControlMapping.h"

#include "model/math/Units.h"

#include <cmath>
#include <cstddef>

namespace burp {

namespace {

// Pitch Settle within this many ms of its maximum counts as the maximum.
constexpr double kSettleTolerance = 1.0e-6;

// Stomach Stiffness is the wall pressure at this gas volume.
constexpr double kStiffnessReferenceMl = 200.0;

std::size_t indexOf(ParamId id)
{
    return static_cast<std::size_t>(paramIndex(id));
}

double valueOf(const ParamValues& plainValues, ParamId id)
{
    return plainValues[indexOf(id)];
}

} // namespace

BurpControls makeControls(const ParamValues& plainValues)
{
    const auto get = [&plainValues](ParamId id) { return valueOf(plainValues, id); };

    BurpControls controls;
    controls.abdominalPressure = mmHgToPa(get(ParamId::AbdominalPressure));
    controls.mouth.jawOpening = get(ParamId::JawOpening);
    controls.mouth.tonguePosition = get(ParamId::TonguePosition);
    controls.mouth.tongueHeight = get(ParamId::TongueHeight);
    controls.mouth.lipArea = cm2ToM2(get(ParamId::LipOpening));
    controls.mouth.pharynxWidth = get(ParamId::PharynxWidth);
    controls.velumArea = cm2ToM2(get(ParamId::Velum));
    controls.pitchBendSemitones = get(ParamId::PitchBend) * get(ParamId::BendRange);

    controls.gasVolume = mlToM3(get(ParamId::GasAmount));
    controls.lesOpeningArea = cm2ToM2(get(ParamId::LesOpeningArea));
    controls.reflex.lesOpeningTime = msToS(get(ParamId::LesOpeningTime));
    controls.reflex.releaseTime = msToS(get(ParamId::ReleaseTime));
    controls.reflex.speed = get(ParamId::ReflexSpeed);
    controls.reflex.peristalsisStrength = get(ParamId::PeristalsisStrength);
    controls.reflex.burstTime = msToS(get(ParamId::BurstLength));
    controls.reflex.sustainStrength = get(ParamId::SustainStrength);
    controls.uesMassScale = get(ParamId::UesMass);
    controls.uesDampingRatio = get(ParamId::UesDamping);
    controls.uesRestArea = cm2ToM2(get(ParamId::UesRestingGap));
    controls.roughness = get(ParamId::Roughness);
    controls.breathiness = get(ParamId::Breathiness);
    controls.wallSoftness = get(ParamId::WallSoftness);
    controls.gasMix = get(ParamId::GasMix);
    controls.glissandoRate = get(ParamId::GlissandoRate);

    const double settle = get(ParamId::PitchSettle);
    const double settleMax = paramTable()[indexOf(ParamId::PitchSettle)].max;
    controls.pitchSettleTime = msToS(settle);
    controls.pitchAlwaysCorrect = settle >= settleMax - kSettleTolerance;
    controls.accentPressure = mmHgToPa(get(ParamId::AccentAmount));

    controls.body.bodySize = get(ParamId::BodySize);
    controls.body.esophagusLength = cmToM(get(ParamId::EsophagusLength));
    controls.stomachElastance =
        mmHgToPa(get(ParamId::StomachStiffness)) / mlToM3(kStiffnessReferenceMl);
    controls.esophagusStiffness = get(ParamId::EsophagusStiffness);
    return controls;
}

double outputGain(const ParamValues& plainValues)
{
    return std::pow(10.0, valueOf(plainValues, ParamId::OutputLevel) / 20.0);
}

} // namespace burp
