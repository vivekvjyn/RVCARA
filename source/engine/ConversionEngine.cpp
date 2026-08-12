#include "ConversionEngine.h"

#include "../dsp/SincResampler.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace rvcara::engine
{

namespace
{
    /** Reflects a signal at both ends without repeating the edge sample.

        Matches NumPy's and torch's `reflect` mode. Reflected context, rather than
        silence, is what lets the networks see plausible audio either side of a region:
        padding with zeros would make the first and last few hundred milliseconds sound
        like the start and end of an utterance even mid-phrase.
    */
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
} // namespace

ConversionEngine::ConversionEngine (const VoiceModel& model)
    : voiceModel (model),
      manifest (model.getManifest())
{
}

int ConversionEngine::getMinimumNumSamples (double sampleRate) const noexcept
{
    // The binding constraint is the spectrogram's reflection, which needs half a
    // transform of signal, measured at the analysis rate and converted back.
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

    // Moving sum of absolute amplitude over one hop, as a cheap energy measure. A seam
    // at its minimum is a seam in the quietest available moment.
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

        // Snap to a frame boundary so the pitch track slices cleanly.
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
    // Seeded generation rather than the graph's own draw, so a cached render can be
    // reproduced exactly and the seed can be exposed as a variation control.
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

    // Bound positionally against the manifest's declared input order, which is the order
    // the exporter wrote them in. VoiceModel::load has already checked the count.
    const auto& names = manifest.vocoderInputs;

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

    const std::vector<const char*> outputs { manifest.vocoderOutput.c_str() };

    auto returned = voiceModel.getVocoder().run (inputs, outputs);

    if (returned.empty())
    {
        error = "vocoder failed: " + voiceModel.getVocoder().getError();
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

    // Half-second windows, matching the reference. Long enough to track phrasing rather
    // than individual glottal pulses, which is what "envelope" should mean here.
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

        // At ratio 1 this is the source envelope imposed outright; at 0 it is a no-op.
        const auto scale = std::pow (wanted, static_cast<double> (ratio))
                         * std::pow (have, -static_cast<double> (ratio));

        converted[sampleIndex] = static_cast<float> (static_cast<double> (converted[sampleIndex]) * scale);
    }
}

ConversionEngine::Result ConversionEngine::convert (const Request& request,
                                                    const ProgressCallback& onProgress,
                                                    const std::atomic<bool>& shouldAbort) const
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

    // 1. To the analysis rate. The resampler band-limits as it goes, so nothing above
    //    8 kHz folds down into what the content encoder sees.
    const dsp::SincResampler toAnalysisRate { request.sampleRate, analysisSampleRate };
    auto analysis = toAnalysisRate.process (request.samples, request.numSamples);

    if (analysis.empty())
    {
        result.error = "resampling to the analysis rate produced nothing";
        return result;
    }

    report (0.05f);

    if (shouldAbort.load())
        return result;

    // 2. Zero-phase high-pass, to keep rumble out of the pitch estimate.
    voiceModel.getInputFilter().process (analysis.data(), static_cast<int> (analysis.size()));

    // 3. Reflected context either side, so the networks never see an artificial edge.
    const auto contextPadding = static_cast<int> (manifest.contextPaddingSeconds * analysisSampleRate);
    const auto padded = reflectPad (analysis.data(), static_cast<int> (analysis.size()), contextPadding);
    const auto numPaddedSamples = static_cast<int> (padded.size());

    // 4. Pitch over the whole padded signal, once. Estimating per chunk would let the
    //    gap-filling interpolation restart at every seam.
    auto melody = voiceModel.getPitchEstimator().estimate (padded.data(),
                                                           numPaddedSamples,
                                                           request.settings.pitchShiftSemitones,
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

    const auto chunks = planChunks (analysis.data(),
                                    static_cast<int> (analysis.size()),
                                    numPaddedSamples,
                                    numFrames);

    const auto featureUpsampleFactor = manifest.getFeatureUpsampleFactor();
    const auto featureDim = manifest.featureDim;
    const auto outputContextPadding =
        static_cast<int> (manifest.contextPaddingSeconds * manifest.modelSampleRate);
    const auto* retriever = voiceModel.getRetriever();

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

        // Retrieval happens before the frame-rate doubling: half as many queries for
        // the same result, since both copies of a duplicated frame would retrieve
        // identically.
        std::vector<float> originalFeatures;

        if (request.settings.consonantProtection < 0.5f)
            originalFeatures = features;

        if (retriever != nullptr && request.settings.retrievalRatio > 0.0f)
            retriever->blend (features.data(), numContentFrames, request.settings.retrievalRatio);

        // Nearest-neighbour repetition to the conditioning rate. Interpolating would
        // blur phoneme boundaries across a 20 ms window.
        const auto expand = [featureDim, featureUpsampleFactor] (const std::vector<float>& source, int numSourceFrames)
        {
            std::vector<float> expanded (source.size() * static_cast<std::size_t> (featureUpsampleFactor));

            for (int frameIndex = 0; frameIndex < numSourceFrames; ++frameIndex)
            {
                const auto* sourceFrame = source.data() + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (featureDim);

                for (int repeat = 0; repeat < featureUpsampleFactor; ++repeat)
                {
                    auto* destination = expanded.data()
                                      + (static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (featureUpsampleFactor)
                                         + static_cast<std::size_t> (repeat))
                                            * static_cast<std::size_t> (featureDim);
                    std::copy (sourceFrame, sourceFrame + featureDim, destination);
                }
            }

            return expanded;
        };

        auto conditioning = expand (features, numContentFrames);

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

        // Consonant protection. Note this is inert whenever the pitch track has been
        // gap-filled, because nothing is then below 1 Hz — see
        // PitchEstimator::fillUnvoicedGaps. Reproduced faithfully rather than
        // "corrected", so that a render here matches a render from the reference.
        if (! originalFeatures.empty())
        {
            const auto expandedOriginal = expand (originalFeatures, numContentFrames);
            const auto protection = request.settings.consonantProtection;

            for (int frameIndex = 0; frameIndex < numChunkFrames; ++frameIndex)
            {
                const auto pitchHz = chunkPitchHz[frameIndex];
                const auto weight = pitchHz < 1.0f ? protection : 1.0f;

                if (weight >= 1.0f)
                    continue;

                auto* target = conditioning.data() + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (featureDim);
                const auto* original = expandedOriginal.data() + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (featureDim);

                for (int dimension = 0; dimension < featureDim; ++dimension)
                    target[dimension] = weight * target[dimension] + (1.0f - weight) * original[dimension];
            }
        }

        auto chunkAudio = synthesise (conditioning,
                                      chunkCoarsePitch,
                                      chunkPitchHz,
                                      numChunkFrames,
                                      request.settings,
                                      result.error);

        if (chunkAudio.empty())
            return result;

        // Trim the context this chunk was given, so consecutive chunks abut exactly.
        const auto trimStart = std::min (outputContextPadding, static_cast<int> (chunkAudio.size()));
        const auto trimEnd = std::min (outputContextPadding,
                                       static_cast<int> (chunkAudio.size()) - trimStart);

        rendered.insert (rendered.end(),
                         chunkAudio.begin() + trimStart,
                         chunkAudio.end() - trimEnd);

        report (0.15f + 0.75f * static_cast<float> (chunkIndex + 1) / static_cast<float> (chunks.size()));
    }

    if (rendered.empty())
    {
        result.error = "conversion produced no audio";
        return result;
    }

    if (request.settings.envelopeFollowRatio > 0.0f)
        followSourceEnvelope (rendered,
                              analysis.data(),
                              static_cast<int> (analysis.size()),
                              analysisSampleRate,
                              request.settings.envelopeFollowRatio);

    if (shouldAbort.load())
        return result;

    // 5. Back to the source's rate, then trimmed or zero-filled to exactly the source
    //    length. The lengths agree to within a sample or two from rate rounding, and the
    //    renderer's mapping relies on them being identical.
    const dsp::SincResampler toSourceRate { static_cast<double> (manifest.modelSampleRate), request.sampleRate };
    result.samples = toSourceRate.process (rendered.data(), static_cast<int> (rendered.size()));
    result.samples.resize (static_cast<std::size_t> (request.numSamples), 0.0f);

    // The pitch track is reported over the region itself, with the context trimmed, so
    // it lines up with the audio the editor draws.
    const auto pitchPaddingFrames = contextPadding / hopSize;
    const auto numRegionFrames = std::max (0, numFrames - 2 * pitchPaddingFrames);

    if (numRegionFrames > 0)
    {
        result.fundamentalFrequencyHz.assign (
            melody.fundamentalFrequencyHz.begin() + pitchPaddingFrames,
            melody.fundamentalFrequencyHz.begin() + pitchPaddingFrames + numRegionFrames);
    }

    report (1.0f);

    result.isValid = true;
    return result;
}

} // namespace rvcara::engine
