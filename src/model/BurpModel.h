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
// the tube shapes (articulation, peristalsis) and refresh the
// connectors' neighbour inertances.
//
// Notes:
//   trigger()     starts a burp. The stomach is filled with
//                 gasVolume * lerp(0.3, 1, velocity). From idle, the body
//                 settings and gas mix are applied and the tubes re-gridded.
//                 While held past the burst, with Sustain Strength above 0,
//                 the reflex keeps a steady flow with small bubbles and the
//                 stomach is refilled toward the trigger's fill (time
//                 constant kRefillTime), so a held note can go on.
//   changeNote()  legato: the pitch glides, and velocity adds an accent to
//                 the abdominal pressure that decays with kAccentDecay.
//   release()     ends the burp; the gas left in the esophagus drains out
//                 as the tail.
// Once the reflex is idle, the model fades over kSafetyFade, zeroes its state
// and sleeps (render writes silence) until the next trigger. The fade starts
// after kQuietTime of output below kQuietPressure, or after kMaxTailTime.
// Ending on quiet matters: gas left under the shut UES can pop it open long
// after the tail has died away, and each pop is a lone click.
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
inline constexpr double kMaxTailTime = 0.5;       // s, longest tail after the reflex goes idle
inline constexpr double kQuietTime = 0.03;        // s of quiet that ends the tail early
inline constexpr double kQuietPressure = 1.0e-3;  // Pa, output below this counts as quiet
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
    double esophagusPressure() const; // Pa, just under the UES
    double pharynxPressure() const;   // Pa, just above the UES
    // Added for tools/run_scenario's trace.
    double mouthPressure() const;     // Pa, just behind the lips
    const ReflexOutput& reflexOutput() const;

    // For the editor's body view (Engine::takeMeters): flows averaged over
    // kDisplayFlowTime, since the UES flow pulses at the pitch, and the
    // stomach's gas volume.
    double lesFlowAverage() const;    // m^3/s
    double uesFlowAverage() const;    // m^3/s
    double stomachVolume() const;     // m^3
    double lesFlow() const;           // m^3/s
    double uesFlow() const;           // m^3/s
    double uesArea() const;           // m^2, narrowest point of the UES slit (negative = pressed shut)

private:
    static constexpr double kLesChannelLength = 0.02;     // m
    static constexpr double kVelumChannelLength = 0.01;   // m
    static constexpr int kEsophagusSections = 32;
    static constexpr double kControlSmoothingTime = 0.015; // s
    static constexpr double kPressureAverageTime = 0.005;  // s
    static constexpr double kRoughnessTime = 0.02;         // s, noise correlation time
    static constexpr double kFloppiestEsophagusWall = 0.05; // wall stiffness at Esophagus Stiffness 0
    static constexpr double kRoughnessDepth = 0.06;        // relative Q deviation at full roughness
    static constexpr double kRefillTime = 0.1;             // s, time constant of the sustain refill
    // Pharynx and mouth area slew limit (Tube::setSlewLimit), so no control can
    // move the tract fast enough to make a pressure pulse. Set by measurement
    // (tools/scenarios/tongue_jumps_high): 5%/ms still clipped, 2.5%/ms did
    // not. A tenfold opening then takes about 90 ms, near real tongue speed.
    static constexpr double kTractSlewRate = 25.0;         // 1/s: at most 2.5% of a section's area per ms
    static constexpr double kTractSlewFloor = 5.0e-3;      // m^2/s: 0.05 cm^2 per ms, so a shut section can open
    static constexpr double kDisplayFlowTime = 0.05;       // s, time constant of the display flow averages

    void applyBody();
    void resetAcoustics();
    void snapSmoothers();
    void controlTick();
    double stepSample();
    double esophagusWallStiffness() const;

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
    double flowAverageCoefficient_ = 0.0;
    double lesFlowAverage_ = 0.0; // m^3/s, for display
    double uesFlowAverage_ = 0.0; // m^3/s, for display
    double noiseScale_ = 1.0;   // rescales the filtered noise to unit variance
    double density_ = 1.0;
    double outputScale_ = 0.0;  // rho / (4 pi r)
    double accent_ = 0.0;       // Pa
    double fullGas_ = 0.0;      // m^3 at atmospheric pressure put in at the note's trigger
    double fullMoles_ = 0.0;    // stomach gas at the note's trigger; sustain refills up to this

    int idleSamples_ = 0;       // samples since the reflex went idle
    int quietSamples_ = 0;      // consecutive idle samples below kQuietPressure
    int tailSamples_ = 0;       // kMaxTailTime in samples
    int quietLimit_ = 1;        // kQuietTime in samples
    int fadeSamples_ = 1;
    int fadeLeft_ = 0;          // samples left in the final fade; 0 = not fading
    bool asleep_ = true;
};

} // namespace burp
