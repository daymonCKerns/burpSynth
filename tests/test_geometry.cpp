// Tests for geometry and the reflex timeline.
//
//   AreaFunction  sample() returns section values at their centres and
//                 interpolates between them; fill and scale behave.
//   Anatomy       Lengths scale with body size; shapes are positive.
//   Articulation  Every section stays at or above the floor for all corner
//                 settings; raising the tongue narrows the section under
//                 it; the last mouth section follows the lip area; pharynx
//                 width only changes the pharynx.
//   Peristalsis   Factor is 1 - depth at the centre and near 1 far away.
//   Reflex        After trigger: LES opens first, the UES opens only after
//                 the pressure threshold or the fallback delay, the wave
//                 runs once from 0 to 1, release closes both, and the
//                 reflex ends idle. Reflex speed 2 halves the fixed times. A
//                 wave cut by the release relaxes instead of vanishing.

#include "Check.h"

#include "model/control/Reflex.h"
#include "model/geometry/Anatomy.h"
#include "model/geometry/AreaFunction.h"
#include "model/geometry/Articulation.h"
#include "model/geometry/Peristalsis.h"
#include "model/math/Acoustics.h"
#include "model/math/Units.h"

#include <algorithm>
#include <cstdio>

namespace {

using namespace burp;

constexpr double kDt = 0.001;         // s, control tick
constexpr double kNever = 1.0e9;      // s, an event time that never comes
constexpr double kTimeTolerance = 0.0025;

void testAreaFunction()
{
    AreaFunction shape;
    BURP_CHECK(shape.sample(0.5) == 0.0);

    shape.fill(4, 1.0);
    BURP_CHECK(shape.count == 4);
    for (int i = 0; i < 4; ++i) {
        shape.area[i] = i + 1.0;
    }
    BURP_CHECK_NEAR(shape.sample(0.125), 1.0, 1e-12);
    BURP_CHECK_NEAR(shape.sample(0.375), 2.0, 1e-12);
    BURP_CHECK_NEAR(shape.sample(0.875), 4.0, 1e-12);
    BURP_CHECK_NEAR(shape.sample(0.25), 1.5, 1e-12);
    BURP_CHECK_NEAR(shape.sample(0.5625), 2.75, 1e-12);
    BURP_CHECK_NEAR(shape.sample(0.0), 1.0, 1e-12);
    BURP_CHECK_NEAR(shape.sample(1.0), 4.0, 1e-12);

    shape.scale(2.0);
    BURP_CHECK_NEAR(shape.area[3], 8.0, 1e-12);

    shape.fill(1000, 2.0);
    BURP_CHECK(shape.count == AreaFunction::kMaxSections);
    BURP_CHECK(shape.area[AreaFunction::kMaxSections - 1] == 2.0);
}

void testAnatomy()
{
    Anatomy anatomy;
    anatomy.set(BodySettings { 1.3, 0.25 });
    BURP_CHECK_NEAR(anatomy.esophagusLength(), 0.325, 1e-12);
    BURP_CHECK_NEAR(anatomy.pharynxLength(), kPharynxLength * 1.3, 1e-12);
    BURP_CHECK_NEAR(anatomy.mouthLength(), kMouthLength * 1.3, 1e-12);
    BURP_CHECK_NEAR(anatomy.nasalLength(), kNasalLength * 1.3, 1e-12);

    const AreaFunction esophagus = anatomy.esophagusShape();
    BURP_CHECK(esophagus.count > 0);
    for (int i = 0; i < esophagus.count; ++i) {
        BURP_CHECK(esophagus.area[i] > 0.0);
    }

    const AreaFunction nasal = anatomy.nasalShape();
    BURP_CHECK(nasal.count > 0);
    for (int i = 0; i < nasal.count; ++i) {
        BURP_CHECK(nasal.area[i] > 0.0);
    }
    BURP_CHECK(nasal.area[nasal.count - 1] == kNostrilArea);
}

// Index of the section whose span contains position s of the upper tract.
int pharynxSectionAt(double s)
{
    return static_cast<int>(s * 2.0 * kTractSections);
}

int mouthSectionAt(double s)
{
    return static_cast<int>((s - 0.5) * 2.0 * kTractSections);
}

void testArticulation()
{
    const double minArea = cm2ToM2(0.05);

    // Corners, plus out-of-range values that must be clamped.
    const double unitValues[] = { -1.0, 0.0, 1.0, 2.0 };
    const double lipAreas[] = { -1.0e-4, 0.0, cm2ToM2(5.0) };
    const double widths[] = { 0.5, 1.5 };
    double smallest = 1.0;
    for (const double jaw : unitValues) {
        for (const double position : unitValues) {
            for (const double height : unitValues) {
                for (const double lips : lipAreas) {
                    for (const double width : widths) {
                        const TractShapes shapes =
                            articulate(MouthShape { jaw, position, height, lips, width });
                        BURP_CHECK(shapes.pharynx.count == kTractSections);
                        BURP_CHECK(shapes.mouth.count == kTractSections);
                        for (int i = 0; i < kTractSections; ++i) {
                            BURP_CHECK(shapes.pharynx.area[i] >= minArea);
                            smallest = std::min(smallest, shapes.pharynx.area[i]);
                        }
                        for (int i = 0; i < kTractSections - 1; ++i) {
                            BURP_CHECK(shapes.mouth.area[i] >= minArea);
                            smallest = std::min(smallest, shapes.mouth.area[i]);
                        }
                        BURP_CHECK(shapes.mouth.area[kTractSections - 1] >= kClosedArea);
                    }
                }
            }
        }
    }
    std::printf("articulation: smallest section %g cm^2\n", smallest * 1.0e4);

    // Raising the tongue narrows the section under it, in the mouth (front)
    // and in the pharynx (back).
    MouthShape low;
    low.tongueHeight = 0.0;
    MouthShape high = low;
    high.tongueHeight = 1.0;

    low.tonguePosition = high.tonguePosition = 1.0;
    const int front = mouthSectionAt(0.9);
    const double frontLow = articulate(low).mouth.area[front];
    const double frontHigh = articulate(high).mouth.area[front];
    BURP_CHECK(frontHigh < frontLow);

    low.tonguePosition = high.tonguePosition = 0.0;
    const int back = pharynxSectionAt(0.3);
    const double backLow = articulate(low).pharynx.area[back];
    const double backHigh = articulate(high).pharynx.area[back];
    BURP_CHECK(backHigh < backLow);
    std::printf("articulation: tongue front %g -> %g cm^2, back %g -> %g cm^2\n",
                frontLow * 1.0e4, frontHigh * 1.0e4, backLow * 1.0e4, backHigh * 1.0e4);

    // The last mouth section is the lip area and nothing else moves with it.
    MouthShape openLips;
    openLips.lipArea = cm2ToM2(2.5);
    MouthShape closedLips = openLips;
    closedLips.lipArea = 0.0;
    const TractShapes openShapes = articulate(openLips);
    const TractShapes closedShapes = articulate(closedLips);
    BURP_CHECK(openShapes.mouth.area[kTractSections - 1] == cm2ToM2(2.5));
    BURP_CHECK(closedShapes.mouth.area[kTractSections - 1] == kClosedArea);
    for (int i = 0; i < kTractSections - 1; ++i) {
        BURP_CHECK(openShapes.mouth.area[i] == closedShapes.mouth.area[i]);
    }

    // Pharynx width scales the pharynx and leaves the mouth alone.
    MouthShape wide = openLips;
    wide.pharynxWidth = 1.5;
    const TractShapes wideShapes = articulate(wide);
    for (int i = 0; i < kTractSections; ++i) {
        BURP_CHECK_NEAR(wideShapes.pharynx.area[i] / openShapes.pharynx.area[i], 1.5, 1e-12);
        BURP_CHECK(wideShapes.mouth.area[i] == openShapes.mouth.area[i]);
    }
}

void testPeristalsis()
{
    BURP_CHECK_NEAR(peristalsisFactor(0.4, 0.4, kPeristalsisWidth, 0.3), 0.7, 1e-12);
    BURP_CHECK_NEAR(peristalsisFactor(1.0, 0.0, kPeristalsisWidth, 0.3), 1.0, 1e-6);
    BURP_CHECK(peristalsisFactor(0.5, 0.4, kPeristalsisWidth, 0.3) > 0.7);
}

// When each reflex event happened during one run (s, -1 = never).
struct Timeline {
    double lesStart = -1.0;
    double uesStart = -1.0;
    double waveStart = -1.0;
    double waveEnd = -1.0;
    double idle = -1.0;
    int waveCount = 0;
    double firstCentre = -1.0;
    double lastCentre = -1.0;
    bool centreRises = true;
    double maxLes = 0.0;
    double maxUes = 0.0;
    double maxDepth = 0.0;
    double finalLes = 0.0;
    double finalUes = 0.0;
};

// Triggers at t = 0, holds the pressure under the UES at 400 Pa from
// pressureOnset (0 before), releases at releaseAt and runs for `duration`.
Timeline runReflex(double speed, double pressureOnset, double releaseAt, double duration)
{
    ReflexSettings settings;
    settings.speed = speed;
    Reflex reflex;
    reflex.setSettings(settings);
    reflex.trigger();

    Timeline timeline;
    bool waveActive = false;
    const int steps = static_cast<int>(duration / kDt + 0.5);
    const int releaseStep = static_cast<int>(releaseAt / kDt + 0.5);
    for (int n = 0; n < steps; ++n) {
        const double t = (n + 1) * kDt;
        if (n == releaseStep) {
            reflex.release();
        }
        const ReflexOutput& out = reflex.tick(kDt, t >= pressureOnset ? 400.0 : 0.0);

        if (timeline.lesStart < 0.0 && out.lesOpen > 0.0) {
            timeline.lesStart = t;
        }
        if (timeline.uesStart < 0.0 && out.uesOpen > 0.0) {
            timeline.uesStart = t;
        }
        const bool active = out.waveCentre >= 0.0;
        if (active && !waveActive) {
            ++timeline.waveCount;
            timeline.waveStart = t;
            timeline.firstCentre = out.waveCentre;
        } else if (active) {
            timeline.centreRises = timeline.centreRises && out.waveCentre > timeline.lastCentre;
        } else if (waveActive) {
            timeline.waveEnd = t;
        }
        if (active) {
            timeline.lastCentre = out.waveCentre;
        }
        waveActive = active;
        if (timeline.idle < 0.0 && reflex.isIdle()) {
            timeline.idle = t;
        }

        timeline.maxLes = std::max(timeline.maxLes, out.lesOpen);
        timeline.maxUes = std::max(timeline.maxUes, out.uesOpen);
        timeline.maxDepth = std::max(timeline.maxDepth, out.waveDepth);
        timeline.finalLes = out.lesOpen;
        timeline.finalUes = out.uesOpen;
    }
    return timeline;
}

void printTimeline(const char* label, const Timeline& timeline)
{
    std::printf("reflex %s: LES %.3f s, UES %.3f s, wave %.3f-%.3f s, idle %.3f s\n", label,
                timeline.lesStart, timeline.uesStart, timeline.waveStart, timeline.waveEnd,
                timeline.idle);
}

void testReflex()
{
    Reflex fresh;
    BURP_CHECK(fresh.isIdle());

    // Speed 1, no pressure: Belch 2 comes from the 120 ms fallback.
    const Timeline slow = runReflex(1.0, kNever, 2.0, 2.5);
    printTimeline("speed 1", slow);
    BURP_CHECK(slow.lesStart > 0.0 && slow.lesStart < slow.uesStart);
    BURP_CHECK_NEAR(slow.uesStart, 0.12, kTimeTolerance);
    BURP_CHECK_NEAR(slow.waveStart - slow.uesStart, 0.25, kTimeTolerance);
    BURP_CHECK_NEAR(slow.waveEnd - slow.waveStart, 0.8, kTimeTolerance);
    BURP_CHECK(slow.waveCount == 1);
    BURP_CHECK(slow.centreRises);
    BURP_CHECK(slow.firstCentre < 0.01);
    BURP_CHECK(slow.lastCentre > 0.99);
    BURP_CHECK_NEAR(slow.maxDepth, ReflexSettings {}.peristalsisStrength, 0.01);
    BURP_CHECK_NEAR(slow.maxLes, 1.0, 1e-12);
    BURP_CHECK_NEAR(slow.maxUes, 1.0, 1e-12);
    BURP_CHECK(slow.idle > 2.0 && slow.idle < 2.0 + 0.08 + kTimeTolerance);
    BURP_CHECK(slow.finalLes == 0.0 && slow.finalUes == 0.0);

    // Pressure under the UES from 30 ms: Belch 2 comes early.
    const Timeline pushed = runReflex(1.0, 0.03, 2.0, 2.5);
    printTimeline("pressure", pushed);
    BURP_CHECK_NEAR(pushed.uesStart, 0.03, kTimeTolerance);
    BURP_CHECK(pushed.lesStart < pushed.uesStart);
    // The pressure never drains, so after the release the UES waits the 0.3 s
    // maximum, then closes.
    BURP_CHECK_NEAR(pushed.idle - 2.0, 0.3 + 0.08, 2.0 * kTimeTolerance);
    BURP_CHECK(pushed.finalUes == 0.0);

    // Speed 2 halves every fixed time.
    const Timeline fast = runReflex(2.0, kNever, 2.0, 2.5);
    printTimeline("speed 2", fast);
    BURP_CHECK_NEAR(fast.uesStart, 0.5 * slow.uesStart, kTimeTolerance);
    BURP_CHECK_NEAR(fast.waveStart - fast.uesStart, 0.5 * (slow.waveStart - slow.uesStart),
                    kTimeTolerance);
    BURP_CHECK_NEAR(fast.waveEnd - fast.waveStart, 0.5 * (slow.waveEnd - slow.waveStart),
                    kTimeTolerance);
    BURP_CHECK_NEAR(fast.idle - 2.0, 0.5 * (slow.idle - 2.0), kTimeTolerance);

    // Release during the wave cancels it: the contraction relaxes over
    // strength * 80 ms (not in one tick, which made a ringing click), and the
    // reflex still ends idle.
    const Timeline cut = runReflex(1.0, kNever, 0.6, 1.5);
    printTimeline("release mid-wave", cut);
    BURP_CHECK(cut.waveCount == 1);
    const double relaxTime = ReflexSettings {}.peristalsisStrength * 0.1 * 0.8;
    BURP_CHECK(cut.waveEnd > 0.6 + relaxTime - kTimeTolerance && cut.waveEnd < 0.6 + relaxTime + kTimeTolerance);
    BURP_CHECK(cut.idle > 0.6);
    BURP_CHECK(cut.finalLes == 0.0 && cut.finalUes == 0.0);

    // A trigger during a release continues from the current openings.
    Reflex reflex;
    reflex.setSettings(ReflexSettings {});
    reflex.trigger();
    for (int n = 0; n < 500; ++n) {
        reflex.tick(kDt, 0.0);
    }
    reflex.release();
    ReflexOutput before {};
    for (int n = 0; n < 10; ++n) {
        before = reflex.tick(kDt, 0.0);
    }
    reflex.trigger();
    const ReflexOutput after = reflex.tick(kDt, 0.0);
    BURP_CHECK(before.uesOpen > 0.0);
    BURP_CHECK(after.lesOpen > before.lesOpen);
    BURP_CHECK(after.lesOpen - before.lesOpen <= kDt / ReflexSettings {}.lesOpeningTime + 1e-12);
    BURP_CHECK(after.uesOpen <= before.uesOpen && after.uesOpen > 0.0);

    reflex.reset();
    BURP_CHECK(reflex.isIdle());
}

} // namespace

int main()
{
    testAreaFunction();
    testAnatomy();
    testArticulation();
    testPeristalsis();
    testReflex();
    return burp::test::failures();
}
