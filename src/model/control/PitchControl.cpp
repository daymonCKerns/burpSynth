// PitchControl: note glide, period measurement and the windowed pitch
// correction from PitchControl.h.

#include "model/control/PitchControl.h"

#include "model/acoustics/Sphincter.h"
#include "model/math/Acoustics.h"

#include <algorithm>
#include <cmath>

namespace burp {

void PitchControl::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    const double dt = 1.0 / sampleRate;
    average_.setTime(kAverageTime, dt);
    amplitudeDecay_ = onePoleCoefficient(kAmplitudeDecayTime, dt);
    reset();
}

void PitchControl::reset()
{
    windowOpen_ = false;
    settleLeft_ = 0.0;
    correction_ = 0.0;
    average_.reset(0.0);
    amplitude_ = 0.0;
    armed_ = false;
    haveCrossing_ = false;
    samplesSinceCrossing_ = 0;
    measuredHz_ = 0.0;
}

void PitchControl::setSettings(const PitchSettings& settings)
{
    settings_ = settings;
}

void PitchControl::startNote(double semitones)
{
    note_ = semitones;
    targetNote_ = semitones;
    windowOpen_ = true;
    settleLeft_ = settings_.settleTime;
}

void PitchControl::changeNote(double semitones)
{
    targetNote_ = semitones;
    windowOpen_ = true;
    settleLeft_ = settings_.settleTime;
}

void PitchControl::observe(double displacement)
{
    const double centred = displacement - average_.next(displacement);
    amplitude_ = std::max(std::abs(centred), amplitude_ * amplitudeDecay_);
    if (amplitude_ < kMinAmplitude) {
        armed_ = false;
        haveCrossing_ = false;
        return;
    }

    ++samplesSinceCrossing_;
    if (centred < -kArmFraction * amplitude_) {
        armed_ = true;
        return;
    }
    if (!armed_ || centred <= 0.0) {
        return;
    }

    // Upward zero crossing.
    armed_ = false;
    if (haveCrossing_) {
        measuredHz_ = sampleRate_ / static_cast<double>(samplesSinceCrossing_);

        // error = 12 log2(f_target / f_measured)
        const double error = note_ + settings_.bendSemitones - hzToMidi(measuredHz_);
        if ((windowOpen_ || settings_.alwaysCorrect) && std::abs(error) <= kMaxPitchError) {
            correction_ = std::clamp(correction_ + kCorrectionGain * error, -kMaxCorrection,
                                     kMaxCorrection);
        }
    }
    haveCrossing_ = true;
    samplesSinceCrossing_ = 0;
}

double PitchControl::tick(double dt)
{
    // Glide toward the target at glissandoRate.
    const double maxStep = settings_.glissandoRate * dt;
    const double remaining = targetNote_ - note_;
    note_ = std::abs(remaining) <= maxStep ? targetNote_ : note_ + std::copysign(maxStep, remaining);

    // The window stays open while gliding, then for settleTime after arrival.
    if (windowOpen_) {
        settleLeft_ = note_ == targetNote_ ? settleLeft_ - dt : settings_.settleTime;
        windowOpen_ = settleLeft_ > 0.0;
    }

    // Q = f_target / kSphincterReferenceHz * 2^(correction / 12)
    return targetHz() / kSphincterReferenceHz * std::exp2(correction_ / 12.0);
}

double PitchControl::targetHz() const
{
    return midiToHz(note_ + settings_.bendSemitones);
}

double PitchControl::measuredHz() const
{
    return measuredHz_;
}

double PitchControl::correctionSemitones() const
{
    return correction_;
}

} // namespace burp
