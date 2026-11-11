#pragma once

// Frequency measurements for tests: find where a signal's energy is without
// an FFT library. Uses the Goertzel algorithm, which evaluates one frequency
// of the discrete Fourier transform at a time.
//
// Header-only: every function is defined inline in this file.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace burp::test {

// Magnitude of `signal` at one frequency (Goertzel).
//   s_n = x_n + 2 cos(w) s_(n-1) - s_(n-2),  w = 2 pi f / fs
//   |X| = sqrt(s_(N-1)^2 + s_(N-2)^2 - 2 cos(w) s_(N-1) s_(N-2))
inline double magnitudeAt(const std::vector<double>& signal, double sampleRate, double hz)
{
    const double coefficient = 2.0 * std::cos(2.0 * std::numbers::pi * hz / sampleRate);
    double previous = 0.0;
    double beforePrevious = 0.0;
    for (const double sample : signal) {
        const double current = sample + coefficient * previous - beforePrevious;
        beforePrevious = previous;
        previous = current;
    }
    const double power = previous * previous + beforePrevious * beforePrevious
                         - coefficient * previous * beforePrevious;
    return std::sqrt(std::max(power, 0.0));
}

// Frequency of the largest magnitude between minHz and maxHz, scanned in
// steps of stepHz.
inline double peakFrequency(const std::vector<double>& signal, double sampleRate, double minHz,
                            double maxHz, double stepHz)
{
    const int steps = static_cast<int>(std::floor((maxHz - minHz) / stepHz));
    double bestHz = minHz;
    double bestMagnitude = -1.0;
    for (int i = 0; i <= steps; ++i) {
        const double hz = minHz + static_cast<double>(i) * stepHz;
        const double magnitude = magnitudeAt(signal, sampleRate, hz);
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            bestHz = hz;
        }
    }
    return bestHz;
}

// Fundamental frequency from the average spacing of upward zero crossings of
// the signal with its mean removed. Returns 0 if there are fewer than three.
// Each crossing time is interpolated linearly between the two samples around it.
inline double zeroCrossingFrequency(const std::vector<double>& signal, double sampleRate)
{
    double mean = 0.0;
    for (const double sample : signal) {
        mean += sample;
    }
    mean /= static_cast<double>(signal.size());

    int crossings = 0;
    double firstTime = 0.0;
    double lastTime = 0.0;
    for (std::size_t i = 1; i < signal.size(); ++i) {
        const double before = signal[i - 1] - mean;
        const double after = signal[i] - mean;
        if (before < 0.0 && after >= 0.0) {
            const double time = static_cast<double>(i - 1) + before / (before - after);
            if (crossings == 0) {
                firstTime = time;
            }
            lastTime = time;
            ++crossings;
        }
    }
    if (crossings < 3) {
        return 0.0;
    }
    return sampleRate * static_cast<double>(crossings - 1) / (lastTime - firstTime);
}

} // namespace burp::test
