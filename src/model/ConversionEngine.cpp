#include "model/ConversionEngine.h"

#include "dsp/PitchConversions.h"
#include "dsp/SincResampler.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace rvcara
{
namespace
{
    constexpr juce::uint32 partialIntervalMilliseconds = 900;

    std::vector<float> expandFrames (const std::vector<float>& source,
                                     int numSourceFrames,
                                     int featureDim,
                                     int repeats)
    {
        std::vector<float> expanded (source.size() * static_cast<std::size_t> (repeats));

        for (int frameIndex = 0; frameIndex < numSourceFrames; ++frameIndex)
        {
            const auto* sourceFrame = source.data()
                                    + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (featureDim);

            for (int repeat = 0; repeat < repeats; ++repeat)
            {
                const auto destinationFrame = static_cast<std::size_t> (frameIndex)
                                                  * static_cast<std::size_t> (repeats)
                                              + static_cast<std::size_t> (repeat);
                std::copy (sourceFrame,
                           sourceFrame + featureDim,
                           expanded.data() + destinationFrame * static_cast<std::size_t> (featureDim));
            }
        }

        return expanded;
    }

    void blendUnvoicedTowardSource (std::vector<float>& conditioning,
                                    const std::vector<float>& sourceFeatures,
                                    const float* fundamentalFrequencyHz,
                                    int numFrames,
                                    int featureDim,
                                    float protection)
    {
        for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        {
            if (fundamentalFrequencyHz[frameIndex] >= 1.0f || protection >= 1.0f)
                continue;

            const auto offset = static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (featureDim);
            auto* target = conditioning.data() + offset;
            const auto* source = sourceFeatures.data() + offset;

            for (int dimension = 0; dimension < featureDim; ++dimension)
                target[dimension] = protection * target[dimension] + (1.0f - protection) * source[dimension];
        }
    }

    std::vector<float> reflectPad (const float* samples, int numSamples, int padLength)
    {
        std::vector<float> padded (static_cast<std::size_t> (numSamples + 2 * padLength));

        std::copy (samples, samples + numSamples, padded.begin() + padLength);

        const auto period = std::max (2 * (numSamples - 1), 1);

        for (int padIndex = 0; padIndex < padLength; ++padIndex)
        {
            const auto reflect = [period, numSamples] (int index)
            {
                auto wrapped = index % period;
                if (wrapped < 0)
                    wrapped += period;
                return wrapped < numSamples ? wrapped : period - wrapped;
            };

            padded[static_cast<std::size_t> (padIndex)] = samples[reflect (padIndex - padLength)];
            padded[static_cast<std::size_t> (padLength + numSamples + padIndex)] =
                samples[reflect (numSamples + padIndex)];
        }

        return padded;
    }
}

ConversionEngine::ConversionEngine (const VoiceModel& model)
    : voiceModel (model),
      manifest (model.getManifest())
{
}

int ConversionEngine::getMinimumNumSamples (double sampleRate) const noexcept
{
    const auto minimumAtAnalysisRate = std::max (manifest.melConfiguration.fftSize,
                                                 manifest.contentMinimumNumSamples);

    return static_cast<int> (std::ceil (static_cast<double> (minimumAtAnalysisRate)
                                        * sampleRate / static_cast<double> (manifest.contentSampleRate)));
}

std::vector<ConversionEngine::Chunk> ConversionEngine::planChunks (const float* filtered,
                                                                  int numFilteredSamples,
                                                                  int numPaddedSamples,
                                                                  int numFrames) const
{
    const auto sampleRate = manifest.contentSampleRate;
    const auto hopSize = manifest.melConfiguration.hopSizeInSamples;
    const auto contextPadding = static_cast<int> (manifest.contextPaddingSeconds * sampleRate);

    const auto maximumChunk = static_cast<int> (manifest.maximumChunkSeconds * sampleRate);

    if (numPaddedSamples <= maximumChunk)
        return { Chunk { 0, numPaddedSamples, 0, numFrames } };

    const auto stride = static_cast<int> (manifest.chunkStrideSeconds * sampleRate);
    const auto searchRadius = static_cast<int> (manifest.splitSearchRadiusSeconds * sampleRate);

    std::vector<double> movingEnergy (static_cast<std::size_t> (numFilteredSamples), 0.0);
    {
        double window = 0.0;

        for (int sampleIndex = 0; sampleIndex < numFilteredSamples; ++sampleIndex)
        {
            window += std::abs (static_cast<double> (filtered[sampleIndex]));

            if (sampleIndex >= hopSize)
                window -= std::abs (static_cast<double> (filtered[sampleIndex - hopSize]));

            movingEnergy[static_cast<std::size_t> (sampleIndex)] = window;
        }
    }

    std::vector<int> boundaries;

    for (auto nominal = stride; nominal < numFilteredSamples; nominal += stride)
    {
        const auto searchStart = std::max (nominal - searchRadius, 0);
        const auto searchEnd = std::min (nominal + searchRadius, numFilteredSamples);

        if (searchEnd <= searchStart)
            continue;

        const auto quietest = std::min_element (movingEnergy.begin() + searchStart,
                                                movingEnergy.begin() + searchEnd);
        const auto position = static_cast<int> (std::distance (movingEnergy.begin(), quietest));

        boundaries.push_back (position / hopSize * hopSize);
    }

    std::vector<Chunk> chunks;
    auto chunkStart = 0;

    for (const auto boundary : boundaries)
    {
        const auto chunkEnd = std::min (boundary + 2 * contextPadding + hopSize, numPaddedSamples);

        if (chunkEnd <= chunkStart)
            continue;

        chunks.push_back (Chunk { chunkStart,
                                  chunkEnd,
                                  chunkStart / hopSize,
                                  std::min ((chunkEnd - hopSize) / hopSize, numFrames) });
        chunkStart = boundary;
    }

    chunks.push_back (Chunk { chunkStart, numPaddedSamples, chunkStart / hopSize, numFrames });

    return chunks;
}

std::vector<float> ConversionEngine::encodeContent (const float* samples,
                                                   int numSamples,
                                                   juce::String& error) const
{
    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (manifest.contentEncoderInput.c_str(), samples, { 1, numSamples })
    };

    const std::vector<const char*> outputs { manifest.contentEncoderOutput.c_str() };

    auto returned = voiceModel.getContentEncoder().run (inputs, outputs);

    if (returned.empty())
    {
        error = "content encoder failed: " + voiceModel.getContentEncoder().getError();
        return {};
    }

    const auto shape = returned.front().GetTensorTypeAndShapeInfo().GetShape();

    if (shape.size() != 3)
    {
        error = "content encoder returned an unexpected rank";
        return {};
    }

    const auto numElements = static_cast<std::size_t> (shape[1] * shape[2]);
    const auto* data = returned.front().GetTensorData<float>();

    return { data, data + numElements };
}

