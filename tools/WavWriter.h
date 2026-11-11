#pragma once

// Writes mono 16-bit PCM WAV files, for listening to the model without a DAW.
//
// A WAV file is a 44-byte RIFF header followed by the samples as
// little-endian signed 16-bit integers.
//
// Header-only: the function is defined inline in this file.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace burp::tools {

// Samples are clipped to -1..1. Returns false if the file cannot be written.
inline bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    auto writeTag = [&file](const char* tag) { file.write(tag, 4); };
    auto write16 = [&file](std::uint16_t value) {
        const char bytes[2] = { static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8) & 0xFFu) };
        file.write(bytes, 2);
    };
    auto write32 = [&file](std::uint32_t value) {
        const char bytes[4] = { static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8) & 0xFFu),
                                static_cast<char>((value >> 16) & 0xFFu),
                                static_cast<char>((value >> 24) & 0xFFu) };
        file.write(bytes, 4);
    };

    const std::uint16_t channels = 1;
    const std::uint16_t bitsPerSample = 16;
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bitsPerSample / 8);
    const std::uint32_t rate = static_cast<std::uint32_t>(sampleRate);
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size()) * blockAlign;

    writeTag("RIFF");
    write32(36 + dataBytes);       // RIFF chunk size: everything after this field
    writeTag("WAVE");
    writeTag("fmt ");
    write32(16);                   // fmt chunk size
    write16(1);                    // PCM
    write16(channels);
    write32(rate);
    write32(rate * blockAlign);    // byte rate
    write16(blockAlign);
    write16(bitsPerSample);
    writeTag("data");
    write32(dataBytes);

    for (const float sample : samples) {
        const long value = std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        write16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
    }

    return file.good();
}

} // namespace burp::tools
