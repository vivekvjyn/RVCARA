#include "model/PitchEstimator.h"

#include <algorithm>
#include <cmath>

namespace rvcara
{
PitchEstimator::PitchEstimator (const ModelManifest& manifestToUse,
                                const MelSpectrogram& spectrogram,
                                const OnnxSession& network)
    : manifest (manifestToUse),
      melSpectrogram (spectrogram),
      pitchNetwork (network),
      quantiser (manifestToUse.pitchMinimumHz, manifestToUse.pitchMaximumHz, manifestToUse.numCoarsePitchBins)
{
    centsPerClass.resize (static_cast<std::size_t> (manifest.numPitchBins));

    for (int binIndex = 0; binIndex < manifest.numPitchBins; ++binIndex)
        centsPerClass[static_cast<std::size_t> (binIndex)] =
            manifest.centsOrigin + manifest.centsPerBin * static_cast<double> (binIndex);
}

double PitchEstimator::decodeFrame (const float* salienceFrame) const
{
    const auto numBins = manifest.numPitchBins;
    const auto radius = manifest.localAverageRadius;

    auto peakIndex = 0;
    auto peakValue = salienceFrame[0];

    for (int binIndex = 1; binIndex < numBins; ++binIndex)
    {
        if (salienceFrame[binIndex] > peakValue)
        {
            peakValue = salienceFrame[binIndex];
            peakIndex = binIndex;
        }
    }

    if (peakValue <= manifest.salienceThreshold)
        return 0.0;

    const auto firstBin = std::max (peakIndex - radius, 0);
    const auto lastBin = std::min (peakIndex + radius, numBins - 1);

    double weightedSum = 0.0;
    double weightSum = 0.0;

    for (int binIndex = firstBin; binIndex <= lastBin; ++binIndex)
    {
        const auto weight = static_cast<double> (salienceFrame[binIndex]);
        weightedSum += weight * centsPerClass[static_cast<std::size_t> (binIndex)];
        weightSum += weight;
    }

    if (weightSum <= 0.0)
        return 0.0;

    const auto cents = weightedSum / weightSum;

    if (cents == 0.0)
        return 0.0;

    return centsToHz (cents, manifest.centsReferenceHz);
}

void PitchEstimator::fillUnvoicedGaps (std::vector<float>& frequencies)
{
    const auto numFrames = static_cast<int> (frequencies.size());

    auto firstVoiced = -1;
    auto lastVoiced = -1;

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        if (frequencies[static_cast<std::size_t> (frameIndex)] > 0.0f)
        {
            if (firstVoiced < 0)
                firstVoiced = frameIndex;

            lastVoiced = frameIndex;
        }
    }

    if (firstVoiced < 0)
        return;

    for (int frameIndex = 0; frameIndex < firstVoiced; ++frameIndex)
        frequencies[static_cast<std::size_t> (frameIndex)] = frequencies[static_cast<std::size_t> (firstVoiced)];

    for (int frameIndex = lastVoiced + 1; frameIndex < numFrames; ++frameIndex)
        frequencies[static_cast<std::size_t> (frameIndex)] = frequencies[static_cast<std::size_t> (lastVoiced)];

    auto gapStart = firstVoiced;

    while (gapStart < lastVoiced)
    {
        if (frequencies[static_cast<std::size_t> (gapStart + 1)] > 0.0f)
        {
            ++gapStart;
            continue;
        }

        auto gapEnd = gapStart + 1;
        while (gapEnd <= lastVoiced && frequencies[static_cast<std::size_t> (gapEnd)] <= 0.0f)
            ++gapEnd;

        const auto startValue = frequencies[static_cast<std::size_t> (gapStart)];
        const auto endValue = frequencies[static_cast<std::size_t> (gapEnd)];
        const auto span = static_cast<float> (gapEnd - gapStart);

        for (int frameIndex = gapStart + 1; frameIndex < gapEnd; ++frameIndex)
        {
            const auto position = static_cast<float> (frameIndex - gapStart) / span;
            frequencies[static_cast<std::size_t> (frameIndex)] =
                startValue + position * (endValue - startValue);
        }

        gapStart = gapEnd;
    }
}

void PitchEstimator::requantise (Result& melody) const
{
    melody.coarsePitchBins.resize (melody.fundamentalFrequencyHz.size());

    for (std::size_t frameIndex = 0; frameIndex < melody.fundamentalFrequencyHz.size(); ++frameIndex)
        melody.coarsePitchBins[frameIndex] =
            quantiser.toBin (static_cast<double> (melody.fundamentalFrequencyHz[frameIndex]));
}

PitchEstimator::Result PitchEstimator::estimate (const float* samples,
                                                 int numSamples,
                                                 juce::String& error) const
{
    Result result;

    std::vector<float> logMel;
    const auto numFrames = melSpectrogram.process (samples, numSamples, logMel);

    if (numFrames <= 0)
    {
        error = "input is too short to estimate pitch";
        return result;
    }

    const auto multiple = std::max (manifest.frameCountMultiple, 1);
    const auto paddedNumFrames = ((numFrames + multiple - 1) / multiple) * multiple;
    const auto numMelBins = manifest.melConfiguration.numMelBins;

    std::vector<float> paddedLogMel;

    if (paddedNumFrames != numFrames)
    {
        paddedLogMel.assign (static_cast<std::size_t> (numMelBins) * static_cast<std::size_t> (paddedNumFrames), 0.0f);

        for (int melIndex = 0; melIndex < numMelBins; ++melIndex)
        {
            const auto* source = logMel.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numFrames);
            auto* destination = paddedLogMel.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (paddedNumFrames);
            std::copy (source, source + numFrames, destination);
        }
    }
    else
    {
        paddedLogMel = std::move (logMel);
    }

    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (manifest.pitchEstimatorInput.c_str(),
                                        paddedLogMel.data(),
                                        { 1, numMelBins, paddedNumFrames })
    };

    const std::vector<const char*> outputs { manifest.pitchEstimatorOutput.c_str() };

    auto returned = pitchNetwork.run (inputs, outputs);

    if (returned.empty())
    {
        error = "pitch estimator failed: " + pitchNetwork.getError();
        return result;
    }

    const auto* salience = returned.front().GetTensorData<float>();
    const auto numBins = manifest.numPitchBins;

    result.fundamentalFrequencyHz.resize (static_cast<std::size_t> (numFrames));

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto* frame = salience + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (numBins);
        result.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] =
            static_cast<float> (decodeFrame (frame));
    }

    fillUnvoicedGaps (result.fundamentalFrequencyHz);

    requantise (result);

    return result;
}
}