std::vector<float> ConversionEngine::synthesise (const std::vector<float>& contentFeatures,
                                                const std::int64_t* coarsePitch,
                                                const float* fundamentalFrequencyHz,
                                                int numFrames,
                                                const ConversionSettings& settings,
                                                juce::String& error) const
{
    std::vector<float> latentNoise (static_cast<std::size_t> (manifest.latentDim)
                                   * static_cast<std::size_t> (numFrames));
    {
        std::mt19937 generator { static_cast<std::uint32_t> (settings.latentNoiseSeed) };
        std::normal_distribution<float> distribution { 0.0f, 1.0f };

        for (auto& value : latentNoise)
            value = distribution (generator);
    }

    const std::int64_t numFramesValue = numFrames;
    const std::int64_t speakerIdValue = manifest.speakerId;

    const auto& names = manifest.synthesizerInputs;

    const std::vector<OnnxSession::TensorView> inputs {
        OnnxSession::TensorView::floats (names[0].c_str(), contentFeatures.data(),
                                        { 1, numFrames, manifest.featureDim }),
        OnnxSession::TensorView::integers (names[1].c_str(), &numFramesValue, { 1 }),
        OnnxSession::TensorView::integers (names[2].c_str(), coarsePitch, { 1, numFrames }),
        OnnxSession::TensorView::floats (names[3].c_str(), fundamentalFrequencyHz, { 1, numFrames }),
        OnnxSession::TensorView::integers (names[4].c_str(), &speakerIdValue, { 1 }),
        OnnxSession::TensorView::floats (names[5].c_str(), latentNoise.data(),
                                        { 1, manifest.latentDim, numFrames }),
    };

    const std::vector<const char*> outputs { manifest.synthesizerOutput.c_str() };

    auto returned = voiceModel.getSynthesizer().run (inputs, outputs);

    if (returned.empty())
    {
        error = "the synthesiser failed: " + voiceModel.getSynthesizer().getError();
        return {};
    }

    const auto shape = returned.front().GetTensorTypeAndShapeInfo().GetShape();
    const auto numOutputSamples = static_cast<std::size_t> (shape.back());
    const auto* data = returned.front().GetTensorData<float>();

    return { data, data + numOutputSamples };
}

