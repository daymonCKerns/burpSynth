// render_wav: plays one burp through the Engine and writes it to a WAV file,
// for listening and measuring without a DAW.
//
// Usage: render_wav [output.wav] [note] [velocity] [hold seconds]
//   defaults: burp.wav, note 45, velocity 0.8, hold 1.0
//
// Renders at 48 kHz in 512-frame blocks: note on, hold, note off, then one
// more second for the tail. Prints how much of one CPU core the render took,
// the peak level and the file path. Only a release build gives a meaningful
// CPU figure.

#include "WavWriter.h"
#include "audio_engine/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kBlockSize = 512;
constexpr double kTailSeconds = 1.0;

} // namespace

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "burp.wav";
    const int key = argc > 2 ? std::atoi(argv[2]) : 45;
    const double velocity = argc > 3 ? std::atof(argv[3]) : 0.8;
    const double holdSeconds = argc > 4 ? std::atof(argv[4]) : 1.0;

    burp::Engine engine;
    engine.prepare(static_cast<double>(kSampleRate), kBlockSize);

    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);
    float* outputs[] = { left.data(), right.data() };
    std::vector<float> recording;

    auto renderFor = [&](double seconds) {
        const long total = std::lround(seconds * kSampleRate);
        for (long done = 0; done < total; done += kBlockSize) {
            const int frames = static_cast<int>(std::min<long>(kBlockSize, total - done));
            engine.render(outputs, 2, frames);
            recording.insert(recording.end(), left.begin(), left.begin() + frames);
        }
    };

    const auto started = std::chrono::steady_clock::now();
    engine.noteOn(key, static_cast<double>(key), velocity);
    renderFor(holdSeconds);
    engine.noteOff(key);
    renderFor(kTailSeconds);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    // A plugin has to render each block before it plays, on one thread: this
    // is how much of one core the render took, and it must stay well below 100%.
    const double audioSeconds = static_cast<double>(recording.size()) / kSampleRate;
    std::printf("rendered %.2f s of audio in %.3f s: %.1f%% of one core\n", audioSeconds, elapsed,
                100.0 * elapsed / audioSeconds);

    float peak = 0.0f;
    for (const float sample : recording) {
        peak = std::max(peak, std::abs(sample));
    }

    if (!burp::tools::writeWav(path, recording, kSampleRate)) {
        std::printf("could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("peak %.4f\n%s\n", static_cast<double>(peak), path.c_str());
    return 0;
}
