// Reflex: the belch timeline from Reflex.h as a small state machine
// (idle, belch 1, belch 2 onward, releasing).

#include "model/control/Reflex.h"

#include <algorithm>

namespace burp {

namespace {

// Moves value a straight line of at most `step` toward target, without overshoot.
double approach(double value, double target, double step)
{
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

// Trapezoid over the wave's travel 0..1: rises over the first `fade`, falls over the last.
double waveEnvelope(double progress, double fade)
{
    return std::min({ 1.0, progress / fade, (1.0 - progress) / fade });
}

} // namespace

void Reflex::setSettings(const ReflexSettings& settings)
{
    settings_ = settings;
}

void Reflex::trigger()
{
    phase_ = Phase::Belch1;
    phaseTime_ = 0.0;
}

void Reflex::release()
{
    if (phase_ != Phase::Idle) {
        phase_ = Phase::Releasing;
    }
}

void Reflex::reset()
{
    phase_ = Phase::Idle;
    phaseTime_ = 0.0;
    output_ = ReflexOutput {};
}

const ReflexOutput& Reflex::tick(double dt, double esophagusTopPressure)
{
    phaseTime_ += dt;
    if (phase_ == Phase::Belch1
        && (esophagusTopPressure > kBelch2Pressure || phaseTime_ >= scaledTime(kFallbackDelay))) {
        phase_ = Phase::Open;
        phaseTime_ = 0.0;
    }

    // Each opening moves linearly at full range per (opening or closing) time.
    const bool lesOpening = phase_ == Phase::Belch1 || phase_ == Phase::Open;
    const bool uesOpening = phase_ == Phase::Open;
    const double lesTime = scaledTime(lesOpening ? settings_.lesOpeningTime : kLesCloseTime);
    const double uesTime = scaledTime(uesOpening ? kUesReleaseTime : settings_.releaseTime);
    output_.lesOpen = approach(output_.lesOpen, lesOpening ? 1.0 : 0.0, dt / lesTime);
    output_.uesOpen = approach(output_.uesOpen, uesOpening ? 1.0 : 0.0, dt / uesTime);

    updateWave();

    if (phase_ == Phase::Releasing && output_.lesOpen <= 0.0 && output_.uesOpen <= 0.0) {
        phase_ = Phase::Idle;
    }
    return output_;
}

bool Reflex::isIdle() const
{
    return phase_ == Phase::Idle;
}

double Reflex::scaledTime(double time) const
{
    return time / settings_.speed;
}

void Reflex::updateWave()
{
    const double travelTime = scaledTime(kWaveTravelTime);
    const double waveTime = phaseTime_ - scaledTime(kWaveDelay);
    if (phase_ != Phase::Open || waveTime < 0.0 || waveTime >= travelTime) {
        output_.waveCentre = -1.0;
        output_.waveDepth = 0.0;
        return;
    }

    // Belch 3: the centre runs from the LES end (0) to the UES end (1).
    const double progress = waveTime / travelTime;
    output_.waveCentre = progress;
    output_.waveDepth = settings_.peristalsisStrength * waveEnvelope(progress, kWaveFade);
}

} // namespace burp
