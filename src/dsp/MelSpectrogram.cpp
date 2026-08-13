#include "dsp/MelSpectrogram.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace rvcara
{
namespace
{
    int reflectIndex (int index, int numSamples) noexcept
    {
        if (numSamples <= 1)
            return 0;

        const auto period = 2 * (numSamples - 1);

        auto wrapped = index % period;
        if (wrapped < 0)
            wrapped += period;

        return wrapped < numSamples ? wrapped : period - wrapped;
    }
}

MelSpectrogram::MelSpectrogram (const Configuration& configuration, std::vector<float> filterBank)
    : config (configuration),
      melFilterBank (std::move (filterBank))
{
    const auto order = static_cast<int> (std::llround (std::log2 (static_cast<double> (config.fftSize))));
    transform = std::make_unique<juce::dsp::FFT> (order);

    analysisWindow.resize (static_cast<std::size_t> (config.windowSize));

    for (int sampleIndex = 0; sampleIndex < config.windowSize; ++sampleIndex)
    {
        const auto phase = 2.0 * std::numbers::pi * static_cast<double> (sampleIndex)
                         / static_cast<double> (config.windowSize);
        analysisWindow[static_cast<std::size_t> (sampleIndex)] =
            static_cast<float> (0.5 - 0.5 * std::cos (phase));
    }
}

int MelSpectrogram::getNumFrames (int numSamples) const noexcept
{
    if (numSamples <= 0)
        return 0;

    if (! config.isCentred)
        return std::max (0, (numSamples - config.fftSize) / config.hopSizeInSamples + 1);

    return 1 + numSamples / config.hopSizeInSamples;
}

int MelSpectrogram::getMinimumNumSamples() const noexcept
{
    return config.isCentred ? config.fftSize / 2 + 1 : config.fftSize;
}

int MelSpectrogram::process (const float* samples, int numSamples, std::vector<float>& destination) const
{
    if (samples == nullptr || numSamples < getMinimumNumSamples())
        return 0;

    const auto numFrames = getNumFrames (numSamples);
    if (numFrames <= 0)
        return 0;

    destination.assign (static_cast<std::size_t> (config.numMelBins) * static_cast<std::size_t> (numFrames), 0.0f);

    const auto centreOffset = config.isCentred ? config.fftSize / 2 : 0;
    const auto logFloor = std::log (config.magnitudeFloor);

    std::vector<float> scratch (static_cast<std::size_t> (2 * config.fftSize));
    std::vector<float> magnitude (static_cast<std::size_t> (config.numBins));

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        std::fill (scratch.begin(), scratch.end(), 0.0f);

        const auto frameStart = frameIndex * config.hopSizeInSamples - centreOffset;

        for (int windowIndex = 0; windowIndex < config.windowSize; ++windowIndex)
        {
            const auto sourceIndex = reflectIndex (frameStart + windowIndex, numSamples);
            scratch[static_cast<std::size_t> (windowIndex)] =
                samples[sourceIndex] * analysisWindow[static_cast<std::size_t> (windowIndex)];
        }

        transform->performRealOnlyForwardTransform (scratch.data(), true);

        for (int binIndex = 0; binIndex < config.numBins; ++binIndex)
        {
            const auto real = scratch[static_cast<std::size_t> (2 * binIndex)];
            const auto imaginary = scratch[static_cast<std::size_t> (2 * binIndex + 1)];
            magnitude[static_cast<std::size_t> (binIndex)] = std::hypot (real, imaginary);
        }

        for (int melIndex = 0; melIndex < config.numMelBins; ++melIndex)
        {
            const auto* row = melFilterBank.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (config.numBins);

            float sum = 0.0f;
            for (int binIndex = 0; binIndex < config.numBins; ++binIndex)
                sum += row[binIndex] * magnitude[static_cast<std::size_t> (binIndex)];

            const auto destinationIndex = static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numFrames)
                                        + static_cast<std::size_t> (frameIndex);

            destination[destinationIndex] = sum > config.magnitudeFloor ? std::log (sum) : logFloor;
        }
    }

    return numFrames;
}
}
