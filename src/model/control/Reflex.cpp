// Reflex: the belch timeline from Reflex.h as a small state machine
// (idle, belch 1, burst, sustain, releasing).

#include "model/control/Reflex.h"

#include "model/math/Smooth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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
    waveTime_ = -1.0;
}

void Reflex::release()
{
    if (phase_ != Phase::Idle) {
        phase_ = Phase::Releasing;
        phaseTime_ = 0.0;
    }
}

void Reflex::reset()
{
    phase_ = Phase::Idle;
    phaseTime_ = 0.0;
    baseOpen_ = 0.0;
    waveTime_ = -1.0;
    output_ = ReflexOutput {};
}

const ReflexOutput& Reflex::tick(double dt, double esophagusTopPressure)
{
    phaseTime_ += dt;
    bubbleTime_ += dt;
    if (waveTime_ >= 0.0) {
        waveTime_ += dt;
    }

    if (phase_ == Phase::Belch1
        && (esophagusTopPressure > kBelch2Pressure || phaseTime_ >= scaledTime(kFallbackDelay))) {
        phase_ = Phase::Open;
        phaseTime_ = 0.0;
        waveTime_ = 0.0;
    } else if (phase_ == Phase::Open && phaseTime_ >= scaledTime(settings_.burstTime)) {
        phase_ = Phase::Sustain;
        phaseTime_ = 0.0;
        baseOpen_ = output_.lesOpen;
        bubbleTime_ = 0.0;
        bubbleGap_ = nextBubbleGap();
    }

    // Each opening moves linearly at full range per (opening or closing) time.
    const bool sustaining = isSustaining();
    const bool lesOpening = phase_ == Phase::Belch1 || phase_ == Phase::Open;
    if (phase_ == Phase::Sustain) {
        const double settleTime = scaledTime(sustaining ? kSettleTime : kLesCloseTime);
        baseOpen_ = approach(baseOpen_, sustainOpening(), dt / settleTime);
        output_.lesOpen = baseOpen_ * (1.0 + kBubbleHeight * bubbleSwell());
    } else {
        const double lesTime = scaledTime(lesOpening ? settings_.lesOpeningTime : kLesCloseTime);
        output_.lesOpen = approach(output_.lesOpen, lesOpening ? 1.0 : 0.0, dt / lesTime);
    }

    // Without sustain, after a burst the UES holds its opening until the gas
    // trapped under it has drained (or kMaxDrainTime passes). Closing it on a
    // full esophagus makes the trapped gas pop it open again and again: harsh
    // clicks.
    const bool uesOpening = phase_ == Phase::Open || sustaining;
    const double uesTime = scaledTime(uesOpening ? kUesReleaseTime : settings_.releaseTime);
    const bool closing = (phase_ == Phase::Sustain && !sustaining) || phase_ == Phase::Releasing;
    const bool draining = closing && esophagusTopPressure > kDrainedPressure
                          && phaseTime_ < scaledTime(kMaxDrainTime);
    if (!draining) {
        output_.uesOpen = approach(output_.uesOpen, uesOpening ? 1.0 : 0.0, dt / uesTime);
    }

    updateWave(dt);

    if (phase_ == Phase::Releasing && output_.lesOpen <= 0.0 && output_.uesOpen <= 0.0) {
        phase_ = Phase::Idle;
    }
    return output_;
}

bool Reflex::isIdle() const
{
    return phase_ == Phase::Idle;
}

bool Reflex::isHeld() const
{
    return phase_ == Phase::Belch1 || phase_ == Phase::Open || phase_ == Phase::Sustain;
}

bool Reflex::isSustaining() const
{
    return phase_ == Phase::Sustain && settings_.sustainStrength > 0.0;
}

const ReflexOutput& Reflex::output() const
{
    return output_;
}

double Reflex::scaledTime(double time) const
{
    return time / settings_.speed;
}

// sustain opening = kSustainOpening s
double Reflex::sustainOpening() const
{
    return kSustainOpening * std::clamp(settings_.sustainStrength, 0.0, 1.0);
}

// gap = (1 + kBubbleJitter u) / rate - kBubbleTime,  rate = lerp(kMinBubbleRate, kMaxBubbleRate, s)
double Reflex::nextBubbleGap()
{
    const double rate = lerp(kMinBubbleRate, kMaxBubbleRate, std::clamp(settings_.sustainStrength, 0.0, 1.0));
    const double period = (1.0 + kBubbleJitter * uniform_(random_)) / rate;
    return std::max(0.0, scaledTime(period - kBubbleTime));
}

// Raised cosine 0.5 (1 - cos(2 pi t / T)), T = kBubbleTime, starting bubbleGap_
// after the last bubble ended. Draws the next gap when a bubble ends.
double Reflex::bubbleSwell()
{
    const double length = scaledTime(kBubbleTime);
    const double t = bubbleTime_ - bubbleGap_;
    if (t < 0.0) {
        return 0.0;
    }
    if (t >= length) {
        bubbleTime_ = t - length;
        bubbleGap_ = nextBubbleGap();
        return 0.0;
    }
    return 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * t / length));
}

// The depth moves toward its target by at most dt / (kWaveFade * travel time)
// per tick: full range in the time the wave's own envelope takes to fade in,
// so a normal wave is untouched. A wave cut short (release, a new trigger, or
// a Peristalsis Strength jump) relaxes where it is instead of vanishing in
// one tick. Vanishing let the squeezed esophagus spring open within 1 ms; the
// pressure pulse kicked the UES open and the tract rang like a struck bell.
void Reflex::updateWave(double dt)
{
    const double travelTime = scaledTime(kWaveTravelTime);
    const double sinceDelay = waveTime_ - scaledTime(kWaveDelay);
    const bool running = isHeld() && waveTime_ >= 0.0 && sinceDelay >= 0.0 && sinceDelay < travelTime;

    // Belch 3: the centre runs from the LES end (0) to the UES end (1).
    const double progress = sinceDelay / travelTime;
    const double target = running ? settings_.peristalsisStrength * waveEnvelope(progress, kWaveFade) : 0.0;
    if (running) {
        output_.waveCentre = progress;
    }
    output_.waveDepth = approach(output_.waveDepth, target, dt / (kWaveFade * travelTime));
    if (!running && output_.waveDepth <= 0.0) {
        output_.waveCentre = -1.0;
    }
}

} // namespace burp
