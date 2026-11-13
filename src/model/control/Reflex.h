#pragma once

// Reflex: the timeline of a belch, as three reflexes in sequence
// (atlasofscience.org/belching-how-does-it-work), then a steady flow with
// small bubbles for as long as the note is held.
//
//   Belch 1  trigger()                      the LES opens over lesOpeningTime
//   Belch 2  pressure under the UES passes  the UES releases over 30 ms
//            kBelch2Pressure, or 120 ms
//            after Belch 1
//   Burst    burstTime after Belch 2        the first impulse ends. With
//                                           sustain, the LES settles to the
//                                           sustain opening over kSettleTime
//                                           and the UES stays open. Without,
//                                           the LES closes in 20 ms, the UES
//                                           holds while the pressure under it
//                                           is above kDrainedPressure (at most
//                                           kMaxDrainTime), then closes over
//                                           releaseTime: the tail
//   Bubble   while sustained, at the        the LES opening swells by
//            bubble rate below              kBubbleHeight for kBubbleTime
//                                           (raised cosine)
//   Belch 3  250 ms after Belch 2,          one contraction wave travels up
//            while held                     the esophagus in 800 ms
//   Release  release()                      the closing without sustain. A
//                                           wave still running relaxes where
//                                           it is (at most 80 ms) rather than
//                                           vanishing
//   Idle     release finished
//
// Sustain Strength s (0..1) sets both the steady flow and the bubbles:
//   sustain opening = kSustainOpening s
//   bubble rate     = lerp(kMinBubbleRate, kMaxBubbleRate, s)
// The gap between bubbles is (1 + kBubbleJitter u) / rate - kBubbleTime with
// u uniform in -1..1, drawn per bubble. s = 0: no sustain, a held note is
// the impulse and its tail. BurpModel keeps the stomach full while sustained.
//
// Every fixed time above, lesOpeningTime, burstTime and releaseTime are
// divided by `speed`. trigger() during a release starts a new belch from the
// current openings, without a jump.
//
// Runs at control rate. Outputs are 0..1 openings that BurpModel scales to
// physical areas.

#include <random>

namespace burp {

inline constexpr double kBelch2Pressure = 300.0;  // Pa
inline constexpr double kDrainedPressure = 100.0; // Pa, esophagus counts as drained below this

struct ReflexSettings {
    double lesOpeningTime = 0.04;     // s
    double releaseTime = 0.08;        // s
    double speed = 1.0;               // time scale; 2 = twice as fast
    double peristalsisStrength = 0.3; // 0..1, depth of the Belch 3 wave
    double burstTime = 0.4;           // s, how long the first impulse keeps the LES fully open
    double sustainStrength = 0.5;     // 0..1, steady flow and bubble rate while held; 0 = none
};

struct ReflexOutput {
    double lesOpen = 0.0;     // 0..1
    double uesOpen = 0.0;     // 0..1
    double waveCentre = -1.0; // 0..1 along the esophagus; negative = no wave
    double waveDepth = 0.0;   // 0..1
};

class Reflex {
public:
    void setSettings(const ReflexSettings& settings);
    void trigger();
    void release();
    void reset(); // idle, everything closed

    // Advances by dt seconds. esophagusTopPressure is the slow-averaged
    // pressure just under the UES (Pa).
    const ReflexOutput& tick(double dt, double esophagusTopPressure);

    bool isIdle() const;
    bool isHeld() const;       // triggered and not yet released
    bool isSustaining() const; // held, past the burst, with sustain on
    const ReflexOutput& output() const; // the last tick's output; added for tools/run_scenario's trace

private:
    enum class Phase {
        Idle,      // closed
        Belch1,    // LES opening, waiting for Belch 2
        Open,      // the burst: LES and UES open
        Sustain,   // still held after the burst: steady flow and bubbles, or the tail
        Releasing, // released: closing
    };

    // Fixed times at speed 1 (s).
    static constexpr double kFallbackDelay = 0.12;  // Belch 1 to Belch 2 without pressure
    static constexpr double kUesReleaseTime = 0.03;
    static constexpr double kWaveDelay = 0.25;      // Belch 2 to Belch 3
    static constexpr double kWaveTravelTime = 0.8;
    static constexpr double kLesCloseTime = 0.02;
    static constexpr double kWaveFade = 0.1;        // fraction of the travel faded in and out
    static constexpr double kMaxDrainTime = 0.3;    // longest the UES waits for the drain
    static constexpr double kSettleTime = 0.2;      // burst to sustain opening, full range
    static constexpr double kBubbleTime = 0.06;

    // Sustain. Guesses, to tune by ear.
    static constexpr double kSustainOpening = 0.5;  // LES opening at full strength
    static constexpr double kMinBubbleRate = 1.0;   // Hz, at strength 0+
    static constexpr double kMaxBubbleRate = 6.0;   // Hz, at strength 1
    static constexpr double kBubbleJitter = 0.5;    // 0..1, irregularity of the bubble timing
    static constexpr double kBubbleHeight = 0.3;    // bubble peak, relative to the sustain opening

    double scaledTime(double time) const;
    double sustainOpening() const;
    double nextBubbleGap();
    double bubbleSwell();
    void updateWave(double dt);

    ReflexSettings settings_;
    ReflexOutput output_;
    Phase phase_ = Phase::Idle;
    double phaseTime_ = 0.0;   // s in the current phase
    double baseOpen_ = 0.0;    // LES opening under the bubbles while in Sustain
    double bubbleTime_ = 0.0;  // s since the last bubble ended
    double bubbleGap_ = 1.0;   // s from the last bubble's end to the next one's start
    double waveTime_ = -1.0;   // s since Belch 2; negative = not yet

    std::minstd_rand random_;
    std::uniform_real_distribution<double> uniform_ { -1.0, 1.0 };
};

} // namespace burp
