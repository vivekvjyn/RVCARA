#include "dsp/SincResampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rvcara
{
namespace
{
    constexpr int kernelTableResolution = 512;

    double normalisedSinc (double x) noexcept
    {
        if (std::abs (x) < 1.0e-12)
            return 1.0;

        const auto scaled = std::numbers::pi * x;
        return std::sin (scaled) / scaled;
    }
}

double modifiedBesselI0 (double x) noexcept
{
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

    cutoff = passbandRolloff * std::min (1.0, 1.0 / ratio);

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
        const auto centre = static_cast<double> (outputIndex) * ratio;

        const auto firstTap = static_cast<int> (std::ceil (centre - kernelHalfWidth));
        const auto lastTap = static_cast<int> (std::floor (centre + kernelHalfWidth));

        double sum = 0.0;
        double weightSum = 0.0;

        for (int tapIndex = firstTap; tapIndex <= lastTap; ++tapIndex)
        {
            const auto weight = static_cast<double> (kernelAt (static_cast<double> (tapIndex) - centre));

            if (tapIndex < 0 || tapIndex >= numSamples)
                continue;

            sum += weight * static_cast<double> (source[tapIndex]);
            weightSum += weight;
        }

        destination[static_cast<std::size_t> (outputIndex)] =
            static_cast<float> (std::abs (weightSum) > 1.0e-9 ? sum / weightSum : 0.0);
    }

    return destination;
}
}
