# BurpSynth implementation plan

This is the working plan for the first full implementation of the model, the
engine, the plugin parameters and a basic editor. The headers in `src/` are the
contract: each one states what the file does, the equations it implements and
its public interface. This document explains how the pieces fit.

The code is written but has not been built or run yet. Every physical
constant is a starting value to tune by ear and by the tests in section 8.

## 1. Rules

- Monophonic: one body, one burp at a time.
- SI units inside the model (Pa gauge, m, m^2, m^3, m^3/s, kg, s). Plain
  parameter units (mmHg, cm^2, mL, ms) are converted in one place,
  `audio_engine/ControlMapping.cpp`.
- One equation per function, named for the equation, with its source in a
  comment. No fluff.
- `math/` holds stateless free functions. Parts are concrete classes.
- Fixed topology: `BurpModel` holds each part as a named member and wires them
  with an explicit update order.
- No allocation, locks or I/O while rendering. Sizes are fixed in `prepare()`.
- No physics or DSP libraries: the model, solvers and DSP are written in
  house. The C++ standard library (including `<random>` for noise), Eigen
  (tube state arrays), the VST3 SDK and VSTGUI are the only dependencies.
- Positive flow is toward the mouth. "Up" means upstream (stomach side) and
  "down" means downstream (mouth side).

## 2. Signal path

```
abdominal pressure
      |
   STOMACH --[LES]-- ESOPHAGUS --[UES]-- PHARYNX --[join]-- MOUTH --[lips]--> out
  (Reservoir) (Valve)  (Tube)  (Sphincter) (Tube)   (Link)   (Tube) (Radiation)
                                              |
                                           [velum]-- NASAL --[nostrils]--> out
                                           (Valve)   (Tube)  (Radiation)
```

The pharynx and mouth are two tubes so that the nasal branch attaches at a tube
end, not in the middle of one.

## 3. Solver

### 3.1 Two primitives

```
Link (flow U between two cells):  L dU/dt = P_up - P_down - R U     L = rho l / A
Cell (pressure P in a volume):    C dP/dt = sum(U_in) - sum(U_out)  C = V / (rho c^2)
```

They are stepped in alternation (staggered leapfrog). Resistance is applied
implicitly so that large resistances cannot destabilise a link:

```
U <- (U + dt/L (P_up - P_down)) / (1 + dt R / L)
P <- P + dt/C (net flow)
```

A tube is a chain of cells with a link between each pair. The link between
cells i and i+1 is two half-cells in series:

```
L_link = rho (dx/2) / A_i + rho (dx/2) / A_(i+1)
```

Every connector (valve, sphincter, join, radiation) must add the half-cell
inertance of each tube end it touches. `Tube::inletHalfInertance()` and
`outletHalfInertance()` provide it.

### 3.2 Stability

For a cell with compliance C and attached links L_j, the scheme is stable when

```
dt^2 / C * sum_j (1 / L_j) <= 2
```

With half-cell inertances, 1/L_j <= 2A/(rho dx) for every link on a cell of
area A. The pharynx outlet cell has three links (interior, join, velum), which
gives c dt / dx <= 1/sqrt(3). The code uses a Courant number of 0.5
(`kCourant`), so dx = 2 c dt. At an internal rate of 96 kHz that is 7.4 mm.

### 3.3 Moving walls

Changing a cell's area changes its volume, which displaces gas:

```
C dP/dt = net flow - dV/dt,   dV/dt = dx dA/dt
```

This term is what lets peristalsis pump gas and lets the esophagus inflate.

### 3.4 Yielding walls

Each tube cell has a wall with mass, damping and stiffness per unit area
(m = 15 kg/m^2, r = 16000 Pa s/m, k = 3e6 Pa/m; soft-tissue values used in
speech synthesis):

```
m y'' + r y' + k y = P,   wall flow = scale * S * y',   S = 2 sqrt(pi A) dx
```

`scale` comes from the Wall Softness parameter (0 = rigid, 0.5 = nominal).
The esophagus wall's `k` is also scaled by Esophagus Stiffness (see
section 6). This is how the esophagus gives way to gas: the wall runs every
sample, so it stays in step with the pressure.

Tube shapes must never be driven by pressure at control rate. An earlier
version set the esophagus area from its averaged pressure every 1 ms (a tube
law). Area changes move pressure by about rho c^2 dA / A, so that loop had a
gain near 100 and a delay of several ms: it flipped every tick and pumped
the gas at 500 Hz without end.

