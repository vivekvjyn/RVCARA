#pragma once

#include <vector>

namespace rvcara
{
/** @brief One second-order section, in the b0 b1 b2 a0 a1 a2 order SciPy writes. */
struct BiquadCoefficients
{
    double b0 { 1.0 };
    double b1 { 0.0 };
    double b2 { 0.0 };
    double a0 { 1.0 };
    double a1 { 0.0 };
    double a2 { 0.0 };
};

/** @brief Forward-backward biquad cascade, matching scipy.signal.filtfilt including its steady-state initialisation. */
class ZeroPhaseFilter
{
public:
    ZeroPhaseFilter() = default;

    ZeroPhaseFilter (std::vector<BiquadCoefficients> sections, int padLength);

    void process (float* samples, int numSamples) const;

    [[nodiscard]] int getPadLength() const noexcept { return padding; }

    [[nodiscard]] bool isEmpty() const noexcept { return cascade.empty(); }

private:
    void processForward (double* samples, int numSamples) const;

    std::vector<BiquadCoefficients> cascade;
    int padding { 0 };
};
}