void ConversionEngine::followSourceEnvelope (std::vector<float>& converted,
                                             const float* source,
                                             int numSourceSamples,
                                             double sourceSampleRate,
                                             float ratio) const
{
    if (converted.empty() || ratio <= 0.0f)
        return;

    const auto measure = [] (const float* samples, int numSamples, int windowSize, int numPoints)
    {
        std::vector<double> envelope (static_cast<std::size_t> (numPoints), 0.0);

        for (int pointIndex = 0; pointIndex < numPoints; ++pointIndex)
        {
            const auto start = pointIndex * windowSize;
            const auto end = std::min (start + windowSize, numSamples);

            double sumOfSquares = 0.0;
            for (int sampleIndex = start; sampleIndex < end; ++sampleIndex)
                sumOfSquares += static_cast<double> (samples[sampleIndex]) * static_cast<double> (samples[sampleIndex]);

            const auto count = std::max (end - start, 1);
            envelope[static_cast<std::size_t> (pointIndex)] = std::sqrt (sumOfSquares / static_cast<double> (count));
        }

        return envelope;
    };

    const auto sourceWindow = std::max (static_cast<int> (sourceSampleRate / 2.0), 1);
    const auto convertedWindow = std::max (manifest.modelSampleRate / 2, 1);

    const auto numSourcePoints = std::max (1 + numSourceSamples / sourceWindow, 2);
    const auto numConvertedPoints = std::max (1 + static_cast<int> (converted.size()) / convertedWindow, 2);

    const auto sourceEnvelope = measure (source, numSourceSamples, sourceWindow, numSourcePoints);
    const auto convertedEnvelope = measure (converted.data(), static_cast<int> (converted.size()),
                                            convertedWindow, numConvertedPoints);

    const auto interpolate = [] (const std::vector<double>& envelope, double position)
    {
        const auto scaled = position * static_cast<double> (envelope.size() - 1);
        const auto lowerIndex = static_cast<std::size_t> (scaled);

        if (lowerIndex + 1 >= envelope.size())
            return envelope.back();

        const auto fraction = scaled - static_cast<double> (lowerIndex);
        return envelope[lowerIndex] * (1.0 - fraction) + envelope[lowerIndex + 1] * fraction;
    };

    const auto numSamples = static_cast<double> (converted.size() - 1);

    for (std::size_t sampleIndex = 0; sampleIndex < converted.size(); ++sampleIndex)
    {
        const auto position = numSamples > 0.0 ? static_cast<double> (sampleIndex) / numSamples : 0.0;

        const auto wanted = interpolate (sourceEnvelope, position);
        const auto have = std::max (interpolate (convertedEnvelope, position), 1.0e-6);

        const auto scale = std::pow (wanted, static_cast<double> (ratio))
                         * std::pow (have, -static_cast<double> (ratio));

        converted[sampleIndex] = static_cast<float> (static_cast<double> (converted[sampleIndex]) * scale);
    }
}

