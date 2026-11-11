# Summary
BurpSynth is a C++ VST3 instrument plugin. It is a physical modeling synth
which is a model of the human gastrointestinal system from the stomach up to the
mouth with the parameters and control tailored to produce burping and belching
sounds. It runs inside a DAW; there is no standalone app. Notes and parameter
automation come from the DAW. It is monophonic: one body, one burp at a time.

# Directory Structure
/src - contains all source code for the:
    model - where the geometry and physics simulation are constructed and run.
        Pure C++, no VST3 SDK or host dependencies. Subfolders:
            math - stateless helper functions, one equation each
            acoustics - audio-rate solver parts (cells, links, tubes, valves)
            geometry - control-rate shapes (area functions, articulation)
            control - reflex timeline and pitch control
    audio_engine - host-agnostic DSP entry point that drives the model
        (note handling, parameters, oversampling). No VST3 SDK dependencies.
    plugin - VST3 SDK glue (factory, processor, controller). The only code
        that talks to the DAW.
    gui - plugin editor: one VSTGUI slider per parameter, built in code.
/libs - Where the dependencies will be held
    vst3sdk - Steinberg VST3 SDK (git submodule), includes VSTGUI
    eigen - linear algebra (git submodule)
/docs - references and design notes. implementation_plan.md is the design
    the code follows: equations, parameter table, tests.
/tests - one small test executable per module, run with ctest
/tools - render_wav: offline WAV render for listening without a DAW
/assets - visual assets for GUI

# Tech Specs
Language - C++ primarily
Plugin format - VST3 only for now (AU possible later)
Plugin SDK - Steinberg VST3 SDK, used directly
Framework - No app frameworks like JUCE
GUI library - VSTGUI (ships with the VST3 SDK). Basic controls only for now.
Linear algebra - Eigen
Other libraries - no physics or DSP libraries; the model, solvers and DSP are
    written in house. The C++ standard library (e.g. <random>) is fine.
Target OS - macOS 13+
C++ standard - C++20
Compiler - Apple Clang
Build system - CMake (see README)

# Convention
Don't ever build or run tests that is my job
Ask before running terminal commands. Read-only commands inside the
    repository (ls, find, grep, cat, git status) are fine without asking.

# Architecture of synthesis model
Signal path (gas and sound travel upward):
    abdominal pressure -> stomach -> lower barrier (LES) -> esophagus ->
    upper sphincter (UES, the sound source) -> pharynx + mouth -> lips
    with a nasal branch off the pharynx through the velum -> nostrils

Parts
    Stomach - elastic gas reservoir. The gas law, wall tension and abdominal
        pressure set its pressure. It empties as the burp proceeds.
    LES - gate. The reflex controller ramps its opening area. It does not
        vibrate.
    Esophagus - collapsible 1D tube. A contraction wave travels up it.
    UES - two coupled mass-spring-dampers (a lower and an upper edge) driven
        by the pressure across them, with a collision force when shut. Its
        tension sets the pitch.
    Pharynx + mouth, nasal cavity - 1D tubes shaped by area functions.
    Lips, nostrils - radiation loads. Their flow is the audio output.

Solver
    1D staggered-grid (leapfrog) network built from two primitives:
        Cell - pressure in a volume:  P += dt/C * (flow in - flow out)
        Link - flow between cells:    U += dt/L * (P_up - P_down - R*U)
    Tubes are chains of cells and links. Valves are links whose area changes.
    Fixed topology: BurpModel holds each part as a named member and wires
    them with an explicit update order. There is no generic graph.

Two rates
    Control rate (about 1 ms): reflex timeline, articulation, peristalsis.
        Produces target area functions.
    Audio rate (2x the host rate, then decimated): pressures, flows and the
        UES oscillator.

Geometry is separate from the acoustics: the geometry layer only produces
lengths, areas and volumes, and the solver only consumes them.

Control
    MIDI: note-on triggers the reflex sequence, pitch sets UES tension,
        velocity scales the gas amount. Pitch bend offsets the pitch.
    Legato: a note pressed while another is held does not start a new burp.
        The same burp glides to the new pitch at the glissando rate, and
        the new note's velocity adds a short pressure accent.
    Pitch accuracy is prioritised at the start of a note and when the note
        changes. While a note is held, pitch changes caused by other
        parameters are kept as expression.
    Note-off cuts the burp short: the LES and UES close, and the gas already
        in the esophagus drains out as a short decaying tail.
    Host parameters: abdominal pressure and mouth shape are continuous and
        can change during a burp.

Code rules for the model
    Built directly on the math: one equation per function, named for the
        equation, with its source in a comment. No fluff.
    math/ holds stateless free functions. Parts are concrete classes.
    SI units internally. Convert at the edges.
    No allocation, locks or I/O while rendering. Sizes are fixed in prepare().
