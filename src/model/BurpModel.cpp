// BurpModel: the wiring and the two-rate loop from BurpModel.h.
//
// The per-sample section reads top to bottom as the signal path: stomach,
// LES, esophagus, UES, pharynx, join and velum, mouth and nasal, lips and
// nostrils. Keep it that way; it is the fixed topology.

#include "model/BurpModel.h"

#include "model/geometry/AreaFunction.h"
#include "model/geometry/Articulation.h"
#include "model/geometry/Peristalsis.h"
#include "model/geometry/TubeLaw.h"
#include "model/math/Gas.h"
#include "model/math/Smooth.h"
#include "model/math/Units.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <numbers>

namespace burp {

void BurpModel::prepare(double sampleRate)
{
    dt_ = 1.0 / sampleRate;
    controlInterval_ = std::max(1, static_cast<int>(std::lround(kControlPeriod * sampleRate)));
    controlDt_ = static_cast<double>(controlInterval_) * dt_;
    tailSamples_ = static_cast<int>(std::lround(kTailTime * sampleRate));
    fadeSamples_ = std::max(1, static_cast<int>(std::lround(kSafetyFade * sampleRate)));

    esophagus_.prepare(kMaxEsophagusLength * kMaxBodySize, sampleRate);
    pharynx_.prepare(kPharynxLength * kMaxBodySize, sampleRate);
    mouth_.prepare(kMouthLength * kMaxBodySize, sampleRate);
    nasal_.prepare(kNasalLength * kMaxBodySize, sampleRate);
    ues_.prepare(sampleRate);
    pitch_.prepare(sampleRate);

    les_.setChannelLength(kLesChannelLength);
    velum_.setChannelLength(kVelumChannelLength);
    join_.resistance = 0.0;

    for (OnePole* smoother : { &abdominalSmoother_, &jawSmoother_, &tonguePositionSmoother_,
                               &tongueHeightSmoother_, &lipAreaSmoother_, &pharynxWidthSmoother_,
                               &velumSmoother_, &bendSmoother_ }) {
        smoother->setTime(kControlSmoothingTime, controlDt_);
    }

    // A one-pole fed unit-variance white noise has variance (1 - a) / (1 + a).
    noiseSmoother_.setTime(kRoughnessTime, controlDt_);
    const double noiseCoefficient = onePoleCoefficient(kRoughnessTime, controlDt_);
    noiseScale_ = std::sqrt((1.0 + noiseCoefficient) / (1.0 - noiseCoefficient));

    averageCoefficient_ = onePoleCoefficient(kPressureAverageTime, controlDt_);
    accentCoefficient_ = onePoleCoefficient(kAccentDecay, controlDt_);

    esophagusAverage_ = Eigen::ArrayXd::Zero(esophagus_.pressures().size());

    applyBody();
    reset();
}

void BurpModel::reset()
{
    resetAcoustics();
    reflex_.reset();
    pitch_.reset();
    accent_ = 0.0;
    snapSmoothers();
    idleSamples_ = 0;
    tickPending_ = false;
    asleep_ = true;
}

void BurpModel::setControls(const BurpControls& controls)
{
    controls_ = controls;
}

void BurpModel::trigger(double semitones, double velocity)
{
    if (asleep_ || reflex_.isIdle()) {
        applyBody();
        resetAcoustics();
        snapSmoothers();
    }

    accent_ = 0.0;
    stomach_.setExternalPressure(abdominalSmoother_.value());
    stomach_.fill(controls_.gasVolume * lerp(0.3, 1.0, std::clamp(velocity, 0.0, 1.0)));
    reflex_.setSettings(controls_.reflex);
    reflex_.trigger();
    pitch_.startNote(semitones);

    idleSamples_ = 0;
    tickPending_ = true;
    asleep_ = false;
}

void BurpModel::changeNote(double semitones, double velocity)
{
    pitch_.changeNote(semitones);
    accent_ += std::clamp(velocity, 0.0, 1.0) * controls_.accentPressure;
}

void BurpModel::release()
{
    reflex_.release();
}

bool BurpModel::isAsleep() const
{
    return asleep_;
}

void BurpModel::render(double* out, int numFrames)
{
    if (tickPending_) {
        controlTick();
        tickPending_ = false;
        samplesUntilTick_ = controlInterval_;
    }

    for (int i = 0; i < numFrames; ++i) {
        if (asleep_) {
            std::fill(out + i, out + numFrames, 0.0);
            return;
        }

        const double fade = std::clamp(
            1.0 - static_cast<double>(idleSamples_ - tailSamples_) / static_cast<double>(fadeSamples_),
            0.0, 1.0);
        out[i] = fade * stepSample();

        if (--samplesUntilTick_ == 0) {
            controlTick();
            samplesUntilTick_ = controlInterval_;
        }

        idleSamples_ = reflex_.isIdle() ? idleSamples_ + 1 : 0;
        if (idleSamples_ >= tailSamples_ + fadeSamples_) {
            resetAcoustics();
            asleep_ = true;
        }
    }
}

double BurpModel::stomachPressure() const
{
    return stomach_.pressure();
}

double BurpModel::stomachMoles() const
{
    return stomach_.moles();
}

double BurpModel::measuredHz() const
{
    return pitch_.measuredHz();
}

double BurpModel::targetHz() const
{
    return pitch_.targetHz();
}

// Gas first: Tube::configure grids each tube with the current speed of sound.
void BurpModel::applyBody()
{
    const Gas gas = mixGases(kAir, kCarbonDioxide, controls_.gasMix);
    density_ = gasDensity(gas, kAtmosphericPressure, kBodyTemperature);
    const double soundSpeed = speedOfSound(gas, kBodyTemperature);
    outputScale_ = density_ / (4.0 * std::numbers::pi * kListenerDistance);

    les_.setGas(density_, kViscosity);
    esophagus_.setGas(density_, soundSpeed, kViscosity);
    ues_.setGas(density_, kViscosity);
    pharynx_.setGas(density_, soundSpeed, kViscosity);
    velum_.setGas(density_, kViscosity);
    mouth_.setGas(density_, soundSpeed, kViscosity);
    nasal_.setGas(density_, soundSpeed, kViscosity);
    lips_.setGas(density_, soundSpeed);
    nostrils_.setGas(density_, soundSpeed);

    anatomy_.set(controls_.body);

    AreaFunction esophagusRest = anatomy_.esophagusShape();
    esophagusRest.scale(esophagusCollapsedFraction());
    const TractShapes tract = articulate(controls_.mouth);

    esophagus_.configure(anatomy_.esophagusLength(), esophagusRest);
    pharynx_.configure(anatomy_.pharynxLength(), tract.pharynx);
    mouth_.configure(anatomy_.mouthLength(), tract.mouth);
    nasal_.configure(anatomy_.nasalLength(), anatomy_.nasalShape());

    stomach_.setWallElastance(controls_.stomachElastance);
}

void BurpModel::resetAcoustics()
{
    stomach_.reset();
    les_.reset();
    esophagus_.reset();
    ues_.reset();
    pharynx_.reset();
    join_.flow = 0.0;
    velum_.reset();
    mouth_.reset();
    nasal_.reset();
    lips_.reset();
    nostrils_.reset();
    esophagusAverage_.setZero();
}

void BurpModel::snapSmoothers()
{
    abdominalSmoother_.reset(controls_.abdominalPressure);
    jawSmoother_.reset(controls_.mouth.jawOpening);
    tonguePositionSmoother_.reset(controls_.mouth.tonguePosition);
    tongueHeightSmoother_.reset(controls_.mouth.tongueHeight);
    lipAreaSmoother_.reset(controls_.mouth.lipArea);
    pharynxWidthSmoother_.reset(controls_.mouth.pharynxWidth);
    velumSmoother_.reset(controls_.velumArea);
    bendSmoother_.reset(controls_.pitchBendSemitones);
    noiseSmoother_.reset(0.0);
}

// docs/implementation_plan.md, section 3.6.
void BurpModel::controlTick()
{
    // 1. Live controls.
    const double abdominal = abdominalSmoother_.next(controls_.abdominalPressure);
    MouthShape mouthShape;
    mouthShape.jawOpening = jawSmoother_.next(controls_.mouth.jawOpening);
    mouthShape.tonguePosition = tonguePositionSmoother_.next(controls_.mouth.tonguePosition);
    mouthShape.tongueHeight = tongueHeightSmoother_.next(controls_.mouth.tongueHeight);
    mouthShape.lipArea = lipAreaSmoother_.next(controls_.mouth.lipArea);
    mouthShape.pharynxWidth = pharynxWidthSmoother_.next(controls_.mouth.pharynxWidth);
    const double velumArea = velumSmoother_.next(controls_.velumArea);
    const double bend = bendSmoother_.next(controls_.pitchBendSemitones);

    // 2. Slow average of the esophagus pressures: avg += (1 - a) (P - avg).
    const int cells = esophagus_.cellCount();
    esophagusAverage_.head(cells)
        += (1.0 - averageCoefficient_) * (esophagus_.pressures().head(cells) - esophagusAverage_.head(cells));

    // 3. Reflex, driven by the pressure just under the UES.
    reflex_.setSettings(controls_.reflex);
    const ReflexOutput& belch = reflex_.tick(controlDt_, esophagusAverage_(cells - 1));

    // 4. Stomach squeeze.
    accent_ *= accentCoefficient_;
    stomach_.setExternalPressure(abdominal + accent_);

    // 5. Valve targets.
    les_.setTargetArea(belch.lesOpen * controls_.lesOpeningArea, controlInterval_);
    velum_.setTargetArea(velumArea, controlInterval_);

    // 6. UES tension, perturbed by roughness.
    pitch_.setSettings({ bend, controls_.glissandoRate, controls_.pitchSettleTime,
                         controls_.pitchAlwaysCorrect });
    const double slowNoise = noiseScale_ * noiseSmoother_.next(gaussian_(random_));
    const double tension = pitch_.tick(controlDt_) * (1.0 + kRoughnessDepth * controls_.roughness * slowNoise);
    ues_.setSettings({ controls_.uesMassScale, controls_.uesDampingRatio, controls_.uesRestArea, tension,
                       belch.uesOpen });
    ues_.setTurbulenceGain(controls_.breathiness);

    // 7. Shapes: articulation for the upper tract; tube law and peristalsis
    // for the esophagus, sampling the averaged pressure at the nearest cell.
    const TractShapes tract = articulate(mouthShape);
    pharynx_.setTargetShape(tract.pharynx, controlInterval_);
    mouth_.setTargetShape(tract.mouth, controlInterval_);
    lips_.setArea(mouthShape.lipArea);
    nostrils_.setArea(kNostrilArea);

    const double collapsed = esophagusCollapsedFraction();
    AreaFunction esophagusTarget;
    esophagusTarget.fill(kEsophagusSections, 0.0);
    for (int i = 0; i < kEsophagusSections; ++i) {
        const double x = (static_cast<double>(i) + 0.5) / static_cast<double>(kEsophagusSections);
        const int cell = std::min(cells - 1, static_cast<int>(x * static_cast<double>(cells)));
        const double wave = belch.waveCentre >= 0.0
                                ? peristalsisFactor(x, belch.waveCentre, kPeristalsisWidth, belch.waveDepth)
                                : 1.0;
        esophagusTarget.area[static_cast<std::size_t>(i)]
            = kEsophagusOpenArea
              * tubeLawOpenFraction(esophagusAverage_(cell), kEsophagusOpeningPressure, collapsed) * wave;
    }
    esophagus_.setTargetShape(esophagusTarget, controlInterval_);

    // 8. Wall yielding and the connectors' neighbour inertances.
    const double wallScale = 2.0 * controls_.wallSoftness;
    esophagus_.setWallScale(wallScale);
    pharynx_.setWallScale(wallScale);
    mouth_.setWallScale(wallScale);
    nasal_.setWallScale(wallScale);

    les_.setNeighbourInertance(esophagus_.inletHalfInertance());
    ues_.setNeighbourInertance(esophagus_.outletHalfInertance() + pharynx_.inletHalfInertance());
    join_.inertance = pharynx_.outletHalfInertance() + mouth_.inletHalfInertance();
    velum_.setNeighbourInertance(pharynx_.outletHalfInertance() + nasal_.inletHalfInertance());
    lips_.setNeighbourInertance(mouth_.outletHalfInertance());
    nostrils_.setNeighbourInertance(nasal_.outletHalfInertance());
}

// docs/implementation_plan.md, section 3.5.
double BurpModel::stepSample()
{
    // 1. Flows from the current pressures.
    const double uLes = les_.step(dt_, stomach_.pressure(), esophagus_.inletPressure());
    esophagus_.stepLinks(dt_);
    const double uUes = ues_.step(dt_, esophagus_.outletPressure(), pharynx_.inletPressure());
    pharynx_.stepLinks(dt_);
    const double uJoin = join_.step(dt_, pharynx_.outletPressure(), mouth_.inletPressure());
    const double uVelum = velum_.step(dt_, pharynx_.outletPressure(), nasal_.inletPressure());
    mouth_.stepLinks(dt_);
    nasal_.stepLinks(dt_);
    const double uLips = lips_.step(dt_, mouth_.outletPressure());
    const double uNose = nostrils_.step(dt_, nasal_.outletPressure());

    // 2. Each connector's flow leaves its upstream cell and enters its downstream cell.
    stomach_.addOutflow(uLes);
    esophagus_.addInletFlow(uLes);
    esophagus_.addOutletFlow(uUes);
    pharynx_.addInletFlow(uUes);
    pharynx_.addOutletFlow(uJoin + uVelum);
    mouth_.addInletFlow(uJoin);
    nasal_.addInletFlow(uVelum);
    mouth_.addOutletFlow(uLips);
    nasal_.addOutletFlow(uNose);

    // 3. Pressures.
    stomach_.step(dt_);
    esophagus_.stepCells(dt_);
    pharynx_.stepCells(dt_);
    mouth_.stepCells(dt_);
    nasal_.stepCells(dt_);

    // 4. Pitch tracking.
    pitch_.observe(ues_.lowerDisplacement());

    // 5. Radiated sound pressure at the listener.
    return outputScale_ * (lips_.flowDerivative() + nostrils_.flowDerivative());
}

// Fraction of the open area left at zero pressure: 1 = rigid open pipe.
double BurpModel::esophagusCollapsedFraction() const
{
    return lerp(0.1, 1.0, controls_.esophagusStiffness);
}

} // namespace burp
