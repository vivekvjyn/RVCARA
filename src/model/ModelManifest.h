#pragma once

#include "dsp/MelSpectrogram.h"
#include "dsp/ZeroPhaseFilter.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace rvcara
{
/** @brief Every constant the pipeline needs, parsed from a voice's manifest.json. */
struct ModelManifest
{
    static constexpr int supportedSchemaVersion = 1;

    juce::String name;

    int modelSampleRate { 40000 };
    int featureDim { 768 };
    int latentDim { 192 };
    int numSpeakers { 1 };
    int speakerId { 0 };
    int upsampleFactor { 400 };

    juce::String contentEncoderFile;
    juce::String pitchEstimatorFile;
    juce::String vocoderFile;

    std::string contentEncoderInput;
    std::string contentEncoderOutput;
    std::string pitchEstimatorInput;
    std::string pitchEstimatorOutput;
    std::vector<std::string> vocoderInputs;
    std::string vocoderOutput;

    int contentSampleRate { 16000 };
    int contentFrameRate { 50 };
    int contentMinimumNumSamples { 400 };

    MelSpectrogram::Configuration melConfiguration;
    juce::String melFilterBankFile;
    int pitchSampleRate { 16000 };

    int numPitchBins { 360 };
    double centsOrigin { 1997.3794084376191 };
    double centsPerBin { 20.0 };
    double centsReferenceHz { 10.0 };
    int localAverageRadius { 4 };
    float salienceThreshold { 0.03f };
    int frameCountMultiple { 32 };
    double pitchMinimumHz { 50.0 };
    double pitchMaximumHz { 1100.0 };
    int numCoarsePitchBins { 255 };

    juce::String retrievalFile;
    int numRetrievalVectors { 0 };
    int numNeighbours { 8 };
    int graphDegree { 16 };
    int constructionCandidateListSize { 200 };
    int searchCandidateListSize { 64 };

    double contextPaddingSeconds { 3.0 };
    double splitSearchRadiusSeconds { 10.0 };
    double chunkStrideSeconds { 60.0 };
    double maximumChunkSeconds { 65.0 };

    std::vector<BiquadCoefficients> highPassSections;
    int highPassPadLength { 18 };

    float defaultPitchShiftSemitones { 0.0f };
    float defaultRetrievalRatio { 0.75f };
    float defaultConsonantProtection { 0.33f };
    float defaultEnvelopeFollowRatio { 0.0f };
    int defaultLatentNoiseSeed { 1 };

    [[nodiscard]] int getPitchFrameRate() const noexcept
    {
        return melConfiguration.hopSizeInSamples > 0
             ? pitchSampleRate / melConfiguration.hopSizeInSamples
             : 100;
    }

    [[nodiscard]] int getFeatureUpsampleFactor() const noexcept
    {
        return contentFrameRate > 0 ? getPitchFrameRate() / contentFrameRate : 2;
    }

    [[nodiscard]] bool hasRetrieval() const noexcept
    {
        return retrievalFile.isNotEmpty() && numRetrievalVectors > 0;
    }

    static std::optional<ModelManifest> parse (const juce::File& file, juce::String& error);
};
}
