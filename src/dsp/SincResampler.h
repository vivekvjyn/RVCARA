#pragma once

#include <vector>

namespace rvcara
{
/** @brief Kaiser-windowed sinc resampler whose cutoff tracks the rate ratio, so downsampling cannot alias. */
class SincResampler
{
public:
    static constexpr double passbandRolloff = 0.945;

    static constexpr double kaiserBeta = 9.4;

    static constexpr int numZeroCrossings = 32;

    SincResampler (double sourceSampleRate, double targetSampleRate);

    [[nodiscard]] std::vector<float> process (const float* source, int numSamples) const;

    [[nodiscard]] int getOutputLength (int numSamples) const noexcept;

    [[nodiscard]] bool isPassThrough() const noexcept { return isUnity; }

private:
    [[nodiscard]] float kernelAt (double distanceInInputSamples) const noexcept;

    double ratio { 1.0 };
    double cutoff { 1.0 };
    double kernelHalfWidth { 0.0 };
    bool isUnity { true };

    std::vector<float> kernelTable;
    double tableEntriesPerInputSample { 0.0 };
};

/** @brief Zeroth-order modified Bessel function of the first kind, which defines the Kaiser window.
    @param x  Argument.
    @return I0(x).
*/
double modifiedBesselI0 (double x) noexcept;
}
