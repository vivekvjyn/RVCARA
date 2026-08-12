#include "SincResampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rvcara
{

namespace
{
    // Samples of the kernel stored per input sample. The kernel is smooth on this
    // scale, so linear interpolation between entries costs nothing measurable in
    // accuracy and turns each tap into two loads and a multiply-add.
    constexpr int kernelTableResolution = 512;

    /** Normalised sinc, sin(pi x) / (pi x), continuous at zero. */
    double normalisedSinc (double x) noexcept
    {
        if (std::abs (x) < 1.0e-12)
            return 1.0;

        const auto scaled = std::numbers::pi * x;
        return std::sin (scaled) / scaled;
    }
} // namespace

double modifiedBesselI0 (double x) noexcept
{
    // Power series I0(x) = sum (x^2/4)^k / (k!)^2. Each term is formed from the last,
    // so no factorial is ever evaluated and the loop is numerically well behaved for
    // the arguments a Kaiser window produces, which never exceed beta.
    const auto quarterSquared = 0.25 * x * x;

    double sum = 1.0;
    double term = 1.0;

    for (int k = 1; k < 64; ++k)
    {
        term *= quarterSquared / (static_cast<double> (k) * static_cast<double> (k));
        sum += term;

        if (term < 1.0e-16 * sum)
            break;
    }

    return sum;
}

SincResampler::SincResampler (double sourceSampleRate, double targetSampleRate)
{
    if (sourceSampleRate <= 0.0 || targetSampleRate <= 0.0)
        return;

    ratio = sourceSampleRate / targetSampleRate;
    isUnity = std::abs (ratio - 1.0) < 1.0e-12;

    if (isUnity)
        return;

    // When downsampling, the passband must be the *output* Nyquist expressed as a
    // fraction of the input Nyquist, which is 1 / ratio. When upsampling there is no
    // new band to protect, so the kernel stays at full input bandwidth.
    cutoff = passbandRolloff * std::min (1.0, 1.0 / ratio);

    // A narrower cutoff stretches the sinc's zero crossings, so a fixed number of
    // them spans proportionally more input samples.
    kernelHalfWidth = static_cast<double> (numZeroCrossings) / cutoff;

    tableEntriesPerInputSample = static_cast<double> (kernelTableResolution);
    const auto numEntries = static_cast<std::size_t> (kernelHalfWidth * tableEntriesPerInputSample) + 2;

    kernelTable.resize (numEntries);
    const auto besselAtBeta = modifiedBesselI0 (kaiserBeta);

    for (std::size_t entryIndex = 0; entryIndex < numEntries; ++entryIndex)
    {
        const auto distance = static_cast<double> (entryIndex) / tableEntriesPerInputSample;
        const auto normalised = distance / kernelHalfWidth;

        if (normalised >= 1.0)
        {
            kernelTable[entryIndex] = 0.0f;
            continue;
        }

        const auto window =
            modifiedBesselI0 (kaiserBeta * std::sqrt (1.0 - normalised * normalised)) / besselAtBeta;

        kernelTable[entryIndex] = static_cast<float> (cutoff * normalisedSinc (cutoff * distance) * window);
    }
}

float SincResampler::kernelAt (double distanceInInputSamples) const noexcept
{
    const auto position = std::abs (distanceInInputSamples) * tableEntriesPerInputSample;
    const auto lowerIndex = static_cast<std::size_t> (position);

    if (lowerIndex + 1 >= kernelTable.size())
        return 0.0f;

    const auto fraction = static_cast<float> (position - static_cast<double> (lowerIndex));

    return kernelTable[lowerIndex] * (1.0f - fraction) + kernelTable[lowerIndex + 1] * fraction;
}

int SincResampler::getOutputLength (int numSamples) const noexcept
{
    if (numSamples <= 0)
        return 0;

    if (isUnity)
        return numSamples;

    return static_cast<int> (std::llround (static_cast<double> (numSamples) / ratio));
}

std::vector<float> SincResampler::process (const float* source, int numSamples) const
{
    if (source == nullptr || numSamples <= 0)
        return {};

    if (isUnity || kernelTable.empty())
        return { source, source + numSamples };

    const auto numOutputSamples = getOutputLength (numSamples);
    std::vector<float> destination (static_cast<std::size_t> (numOutputSamples));

    for (int outputIndex = 0; outputIndex < numOutputSamples; ++outputIndex)
    {
        // The centre of this output sample expressed in input samples. Because the
        // kernel is symmetric about this point, the filter contributes no delay and
        // the output stays time-aligned with the input.
        const auto centre = static_cast<double> (outputIndex) * ratio;

        const auto firstTap = static_cast<int> (std::ceil (centre - kernelHalfWidth));
        const auto lastTap = static_cast<int> (std::floor (centre + kernelHalfWidth));

        double sum = 0.0;
        double weightSum = 0.0;

        for (int tapIndex = firstTap; tapIndex <= lastTap; ++tapIndex)
        {
            const auto weight = static_cast<double> (kernelAt (static_cast<double> (tapIndex) - centre));

            // Taps that fall outside the signal are dropped and the remaining weights
            // renormalised, rather than reading zeros. Treating the outside as silence
            // would attenuate the first and last few milliseconds of every region.
            if (tapIndex < 0 || tapIndex >= numSamples)
                continue;

            sum += weight * static_cast<double> (source[tapIndex]);
            weightSum += weight;
        }

        // The kernel's weights sum to one only for a complete tap set; near the ends,
        // and to absorb the table's quantisation, normalise by what was actually used.
        destination[static_cast<std::size_t> (outputIndex)] =
            static_cast<float> (std::abs (weightSum) > 1.0e-9 ? sum / weightSum : 0.0);
    }

    return destination;
}

} // namespace rvcara
