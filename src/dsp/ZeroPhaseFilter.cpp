#include "dsp/ZeroPhaseFilter.h"

#include <algorithm>
#include <utility>

namespace rvcara
{
ZeroPhaseFilter::ZeroPhaseFilter (std::vector<BiquadCoefficients> sections, int padLength)
    : cascade (std::move (sections)),
      padding (std::max (padLength, 0))
{
    for (auto& section : cascade)
    {
        if (section.a0 == 0.0 || section.a0 == 1.0)
            continue;

        const auto scale = 1.0 / section.a0;
        section.b0 *= scale;
        section.b1 *= scale;
        section.b2 *= scale;
        section.a1 *= scale;
        section.a2 *= scale;
        section.a0 = 1.0;
    }
}

void ZeroPhaseFilter::processForward (double* samples, int numSamples) const
{
    if (numSamples <= 0)
        return;

    for (const auto& section : cascade)
    {
        const auto firstSample = samples[0];
        const auto denominator = 1.0 + section.a1 + section.a2;
        const auto dcGain = denominator != 0.0
                          ? (section.b0 + section.b1 + section.b2) / denominator
                          : 0.0;
        const auto steadyOutput = dcGain * firstSample;

        double state2 = section.b2 * firstSample - section.a2 * steadyOutput;
        double state1 = section.b1 * firstSample - section.a1 * steadyOutput + state2;

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            const auto input = samples[sampleIndex];
            const auto output = section.b0 * input + state1;

            state1 = section.b1 * input - section.a1 * output + state2;
            state2 = section.b2 * input - section.a2 * output;

            samples[sampleIndex] = output;
        }
    }
}

void ZeroPhaseFilter::process (float* samples, int numSamples) const
{
    if (cascade.empty() || numSamples <= 0)
        return;

    const auto padLength = std::min (padding, numSamples - 1);

    if (padLength < 0)
        return;

    const auto paddedLength = numSamples + 2 * padLength;
    std::vector<double> padded (static_cast<std::size_t> (paddedLength));

    const auto first = static_cast<double> (samples[0]);
    const auto last = static_cast<double> (samples[numSamples - 1]);

    for (int sampleIndex = 0; sampleIndex < padLength; ++sampleIndex)
    {
        padded[static_cast<std::size_t> (sampleIndex)] =
            2.0 * first - static_cast<double> (samples[padLength - sampleIndex]);
        padded[static_cast<std::size_t> (paddedLength - 1 - sampleIndex)] =
            2.0 * last - static_cast<double> (samples[numSamples - 1 - padLength + sampleIndex]);
    }

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        padded[static_cast<std::size_t> (padLength + sampleIndex)] =
            static_cast<double> (samples[sampleIndex]);

    processForward (padded.data(), paddedLength);
    std::reverse (padded.begin(), padded.end());
    processForward (padded.data(), paddedLength);
    std::reverse (padded.begin(), padded.end());

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        samples[sampleIndex] = static_cast<float> (padded[static_cast<std::size_t> (padLength + sampleIndex)]);
}
}
