#include "ZeroPhaseFilter.h"

#include <algorithm>
#include <utility>

namespace rvcara
{

ZeroPhaseFilter::ZeroPhaseFilter (std::vector<BiquadCoefficients> sections, int padLength)
    : cascade (std::move (sections)),
      padding (std::max (padLength, 0))
{
    // Normalise each section by a0 once, here, so the inner loop has no divisions.
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
        // Transposed direct form II: two state variables per section, and the input
        // sample is read once. State is carried across the whole buffer, so each
        // section sees the previous section's complete output.
        //
        // The state is initialised to the steady state the section would settle into for
        // a constant input equal to the first sample, rather than to zero. Starting from
        // zero makes the filter treat the beginning of the signal as a step from silence
        // and ring on it: with poles at a radius of 0.988, that transient takes tens of
        // milliseconds to decay, and it lands squarely in the first frames the pitch
        // estimator reads. This is what SciPy's filtfilt does via lfilter_zi, and it
        // takes the agreement between the two from around 1e-1 to around 3e-8.
        //
        // Solving y = dcGain * x, s2 = b2 x - a2 y, s1 = b1 x - a1 y + s2 for a constant
        // x gives the expressions below. For a high-pass dcGain is zero and they reduce
        // to s1 = (b1 + b2) x, s2 = b2 x, but the general form costs nothing and keeps
        // the class usable for any response.
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

    // The odd extension mirrors the signal about its endpoint values rather than
    // about the endpoints themselves — reflecting 2 * x[0] - x[n] instead of x[n] —
    // so the extension continues the signal's slope instead of creating a corner at
    // the boundary, which the filter would ring on.
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

} // namespace rvcara
