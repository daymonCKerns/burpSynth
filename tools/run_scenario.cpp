// run_scenario: plays scripted scenarios through the Engine and documents
// what happened, for finding and comparing sound problems without a DAW.
//
// Usage: run_scenario [-o output_dir] scenario.txt [more.txt ...]
//   default output_dir: build/renders
//
// Scenario file: one event per line, "#" starts a comment. Times in seconds.
//   <t> note_on  <key> <velocity 0..1>
//   <t> note_off <key>
//   <t> set  <Param> <value>                    plain units, as in the editor
//   <t> ramp <Param> <from> <to> <seconds>      linear
//   <t> end                                     stop rendering here
// <Param> is a parameter name from Params.cpp without spaces, any case
// (e.g. TonguePosition). Events take effect at the next 1 ms block.
//
// For each scenario <name>, writes into output_dir:
//   <name>.wav          the host-rate output (48 kHz, mono)
//   <name>_trace.csv    every 1 ms: output peak, level before the soft clip,
//                       the model's pressures, flows and openings
//   <name>_summary.txt  peak, samples pushed into the soft clip, the largest
//                       sample-to-sample jump, the loudest 10 ms windows, and
//                       the level around each note-off. Also printed.
// The trace's model values are snapshots at the end of each 1 ms block, not
// peaks within it.
//
// Level before the soft clip: the engine's last step is tanh, so
// atanh(|out|) recovers its input. Above 1.0 (|out| > tanh 1 = 0.76) the
// output is audibly squashed; the summary counts those samples.

#include "WavWriter.h"
#include "audio_engine/Engine.h"
#include "audio_engine/Params.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kBlockFrames = kSampleRate / 1000; // 1 ms
constexpr double kBlockSeconds = 0.001;
constexpr int kWindowBlocks = 10;                // loudest-window length, ms
constexpr int kLoudestCount = 5;
constexpr int kAroundOffBlocks = 100;            // ms compared before/after each note-off
constexpr double kMaxSeconds = 60.0;             // safety stop when a scenario has no end

enum class Kind { NoteOn, NoteOff, Set, Ramp, End };

struct Event {
    double time = 0.0;
    Kind kind = Kind::End;
    int key = 0;
    double velocity = 0.0;
    burp::ParamId param {};
    double from = 0.0;
    double to = 0.0;
    double duration = 0.0;
    std::string text; // the line, for the summary
};

struct Ramp {
    burp::ParamId param;
    double start;
    double from;
    double to;
    double duration;
};

