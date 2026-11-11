#pragma once

// Engine: the host-agnostic entry point the plugin drives. It turns notes
// and parameter changes into calls on the one BurpModel and brings the
// model's output to the host's sample rate.
//
// Notes (monophonic, legato):
//   noteOn, nothing held    -> BurpModel::trigger
//   noteOn, a note is held  -> BurpModel::changeNote (glide + pressure accent)
//   noteOff, others held    -> BurpModel::changeNote to the newest held note,
//                              velocity 0 (no accent), if the sounding note changed
//   noteOff, last note      -> BurpModel::release
//
// Audio, per render call:
//   1. makeControls(current parameter values) -> BurpModel::setControls
//   2. the model renders kOversampling * numFrames samples (Pa)
//   3. scale by outputGain / kFullScalePascals
//   4. Decimator -> host rate
//   5. soft clip: tanh
//   6. the same signal is written to every output channel
// If any sample is not finite the model is reset and the block is silence,
// so a simulation that blows up cannot reach the speakers.
//
// render() never allocates or locks. No VST3 types here.

#include "audio_engine/Decimator.h"
#include "audio_engine/NoteStack.h"
#include "audio_engine/Params.h"
#include "model/BurpModel.h"

#include <vector>

namespace burp {

inline constexpr int kOversampling = 2;
inline constexpr double kFullScalePascals = 2.0; // sound pressure that maps to 1.0

class Engine {
public:
    // Allocates; call off the audio thread.
    void prepare(double hostSampleRate, int maxBlockSize);

    // Silences the model and forgets held notes; keeps parameter values.
    void reset();

    // semitones is the key number plus any tuning offset. velocity 0..1.
    void noteOn(int key, double semitones, double velocity);
    void noteOff(int key);

    // Unknown IDs are ignored.
    void setParamNormalized(int rawId, double normalized);

    // Longer calls are rendered in chunks of maxBlockSize.
    void render(float* const* outputs, int numChannels, int numFrames);

private:
    // Steps 2 to 6 for at most maxBlockSize_ frames, written from
    // outputs[channel][offset].
    void renderChunk(float* const* outputs, int numChannels, int offset, int numFrames,
                     double gain);

    BurpModel model_;
    Decimator decimator_;
    NoteStack notes_;
    ParamValues values_ = defaultParamValues(); // plain values
    std::vector<double> modelBuffer_;           // kOversampling * maxBlockSize_, Pa
    std::vector<double> hostBuffer_;            // maxBlockSize_, host rate
    double hostSampleRate_ = 0.0;
    int maxBlockSize_ = 0;
};

} // namespace burp
