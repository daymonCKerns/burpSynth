#pragma once

// BurpModel: one body. Owns every part, wires them in a fixed topology and
// runs the two-rate loop.
//
//   stomach --[LES]-- esophagus --[UES]-- pharynx --[join]-- mouth --[lips]
//                                            |
//                                         [velum]-- nasal --[nostrils]
//
// Audio rate, per sample (docs/implementation_plan.md, section 3.5):
//   1. flows from the current pressures: LES, tube links, UES, join, velum,
//      lips, nostrils
//   2. each connector's flow leaves its upstream cell and enters its
//      downstream cell
//   3. pressures: stomach, then every tube's cells
//   4. the UES lower-mass displacement goes to the pitch tracker
//   5. out = rho / (4 pi r) * (dU_lips/dt + dU_nostrils/dt),  r = kListenerDistance
//
// Control rate, every kControlPeriod (section 3.6): smooth the live controls,
// advance the reflex and the pitch, set valve and sphincter targets, rebuild
// the tube shapes (articulation, tube law, peristalsis) and refresh the
// connectors' neighbour inertances.
//
// Notes:
//   trigger()     starts a burp. The stomach is filled with
//                 gasVolume * lerp(0.3, 1, velocity). From idle, the body
//                 settings and gas mix are applied and the tubes re-gridded.
//   changeNote()  legato: the pitch glides, and velocity adds an accent to
//                 the abdominal pressure that decays with kAccentDecay.
//   release()     ends the burp; the gas left in the esophagus drains out
//                 as the tail.
// After the reflex is idle for kTailTime the model fades over kSafetyFade,
// zeroes its state and sleeps (render writes silence) until the next trigger.
//
// Roughness perturbs the tension Q with slow Gaussian noise from <random>
// (about +-6% at full roughness, 20 ms correlation time).
//
// render() never allocates. Output is sound pressure in Pa.

#include "model/BurpControls.h"
#include "model/acoustics/Link.h"
#include "model/acoustics/Radiation.h"
#include "model/acoustics/Reservoir.h"
#include "model/acoustics/Sphincter.h"
#include "model/acoustics/Tube.h"
#include "model/acoustics/Valve.h"
#include "model/control/PitchControl.h"
#include "model/control/Reflex.h"
#include "model/geometry/Anatomy.h"
#include "model/math/Smooth.h"

#include <Eigen/Core>

#include <random>

namespace burp {

inline constexpr double kControlPeriod = 0.001;   // s
inline constexpr double kListenerDistance = 0.3;  // m
inline constexpr double kAccentDecay = 0.15;      // s, time constant
inline constexpr double kTailTime = 0.5;          // s
inline constexpr double kSafetyFade = 0.02;       // s

class BurpModel {
public:
    // sampleRate is the rate render() runs at (the engine's oversampled rate).
    // Allocates; call off the audio thread.
    void prepare(double sampleRate);

    // Silences and empties everything; keeps the controls.
    void reset();

    // Copies the controls; they take effect from the next control tick.
    void setControls(const BurpControls& controls);

    void trigger(double semitones, double velocity);    // velocity 0..1
    void changeNote(double semitones, double velocity); // velocity 0 = no accent
    void release();

    bool isAsleep() const;

    // Mono, numFrames samples at the prepared rate.
    void render(double* out, int numFrames);

    // For tests and tuning.
    double stomachPressure() const; // Pa, gauge
    double stomachMoles() const;
    double measuredHz() const;      // UES pitch from the tracker, 0 until measured
    double targetHz() const;

private:
    static constexpr double kLesChannelLength = 0.02;     // m
    static constexpr double kVelumChannelLength = 0.01;   // m
    static constexpr int kEsophagusSections = 32;
    static constexpr double kControlSmoothingTime = 0.015; // s
    static constexpr double kPressureAverageTime = 0.005;  // s
    static constexpr double kRoughnessTime = 0.02;         // s, noise correlation time
    static constexpr double kRoughnessDepth = 0.06;        // relative Q deviation at full roughness

    void applyBody();
    void resetAcoustics();
    void snapSmoothers();
    void controlTick();
    double stepSample();
    double esophagusCollapsedFraction() const;

    BurpControls controls_;

    Reservoir stomach_;
    Valve les_;
    Tube esophagus_;
    Sphincter ues_;
    Tube pharynx_;
    Link join_;
    Valve velum_;
    Tube mouth_;
    Tube nasal_;
    Radiation lips_;
    Radiation nostrils_;

    Reflex reflex_;
    PitchControl pitch_;
    Anatomy anatomy_;

    OnePole abdominalSmoother_;
    OnePole jawSmoother_;
    OnePole tonguePositionSmoother_;
    OnePole tongueHeightSmoother_;
    OnePole lipAreaSmoother_;
    OnePole pharynxWidthSmoother_;
    OnePole velumSmoother_;
    OnePole bendSmoother_;
    OnePole noiseSmoother_;

    Eigen::ArrayXd esophagusAverage_; // slow average of each esophagus cell's pressure, Pa

    std::mt19937 random_;
    std::normal_distribution<double> gaussian_;

    double dt_ = 0.0;
    double controlDt_ = 0.0;
    int controlInterval_ = 1;
    int samplesUntilTick_ = 1;
    bool tickPending_ = false;

    double averageCoefficient_ = 0.0;
    double accentCoefficient_ = 0.0;
    double noiseScale_ = 1.0;   // rescales the filtered noise to unit variance
    double density_ = 1.0;
    double outputScale_ = 0.0;  // rho / (4 pi r)
    double accent_ = 0.0;       // Pa

    int idleSamples_ = 0;
    int tailSamples_ = 0;
    int fadeSamples_ = 1;
    bool asleep_ = true;
};

} // namespace burp