### 3.5 Per-sample order (`BurpModel::render`)

1. Flows, all from the current pressures: LES, esophagus links, UES (which
   also moves its masses), pharynx links, join, velum, mouth links, nasal
   links, lips, nostrils.
2. Route each connector flow out of the upstream cell and into the
   downstream cell.
3. Pressures: stomach, then every tube's cells.
4. Feed the UES lower-mass displacement to the pitch tracker.
5. Output = rho / (4 pi r) * (dU_lips/dt + dU_nostrils/dt), r = 0.3 m.

### 3.6 Control tick (every 1 ms)

1. Smooth the live controls (one-pole, about 15 ms).
2. Update the slow average of the esophagus pressures (one-pole, 5 ms).
3. Advance the reflex with the averaged pressure under the UES.
4. Stomach external pressure = abdominal pressure + accent envelope.
5. Valve targets: LES area = reflex.lesOpen * LES opening area; velum area.
6. Tension Q from `PitchControl`, perturbed by roughness, into the UES with
   the reflex's `uesOpen`.
7. Shapes: `articulate()` for pharynx and mouth; esophagus = open area *
   peristalsis factor. Tubes ramp to the new shapes over one control
   interval. Pharynx and mouth areas are slew-limited to
   max(2.5% of the area, 0.05 cm^2) per ms: a faster change squeezes the gas
   in the cells into a pressure pulse (a tongue jump made +14 dB clicks).
   Esophagus wall stiffness from Esophagus Stiffness.
8. Refresh each connector's neighbour inertance and the lip opening.

Body parameters (lengths, gas mix) are applied only when a burp starts from
idle, because they re-grid the tubes.

## 4. Parts

| Part | Class | Key behaviour |
|---|---|---|
| Stomach | `Reservoir` | Gas law + elastic wall + abdominal pressure. Tracks moles; pressure falls as it empties |
| LES, velum | `Valve` | Link with a ramped opening area and Bernoulli loss. Closed below `kClosedArea` |
| Esophagus, pharynx, mouth, nasal | `Tube` | Chain of cells and links, moving and yielding walls |
| UES | `Sphincter` | Two coupled masses (Steinecke & Herzel 1995) with collision; tension factor Q sets pitch |
| Lips, nostrils | `Radiation` | Parallel R-L load (Flanagan); its dU/dt is the output |
| Jet noise | `Turbulence` | Noise pressure scaled by dynamic pressure above a critical Reynolds number. The noise is band-limited to 300 Hz–2 kHz (one-pole slopes) and normalised to unit variance |

Why two masses in the UES: a single mass driven by Bernoulli pressure cannot
feed energy into its own motion without help from the tube. The phase lag
between a lower and an upper edge is what sustains oscillation, and it makes
the pitch follow tension, not tube resonances.

The UES constants are vocal-fold values from Steinecke & Herzel. No measured
UES values were available. UES Mass scales mass and stiffness together, which
is equivalent to dividing the driving pressure, so it is the main tuning knob.

## 5. Control

### 5.1 Reflex timeline (`Reflex`)