ConversionEngine::Result ConversionEngine::convert (const Request& request,
                                                    const ProgressCallback& onProgress,
                                                    const std::atomic<bool>& shouldAbort,
                                                    const PartialCallback& onPartial) const
{
    Result result;
    result.pitchFrameRate = static_cast<double> (manifest.getPitchFrameRate());

    if (request.samples == nullptr || request.numSamples <= 0)
    {
        result.error = "nothing to convert";
        return result;
    }

    if (request.numSamples < getMinimumNumSamples (request.sampleRate))
    {
        result.error = "region is shorter than the analysis window";
        return result;
    }

    const auto report = [&onProgress] (float fraction)
    {
        if (onProgress)
            onProgress (std::clamp (fraction, 0.0f, 1.0f));
    };

    const auto analysisSampleRate = static_cast<double> (manifest.contentSampleRate);
    const auto hopSize = manifest.melConfiguration.hopSizeInSamples;

    const SincResampler toAnalysisRate { request.sampleRate, analysisSampleRate };
    auto analysis = toAnalysisRate.process (request.samples, request.numSamples);

    if (analysis.empty())
    {
        result.error = "resampling to the analysis rate produced nothing";
        return result;
    }

    report (0.05f);

    if (shouldAbort.load())
        return result;

    voiceModel.getInputFilter().process (analysis.data(), static_cast<int> (analysis.size()));

    const auto contextPadding = static_cast<int> (manifest.contextPaddingSeconds * analysisSampleRate);
    const auto padded = reflectPad (analysis.data(), static_cast<int> (analysis.size()), contextPadding);
    const auto numPaddedSamples = static_cast<int> (padded.size());

    auto melody = voiceModel.getPitchEstimator().estimate (padded.data(),
                                                           numPaddedSamples,
                                                           result.error);

    if (melody.getNumFrames() == 0)
    {
        if (result.error.isEmpty())
            result.error = "pitch estimation produced no frames";

        return result;
    }

    report (0.15f);

    if (shouldAbort.load())
        return result;

    const auto numFrames = std::min (numPaddedSamples / hopSize, melody.getNumFrames());
    melody.fundamentalFrequencyHz.resize (static_cast<std::size_t> (numFrames));
    melody.coarsePitchBins.resize (static_cast<std::size_t> (numFrames));

    const auto pitchPaddingFrames = contextPadding / hopSize;

    const auto sungFundamentalFrequencyHz = melody.fundamentalFrequencyHz;

    if (request.pitchEdit != nullptr)
        applyPitchEdit (melody.fundamentalFrequencyHz,
                        *request.pitchEdit,
                        result.pitchFrameRate,
                        pitchPaddingFrames);

    if (request.settings.pitchShiftSemitones != 0.0f)
    {
        const auto ratio = static_cast<float> (semitonesToRatio (request.settings.pitchShiftSemitones));

        for (auto& frequencyHz : melody.fundamentalFrequencyHz)
            frequencyHz *= ratio;
    }

    if (request.pitchEdit != nullptr || request.settings.pitchShiftSemitones != 0.0f)
        voiceModel.getPitchEstimator().requantise (melody);

    const auto chunks = planChunks (analysis.data(),
                                    static_cast<int> (analysis.size()),
                                    numPaddedSamples,
                                    numFrames);

    const auto featureUpsampleFactor = manifest.getFeatureUpsampleFactor();
    const auto featureDim = manifest.featureDim;
    const auto outputContextPadding =
        static_cast<int> (manifest.contextPaddingSeconds * manifest.modelSampleRate);
    const auto* retriever = voiceModel.getRetriever();

    const auto outputSampleRate = request.outputSampleRate > 0.0 ? request.outputSampleRate
                                                                 : request.sampleRate;

    const SincResampler toOutputRate { static_cast<double> (manifest.modelSampleRate), outputSampleRate };

    const auto numRegionFrames = std::max (0, numFrames - 2 * pitchPaddingFrames);

    const auto assemble = [&] (const std::vector<float>& renderedSoFar, bool isComplete)
    {
        Result assembled;
        assembled.pitchFrameRate = result.pitchFrameRate;

        const auto renderedSeconds = static_cast<double> (renderedSoFar.size())
                                   / static_cast<double> (manifest.modelSampleRate);

        std::vector<float> shaped;
        const auto* toResample = &renderedSoFar;

        if (request.settings.envelopeFollowRatio > 0.0f)
        {
            const auto numAnalysisSamples = isComplete
                ? static_cast<int> (analysis.size())
                : std::min (static_cast<int> (renderedSeconds * analysisSampleRate),
                            static_cast<int> (analysis.size()));

            if (numAnalysisSamples > 0)
            {
                shaped = renderedSoFar;
                followSourceEnvelope (shaped,
                                      analysis.data(),
                                      numAnalysisSamples,
                                      analysisSampleRate,
                                      request.settings.envelopeFollowRatio);
                toResample = &shaped;
            }
        }

        assembled.samples = toOutputRate.process (toResample->data(),
                                                  static_cast<int> (toResample->size()));

        const auto numOutputSamples = static_cast<std::size_t> (std::llround (
            static_cast<double> (request.numSamples) * outputSampleRate / request.sampleRate));

        assembled.samples.resize (isComplete ? numOutputSamples
                                             : std::min (assembled.samples.size(), numOutputSamples),
                                  0.0f);

        const auto numFramesSoFar = isComplete
            ? numRegionFrames
            : std::min (numRegionFrames, static_cast<int> (renderedSeconds * assembled.pitchFrameRate));

        if (numFramesSoFar > 0)
        {
            assembled.fundamentalFrequencyHz.assign (
                melody.fundamentalFrequencyHz.begin() + pitchPaddingFrames,
                melody.fundamentalFrequencyHz.begin() + pitchPaddingFrames + numFramesSoFar);

            assembled.sourceFundamentalFrequencyHz.assign (
                sungFundamentalFrequencyHz.begin() + pitchPaddingFrames,
                sungFundamentalFrequencyHz.begin() + pitchPaddingFrames + numFramesSoFar);
        }

        assembled.isValid = true;
        return assembled;
    };

    auto lastPartialAt = juce::Time::getMillisecondCounter();

    std::vector<float> rendered;
    rendered.reserve (static_cast<std::size_t> (numFrames) * static_cast<std::size_t> (manifest.upsampleFactor));

    for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex)
    {
        if (shouldAbort.load())
            return result;

        const auto& chunk = chunks[chunkIndex];
        const auto chunkNumSamples = chunk.endSample - chunk.startSample;

        auto features = encodeContent (padded.data() + chunk.startSample, chunkNumSamples, result.error);

        if (features.empty())
            return result;

        const auto numContentFrames = static_cast<int> (features.size() / static_cast<std::size_t> (featureDim));

        std::vector<float> originalFeatures;

        if (request.settings.consonantProtection < 0.5f)
            originalFeatures = features;

        if (retriever != nullptr && request.settings.retrievalRatio > 0.0f)
            retriever->blend (features.data(), numContentFrames, request.settings.retrievalRatio);

        auto conditioning = expandFrames (features, numContentFrames, featureDim, featureUpsampleFactor);

        const auto chunkStartFrame = chunk.startFrame;
        const auto availableFrames = std::min (chunk.endFrame, numFrames) - chunkStartFrame;

        auto numChunkFrames = std::min ({ chunkNumSamples / hopSize,
                                          numContentFrames * featureUpsampleFactor,
                                          availableFrames });

        if (numChunkFrames <= 0)
            continue;

        conditioning.resize (static_cast<std::size_t> (numChunkFrames) * static_cast<std::size_t> (featureDim));

        const auto* chunkPitchHz = melody.fundamentalFrequencyHz.data() + chunkStartFrame;
        const auto* chunkCoarsePitch = melody.coarsePitchBins.data() + chunkStartFrame;

        if (! originalFeatures.empty())
            blendUnvoicedTowardSource (conditioning,
                                       expandFrames (originalFeatures, numContentFrames, featureDim, featureUpsampleFactor),
                                       chunkPitchHz,
                                       numChunkFrames,
                                       featureDim,
                                       request.settings.consonantProtection);

        auto chunkAudio = synthesise (conditioning,
                                      chunkCoarsePitch,
                                      chunkPitchHz,
                                      numChunkFrames,
                                      request.settings,
                                      result.error);

        if (chunkAudio.empty())
            return result;

        const auto trimStart = std::min (outputContextPadding, static_cast<int> (chunkAudio.size()));
        const auto trimEnd = std::min (outputContextPadding,
                                       static_cast<int> (chunkAudio.size()) - trimStart);

        rendered.insert (rendered.end(),
                         chunkAudio.begin() + trimStart,
                         chunkAudio.end() - trimEnd);

        report (0.15f + 0.75f * static_cast<float> (chunkIndex + 1) / static_cast<float> (chunks.size()));

        if (onPartial && chunkIndex + 1 < chunks.size())
        {
            const auto now = juce::Time::getMillisecondCounter();

            if (now - lastPartialAt >= partialIntervalMilliseconds)
            {
                lastPartialAt = now;
                onPartial (assemble (rendered, false));
            }
        }
    }

    if (rendered.empty())
    {
        result.error = "conversion produced no audio";
        return result;
    }

    if (shouldAbort.load())
        return result;

    auto assembled = assemble (rendered, true);
    result.samples = std::move (assembled.samples);
    result.fundamentalFrequencyHz = std::move (assembled.fundamentalFrequencyHz);
    result.sourceFundamentalFrequencyHz = std::move (assembled.sourceFundamentalFrequencyHz);

    report (1.0f);

    result.isValid = true;
    return result;
}
}