std::string squeeze(const std::string& name)
{
    std::string out;
    for (const char c : name) {
        if (c != ' ') {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

std::optional<burp::ParamId> findParam(const std::string& name)
{
    for (const burp::ParamInfo& info : burp::paramTable()) {
        if (squeeze(info.name) == squeeze(name)) {
            return info.id;
        }
    }
    return std::nullopt;
}

const burp::ParamInfo& infoOf(burp::ParamId id)
{
    return burp::paramTable()[static_cast<std::size_t>(burp::paramIndex(id))];
}

// Returns false and prints the line on a parse error.
bool parseScenario(const std::string& path, std::vector<Event>& events)
{
    std::ifstream file(path);
    if (!file) {
        std::printf("cannot open %s\n", path.c_str());
        return false;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string body = line.substr(0, line.find('#'));
        std::istringstream in(body);
        Event event;
        std::string command;
        if (!(in >> event.time)) {
            continue; // blank or comment
        }
        in >> command;
        event.text = body.substr(0, body.find_last_not_of(" \t") + 1);

        bool ok = true;
        std::string name;
        if (command == "note_on") {
            event.kind = Kind::NoteOn;
            ok = static_cast<bool>(in >> event.key >> event.velocity);
        } else if (command == "note_off") {
            event.kind = Kind::NoteOff;
            ok = static_cast<bool>(in >> event.key);
        } else if (command == "set") {
            event.kind = Kind::Set;
            ok = static_cast<bool>(in >> name >> event.to);
        } else if (command == "ramp") {
            event.kind = Kind::Ramp;
            ok = static_cast<bool>(in >> name >> event.from >> event.to >> event.duration);
        } else if (command == "end") {
            event.kind = Kind::End;
        } else {
            ok = false;
        }

        if (ok && !name.empty()) {
            const std::optional<burp::ParamId> id = findParam(name);
            ok = id.has_value();
            if (ok) {
                event.param = *id;
            }
        }
        if (!ok) {
            std::printf("%s:%d: cannot read \"%s\"\n", path.c_str(), lineNumber, line.c_str());
            return false;
        }
        events.push_back(event);
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) { return a.time < b.time; });
    return true;
}

void setPlain(burp::Engine& engine, burp::ParamId id, double plain)
{
    engine.setParamNormalized(static_cast<int>(id), burp::toNormalized(infoOf(id), plain));
}

double toDb(double level)
{
    return level > 0.0 ? 20.0 * std::log10(level) : -999.0;
}

double preClip(double out)
{
    return std::atanh(std::min(std::abs(out), 0.9999999));
}

int runScenario(const std::string& path, const std::filesystem::path& outputDir)
{
    std::vector<Event> events;
    if (!parseScenario(path, events)) {
        return 1;
    }
    const std::string name = std::filesystem::path(path).stem().string();

    burp::Engine engine;
    engine.prepare(static_cast<double>(kSampleRate), kBlockFrames);

    std::vector<float> left(kBlockFrames);
    std::vector<float> right(kBlockFrames);
    float* outputs[] = { left.data(), right.data() };
    std::vector<float> recording;
    std::vector<Ramp> ramps;
    std::vector<double> blockPeaks;
    std::vector<int> noteOffBlocks;

    std::ofstream trace(outputDir / (name + "_trace.csv"));
    trace << "time_ms,out_peak,preclip_peak,stomach_Pa,esophagus_Pa,pharynx_Pa,mouth_Pa,"
             "ues_area_mm2,ues_flow_mLs,les_flow_mLs,les_open,ues_open,wave_centre,measured_Hz,asleep\n";

    std::size_t next = 0;
    bool ended = false;
    const int maxBlocks = static_cast<int>(kMaxSeconds / kBlockSeconds);
    for (int block = 0; block < maxBlocks && !ended; ++block) {
        const double t = block * kBlockSeconds;

        for (; next < events.size() && events[next].time <= t + 1e-9; ++next) {
            const Event& event = events[next];
            switch (event.kind) {
            case Kind::NoteOn:
                engine.noteOn(event.key, static_cast<double>(event.key), event.velocity);
                break;
            case Kind::NoteOff:
                engine.noteOff(event.key);
                noteOffBlocks.push_back(block);
                break;
            case Kind::Set:
                setPlain(engine, event.param, event.to);
                break;
            case Kind::Ramp:
                ramps.push_back({ event.param, event.time, event.from, event.to, event.duration });
                break;
            case Kind::End:
                ended = true;
                break;
            }
        }
        if (ended) {
            break;
        }

        for (const Ramp& ramp : ramps) {
            const double progress =
                ramp.duration > 0.0 ? std::clamp((t - ramp.start) / ramp.duration, 0.0, 1.0) : 1.0;
            setPlain(engine, ramp.param, ramp.from + (ramp.to - ramp.from) * progress);
        }

        engine.render(outputs, 2, kBlockFrames);
        recording.insert(recording.end(), left.begin(), left.end());

        double peak = 0.0;
        for (const float sample : left) {
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
        }
        blockPeaks.push_back(peak);

        const burp::BurpModel& model = engine.model();
        const burp::ReflexOutput& reflex = model.reflexOutput();
        char row[512];
        std::snprintf(row, sizeof row,
                      "%d,%.5f,%.5f,%.2f,%.2f,%.3f,%.3f,%.4f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%d\n", block,
                      peak, preClip(peak), model.stomachPressure(), model.esophagusPressure(),
                      model.pharynxPressure(), model.mouthPressure(), model.uesArea() * 1.0e6,
                      model.uesFlow() * 1.0e6, model.lesFlow() * 1.0e6, reflex.lesOpen, reflex.uesOpen,
                      reflex.waveCentre, model.measuredHz(), model.isAsleep() ? 1 : 0);
        trace << row;
    }

    // Summary.
    double peak = 0.0;
    double biggestJump = 0.0;
    std::size_t jumpAt = 0;
    int clipped = 0;
    const double clipLevel = std::tanh(1.0);
    for (std::size_t i = 0; i < recording.size(); ++i) {
        const double sample = static_cast<double>(recording[i]);
        peak = std::max(peak, std::abs(sample));
        clipped += std::abs(sample) > clipLevel ? 1 : 0;
        if (i > 0) {
            const double jump = std::abs(sample - static_cast<double>(recording[i - 1]));
            if (jump > biggestJump) {
                biggestJump = jump;
                jumpAt = i;
            }
        }
    }

    std::vector<std::pair<double, int>> windows; // peak, start ms
    for (int start = 0; start + kWindowBlocks <= static_cast<int>(blockPeaks.size()); start += kWindowBlocks) {
        const auto first = blockPeaks.begin() + start;
        windows.emplace_back(*std::max_element(first, first + kWindowBlocks), start);
    }
    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    auto rangePeak = [&blockPeaks](int from, int to) {
        double result = 0.0;
        for (int b = std::max(from, 0); b < std::min(to, static_cast<int>(blockPeaks.size())); ++b) {
            result = std::max(result, blockPeaks[static_cast<std::size_t>(b)]);
        }
        return result;
    };

    std::ostringstream summary;
    char text[256];
    summary << "scenario " << name << " (" << path << ")\n";
    for (const Event& event : events) {
        summary << "  " << event.text << "\n";
    }
    std::snprintf(text, sizeof text, "length %.3f s\n", static_cast<double>(recording.size()) / kSampleRate);
    summary << text;
    std::snprintf(text, sizeof text, "peak %.4f (%.1f dBFS), before the soft clip %.3f (%.1f dB)\n", peak,
                  toDb(peak), preClip(peak), toDb(preClip(peak)));
    summary << text;
    std::snprintf(text, sizeof text, "samples pushed into the soft clip (|out| > %.3f): %d (%.2f%%)\n",
                  clipLevel, clipped, recording.empty() ? 0.0 : 100.0 * clipped / recording.size());
    summary << text;
    std::snprintf(text, sizeof text, "largest sample-to-sample jump %.4f at %.4f s\n", biggestJump,
                  static_cast<double>(jumpAt) / kSampleRate);
    summary << text;
    summary << "loudest 10 ms windows:\n";
    for (int i = 0; i < std::min(kLoudestCount, static_cast<int>(windows.size())); ++i) {
        std::snprintf(text, sizeof text, "  %7.3f s  %.4f (%.1f dBFS)\n", windows[i].second * kBlockSeconds,
                      windows[i].first, toDb(windows[i].first));
        summary << text;
    }
    for (const int off : noteOffBlocks) {
        const double before = rangePeak(off - kAroundOffBlocks, off);
        const double after = rangePeak(off, off + kAroundOffBlocks);
        std::snprintf(text, sizeof text,
                      "note-off at %.3f s: peak %.1f dBFS in the 100 ms before, %.1f dBFS in the 100 ms after "
                      "(%+.1f dB)\n",
                      off * kBlockSeconds, toDb(before), toDb(after), toDb(after) - toDb(before));
        summary << text;
    }

    const std::filesystem::path wavPath = outputDir / (name + ".wav");
    if (!burp::tools::writeWav(wavPath.string(), recording, kSampleRate)) {
        std::printf("could not write %s\n", wavPath.string().c_str());
        return 1;
    }
    std::ofstream(outputDir / (name + "_summary.txt")) << summary.str();
    std::printf("%s\n", summary.str().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path outputDir = "build/renders";
    std::vector<std::string> scenarios;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputDir = argv[++i];
        } else {
            scenarios.push_back(arg);
        }
    }
    if (scenarios.empty()) {
        std::printf("usage: run_scenario [-o output_dir] scenario.txt [more.txt ...]\n");
        return 1;
    }

    std::filesystem::create_directories(outputDir);
    int failures = 0;
    for (const std::string& scenario : scenarios) {
        failures += runScenario(scenario, outputDir);
    }
    std::printf("outputs in %s\n", outputDir.string().c_str());
    return failures;
}
