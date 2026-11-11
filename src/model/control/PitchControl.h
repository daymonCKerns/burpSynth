#pragma once

// Pitch control: turns the played note into the UES tension, and keeps the
// start of each note in tune.
//
// Target (control rate):
//   pitch    = note + bend            semitones; note glides at glissandoRate
//   f_target = 440 * 2^((pitch - 69) / 12)
//   Q        = f_target / kSphincterReferenceHz * 2^(correction / 12)
//
// Measurement (audio rate): the period of the UES lower mass, from upward
// zero crossings of its displacement with the slow average removed. A
// crossing only counts when the swing is large enough to be real oscillation.
//
// Correction: a slow integrator on the pitch error, in semitones, updated
// once per measured cycle:
//   correction += kCorrectionGain * 12 log2(f_target / f_measured)
// It is clamped to +-kMaxCorrection, and a measurement more than
// kMaxPitchError from the target is ignored (it is a subharmonic or noise).
//
// The correction only runs inside a window: from startNote()/changeNote()
// until settleTime after the glide has arrived. Then it freezes, so pitch
// movement caused by pressure or mouth shape while a note is held is kept as
// expression. With alwaysCorrect it never freezes. The correction value
// carries over from note to note, so later notes start in tune.

#include "model/math/Smooth.h"

namespace burp {

inline constexpr double kCorrectionGain = 0.3;
inline constexpr double kMaxCorrection = 4.0;  // semitones
inline constexpr double kMaxPitchError = 7.0;  // semitones

struct PitchSettings {
    double bendSemitones = 0.0;  // pitch bend already scaled by the bend range
    double glissandoRate = 60.0; // semitones per second
    double settleTime = 0.15;    // s
    bool alwaysCorrect = false;
};

class PitchControl {
public:
    void prepare(double sampleRate); // audio (internal) rate, for observe()
    void reset();                    // also clears the carried correction
    void setSettings(const PitchSettings& settings);

    void startNote(double semitones);  // new burp: jumps to the note
    void changeNote(double semitones); // legato: glides to the note

    // Audio rate: one sample of the UES lower-mass displacement (m).
    void observe(double displacement);

    // Control rate: advances the glide and the window by dt seconds and
    // returns the tension Q.
    double tick(double dt);

    double targetHz() const;
    double measuredHz() const;          // 0 until a period has been measured
    double correctionSemitones() const;

private:
    static constexpr double kAverageTime = 0.03;        // s, slow average removed
    static constexpr double kAmplitudeDecayTime = 0.1;  // s, peak tracker release
    static constexpr double kMinAmplitude = 1.0e-6;     // m, below this nothing counts
    static constexpr double kArmFraction = 0.2;         // hysteresis, of the amplitude

    PitchSettings settings_;
    double sampleRate_ = 48000.0;

    double note_ = 60.0;       // semitones, gliding
    double targetNote_ = 60.0; // semitones
    bool windowOpen_ = false;
    double settleLeft_ = 0.0;  // s of window left once arrived
    double correction_ = 0.0;  // semitones

    OnePole average_;
    double amplitude_ = 0.0;
    double amplitudeDecay_ = 0.0;
    bool armed_ = false;
    bool haveCrossing_ = false;
    long long samplesSinceCrossing_ = 0;
    double measuredHz_ = 0.0;
};

} // namespace burp
