#pragma once

// Everything a player or patch can set on the model, in SI units.
//
// The engine fills this from the host parameters (ControlMapping.cpp) and
// hands it to BurpModel::setControls() before each render call. The defaults
// here match the parameter defaults in docs/implementation_plan.md.

#include "model/control/Reflex.h"
#include "model/geometry/Anatomy.h"
#include "model/geometry/Articulation.h"
#include "model/math/Units.h"

namespace burp {

struct BurpControls {
    // Live: may change during a burp. BurpModel smooths these.
    double abdominalPressure = mmHgToPa(10.0); // Pa
    MouthShape mouth;
    double velumArea = 0.0;                    // m^2
    double pitchBendSemitones = 0.0;           // bend already scaled by the bend range

    // Character.
    double gasVolume = mlToM3(200.0);          // m^3 at atmospheric pressure, at full velocity
    double lesOpeningArea = cm2ToM2(0.8);      // m^2
    ReflexSettings reflex;
    double uesMassScale = 3.0;
    double uesDampingRatio = 0.12;
    double uesRestArea = cm2ToM2(0.05);        // m^2
    double roughness = 0.3;                    // 0..1
    double breathiness = 0.3;                  // 0..1
    double wallSoftness = 0.5;                 // 0..1, 0.5 = nominal tissue
    double gasMix = 0.0;                       // 0 = air, 1 = carbon dioxide
    double glissandoRate = 60.0;               // semitones per second
    double pitchSettleTime = 0.15;             // s
    bool pitchAlwaysCorrect = false;
    double accentPressure = mmHgToPa(15.0);    // Pa added by a full-velocity legato note

    // Body: applied when a burp starts from idle.
    BodySettings body;
    double stomachElastance = mmHgToPa(10.0) / mlToM3(200.0); // Pa/m^3
    double esophagusStiffness = 0.7;           // 0..1 wall stiffness, 1 = nominal tissue, 0 = floppy
};

} // namespace burp
