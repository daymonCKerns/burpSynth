#pragma once

// Reflex: the timeline of a belch, as three reflexes in sequence
// (atlasofscience.org/belching-how-does-it-work).
//
//   Belch 1  trigger()                      the LES opens over lesOpeningTime
//   Belch 2  pressure under the UES passes  the UES releases over 30 ms
//            kBelch2Pressure, or 120 ms
//            after Belch 1
//   Belch 3  250 ms after Belch 2           one contraction wave travels up
//                                           the esophagus in 800 ms
//   Hold                                    open until release()
//   Release  release()                      LES closes in 20 ms, UES closes
//                                           over releaseTime
//   Idle     release finished
//
// Every fixed time above is divided by `speed`. trigger() during a release
// starts a new belch from the current openings, without a jump.
//
// Runs at control rate. Outputs are 0..1 openings that BurpModel scales to
// physical areas.

namespace burp {

inline constexpr double kBelch2Pressure = 300.0; // Pa

struct ReflexSettings {
    double lesOpeningTime = 0.04;     // s
    double releaseTime = 0.08;        // s
    double speed = 1.0;               // time scale; 2 = twice as fast
    double peristalsisStrength = 0.3; // 0..1, depth of the Belch 3 wave
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

private:
    enum class Phase {
        Idle,      // closed
        Belch1,    // LES opening, waiting for Belch 2
        Open,      // Belch 2 onward: UES open, Belch 3 wave, hold
        Releasing, // closing
    };

    // Fixed times at speed 1 (s).
    static constexpr double kFallbackDelay = 0.12;  // Belch 1 to Belch 2 without pressure
    static constexpr double kUesReleaseTime = 0.03;
    static constexpr double kWaveDelay = 0.25;      // Belch 2 to Belch 3
    static constexpr double kWaveTravelTime = 0.8;
    static constexpr double kLesCloseTime = 0.02;
    static constexpr double kWaveFade = 0.1;        // fraction of the travel faded in and out

    double scaledTime(double time) const;
    void updateWave();

    ReflexSettings settings_;
    ReflexOutput output_;
    Phase phase_ = Phase::Idle;
    double phaseTime_ = 0.0; // s since trigger (Belch1) or since Belch 2 (Open)
};

} // namespace burp
