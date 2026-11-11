#pragma once

// 2:1 decimator: brings the model's oversampled output down to the host rate.
//
// The model runs at twice the host rate so that the nonlinear sphincter does
// not alias. Before every second sample is dropped, everything above the
// host's Nyquist frequency must be removed. This is a windowed-sinc lowpass
// FIR with its cutoff at a quarter of the input rate:
//   h[n] = sinc((n - M) / 2) / 2 * w[n],  n = 0..2M,  w = Blackman window
// with the taps normalised to sum to 1 (unity gain at DC).
// See J.O. Smith, "Spectral Audio Signal Processing", Window Method.

#include <array>

namespace burp {

class Decimator {
public:
    static constexpr int kHalfLength = 31; // M: the filter has 2M + 1 taps

    void prepare(); // computes the taps
    void reset();   // clears the history

    // Reads 2 * numOut input samples and writes numOut output samples.
    void process(const double* in, double* out, int numOut);

private:
    static constexpr int kTaps = 2 * kHalfLength + 1;

    void push(double sample);

    std::array<double, kTaps> taps_ {};

    // Each input is stored twice, kTaps apart, so the newest kTaps samples
    // are always contiguous, oldest first, starting at history_[next_].
    std::array<double, 2 * kTaps> history_ {};
    int next_ = 0;
};

} // namespace burp