| Phase | Trigger | Effect |
|---|---|---|
| Belch 1 | note-on | LES opens over the LES opening time |
| Belch 2 | averaged pressure under the UES passes 300 Pa, or 120 ms after Belch 1 | UES releases over 30 ms |
| Belch 3 | 250 ms after Belch 2 | One contraction wave travels up the esophagus in 800 ms |
| Burst | Burst Length after Belch 2 | The first impulse ends. With Sustain Strength s > 0 the LES settles over 200 ms to 0.5 s of full opening and the UES stays open; with s = 0 it closes as on release (the tail) |
| Bubble | while sustained, about lerp(1, 6 Hz, s) | The LES opening swells by 30% for 60 ms (raised cosine); gaps jitter by ±50% |
| Release | note-off | LES closes in 20 ms. The UES holds open until the averaged pressure under it drops below 100 Pa (at most 300 ms), then closes over the release time. Closing on a full esophagus makes the trapped gas pop it open repeatedly (harsh clicks). A Belch 3 wave still running relaxes where it is (its depth falls at the rate of the wave's own fade, at most 80 ms) instead of vanishing: vanishing let the esophagus spring open in 1 ms, and the pulse rang the tract like a bell |
| Idle | release done | Model sleeps after 30 ms of output below 1e-3 Pa (at most 0.5 s), with a 20 ms safety fade. Ending on quiet stops gas under the shut UES from popping it long after the tail |

While sustained, BurpModel refills the stomach toward the note's first fill
(time constant 100 ms) so the flow stays steady. Not physical; musical.

All times are divided by Reflex Speed.

### 5.2 Notes (`Engine` + `NoteStack`)

- Note-on with no held notes: `BurpModel::trigger`, after handing the model
  the current parameter values (parameters set earlier in the same block
  apply from the note's first sample). Velocity scales the gas amount (30% to
  100% of Gas Amount).
- Note-on while a note is held: `BurpModel::changeNote`. The pitch glides and
  the velocity adds a pressure accent (velocity * Accent Amount, decaying with
  a 150 ms time constant).
- Note-off of the sounding note with others held: glide back to the most
  recent held note, with no accent.
- Last note-off: `BurpModel::release`.

### 5.3 Pitch (`PitchControl`)

```
pitch    = note + bend * bend range                (semitones, glides at the glissando rate)
f_target = 440 * 2^((pitch - 69) / 12)
Q        = f_target / kSphincterReferenceHz * 2^(correction / 12)
```

The correction is a slow controller on the measured period of the UES lower
mass. It runs from a note start or note change until Pitch Settle after the
glide ends, then freezes, so later pitch movement from other parameters is
kept. At the maximum Pitch Settle it never freezes. It is limited to +-4
semitones, ignores measurements more than 7 semitones off target, and carries
over between notes.

## 6. Parameters

IDs are fixed forever (projects and automation store them). Plain units are
what the host displays.

| ID | Name | Units | Min | Max | Default |
|---|---|---|---|---|---|
| 0 | Abdominal Pressure | mmHg | 0 | 100 | 10 |
| 1 | Jaw Opening | | 0 | 1 | 0.5 |
| 2 | Tongue Position | | 0 | 1 | 0.5 |
| 3 | Tongue Height | | 0 | 1 | 0.3 |
| 4 | Lip Opening | cm2 | 0 | 5 | 2.5 |
| 5 | Velum | cm2 | 0 | 1.5 | 0 |
| 6 | Pitch Bend | | -1 | 1 | 0 |
| 10 | Gas Amount | mL | 20 | 500 | 200 |
| 11 | LES Opening Time | ms | 10 | 300 | 40 |
| 12 | LES Opening Area | cm2 | 0.1 | 2 | 0.8 |
| 13 | UES Mass | x | 0.5 | 8 | 3 |
| 14 | UES Damping | | 0.02 | 0.5 | 0.12 |
| 15 | UES Resting Gap | cm2 | -0.05 | 0.2 | 0.05 |
| 16 | Roughness | | 0 | 1 | 0.3 |
| 17 | Breathiness | | 0 | 1 | 0.3 |
| 18 | Wall Softness | | 0 | 1 | 0.5 |
| 19 | Peristalsis Strength | | 0 | 1 | 0.3 |
| 20 | Reflex Speed | x | 0.5 | 2 | 1 |
| 21 | Gas Mix | | 0 | 1 | 0 |
| 22 | Output Level | dB | -60 | 12 | 0 |
| 23 | Bend Range | st | 0 | 12 | 2 |
| 24 | Glissando Rate | st/s | 1 | 500 | 60 |
| 25 | Pitch Settle | ms | 0 | 2000 | 150 |
| 26 | Release Time | ms | 30 | 300 | 80 |
| 27 | Accent Amount | mmHg | 0 | 50 | 15 |
| 28 | Burst Length | ms | 30 | 2000 | 400 |
| 29 | Sustain Strength | | 0 | 1 | 0.5 |
| 40 | Body Size | x | 0.7 | 1.3 | 1 |
| 41 | Stomach Stiffness | mmHg | 2 | 30 | 10 |
| 42 | Esophagus Length | cm | 18 | 30 | 25 |
| 43 | Esophagus Stiffness | | 0 | 1 | 0.7 |
| 44 | Pharynx Width | x | 0.5 | 1.5 | 1 |

Notes:
- UES Damping is the damping ratio of the lower UES mass. Both masses share
  the resulting damping constant, as in Steinecke & Herzel.
- Stomach Stiffness is the wall pressure at 200 mL of gas.
- Gas Mix runs from air (0) to carbon dioxide (1).
- Esophagus Stiffness sets the esophagus wall stiffness:
  0.05^(1 - value) x nominal tissue, so 1 = nominal and 0 = 20x floppier.
- Pitch Settle at its maximum means "always correcting".
- Burst Length is how long the first impulse keeps the LES fully open.
- Sustain Strength sets what a held note does after the burst: the steady
  LES opening (0.5 x s), the bubble rate (lerp(1, 6 Hz, s)) and, through the
  refill, a stomach kept full. 0 = no sustain: the burst and its tail.
- Stomach Stiffness applies live (every control tick). Gas Amount, Body Size,
  Esophagus Length and Gas Mix apply when a burp starts from idle.
- MIDI: the mod wheel maps to Abdominal Pressure and the pitch wheel to
  Pitch Bend (VST3 delivers both as parameter changes).

## 7. Files and work packages

Each package is implemented by one agent. Public interfaces in the headers are
frozen; private members are the implementer's choice.

| Package | Files |
|---|---|
| 1 Math | `src/model/math/{Gas,Acoustics,Flow,Integrate,Smooth}.h`, `tests/Spectrum.h`, `tests/test_math.cpp` |
| 2 Acoustics | `src/model/acoustics/{Cell,Link}.h`, `{Tube,Reservoir,Valve,Radiation,Turbulence}.{h,cpp}`, `tests/test_tube.cpp`, `tests/test_reservoir.cpp` |
| 3 Source | `src/model/acoustics/Sphincter.{h,cpp}`, `src/model/control/PitchControl.{h,cpp}`, `tests/test_sphincter.cpp` |
| 4 Geometry and reflex | `src/model/geometry/{AreaFunction,Peristalsis}.h`, `{Anatomy,Articulation}.{h,cpp}`, `src/model/control/Reflex.{h,cpp}`, `tests/test_geometry.cpp` |
| 5 Model | `src/model/BurpModel.{h,cpp}`, `tests/test_model.cpp`, `tools/WavWriter.h`, `tools/render_wav.cpp` |
| 6 Engine | `src/audio_engine/{Params,Decimator,ControlMapping,Engine}.{h,cpp}`, `NoteStack.h`, `tests/test_engine.cpp` |
| 7 Plugin and build | `src/plugin/PluginProcessor.{h,cpp}`, `PluginController.{h,cpp}`, every `CMakeLists.txt` except `src/gui`, `readme.md` |
| 8 Editor | `src/gui/Editor.{h,cpp}`, `src/gui/BodyView.{h,cpp}` (body cross-section: live parameters as shapes, gas bubbles moving at the measured flows, scaled down), `src/audio_engine/Meters.h` (display values the processor sends as read-only output parameters), `src/gui/CMakeLists.txt` |

Shared files already complete: `src/model/math/Units.h`, `src/model/BurpControls.h`,
`tests/Check.h`.

## 8. Tests

Each test is a plain executable registered with CTest. It prints what it
measured and returns the number of failed checks.

| Test | Checks |
|---|---|
| `test_math` | c = 353 m/s in air at 37 C; unit conversions; note 69 = 440 Hz; a mass-spring rings at sqrt(k/m) |
| `test_tube` | A 17 cm closed-open tube rings near 519, 1557 and 2596 Hz; pressure stays finite |
| `test_reservoir` | Starting pressure matches the closed form; pressure falls monotonically as gas leaves |
| `test_sphincter` | Under constant pressure the UES oscillates; prints the frequency for a few tensions |
| `test_geometry` | Areas stay positive; lip and tongue controls move the right sections; reflex phases occur in order |
| `test_model` | A triggered burp is finite, audible, and silent after release; prints peak level and pitch |
| `test_engine` | Parameter round trips; legato note logic; decimator passes DC and rejects the upper band |

`tools/render_wav` writes a burp to a WAV file for listening without a DAW.

## 9. Known risks

- None of this has been compiled. Expect compile errors on the first build.
- The UES is a nonlinear oscillator tuned from vocal-fold constants. It may
  not start, or may start at the wrong pitch, until UES Mass, UES Damping and
  the resting gap are tuned with `test_sphincter`.
- The engine soft-clips its output and resets the model if a sample is not
  finite, so a blown-up simulation cannot reach the speakers at full scale.

## Notes from end of 11/11 6pm session
  - Sustain wiht bubbles is wierd look into it or just let sustain be based on paramaters
  so changing paramaters during release can lead to a sustain effect.
  - Still alot of work to figure out which paramaters should make it the UI and how
  to have different changing paramaters interact witheachother in a true to 
  burp manner.