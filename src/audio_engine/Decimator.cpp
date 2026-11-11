// Decimator: the windowed-sinc half-band lowpass and 2:1 downsampling from
// Decimator.h.

#include "audio_engine/Decimator.h"

#include <cmath>
#include <cstddef>
#include <numbers>

namespace burp {

namespace {

// sinc(x) = sin(pi x) / (pi x), sinc(0) = 1.
double sinc(double x)
{
    if (x == 0.0) {
        return 1.0;
    }
    const double angle = std::numbers::pi * x;
    return std::sin(angle) / angle;
}

// Blackman window, n = 0..length - 1:
//   w[n] = 0.42 - 0.5 cos(2 pi n / (N - 1)) + 0.08 cos(4 pi n / (N - 1))
double blackman(int n, int length)
{
    const double angle = 2.0 * std::numbers::pi * static_cast<double>(n)
                         / static_cast<double>(length - 1);
    return 0.42 - 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle);
}

} // namespace

void Decimator::prepare()
{
    double sum = 0.0;
    for (int n = 0; n < kTaps; ++n) {
        const double x = static_cast<double>(n - kHalfLength) / 2.0;
        const double tap = sinc(x) / 2.0 * blackman(n, kTaps);
        taps_[static_cast<std::size_t>(n)] = tap;
        sum += tap;
    }
    for (double& tap : taps_) {
        tap /= sum;
    }
    reset();
}

void Decimator::reset()
{
    history_.fill(0.0);
    next_ = 0;
}

void Decimator::push(double sample)
{
    history_[static_cast<std::size_t>(next_)] = sample;
    history_[static_cast<std::size_t>(next_ + kTaps)] = sample;
    next_ = next_ + 1 == kTaps ? 0 : next_ + 1;
}

void Decimator::process(const double* in, double* out, int numOut)
{
    for (int i = 0; i < numOut; ++i) {
        push(in[2 * i]);
        push(in[2 * i + 1]);

        // The taps are symmetric, so the window need not be reversed.
        const double* window = history_.data() + next_;
        double sum = 0.0;
        for (std::size_t n = 0; n < taps_.size(); ++n) {
            sum += taps_[n] * window[n];
        }
        out[i] = sum;
    }
}

} // namespace burp
