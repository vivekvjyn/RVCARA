#pragma once

#include "dsp/MelSpectrogram.h"
#include "dsp/ZeroPhaseFilter.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace rvcara
{
/** @brief What a content encoder installed beside the voices says about itself.

    The encoder is HuBERT, carrying ContentVec's weights, and is the same graph whichever voice
    is loaded, so it is installed once and describes itself rather than being described by every
    voice that borrows it.
*/
struct ContentEncoderManifest
{
    static constexpr int supportedSchemaVersion = 1;

    /** @brief The directory it is installed in, named for the architecture. */
    static constexpr const char* directoryName = "HuBERT";

    /** @brief What the same directory was called when it was named for the weights. */
    static constexpr const char* legacyDirectoryName = "ContentVec";

    juce::String graphFile;
    std::string inputName;
    std::string outputName;

    int sampleRate { 16000 };
    int frameRate { 50 };
    int featureDim { 768 };
    int minimumNumSamples { 400 };

    static std::optional<ContentEncoderManifest> parse (const juce::File& file, juce::String& error);
};

/** @brief What a pitch estimator installed beside the voices says about itself.

    The estimator is RMVPE, which is not trained per singer, so it carries its own front end
    and its own decoding constants: the mel filter bank it wants is its business, not a voice's.
*/
struct PitchEstimatorManifest
{
    static constexpr int supportedSchemaVersion = 1;

    static constexpr const char* directoryName = "RMVPE";

    static constexpr const char* legacyDirectoryName = "RMVPE";

    juce::String graphFile;
    juce::String filterBankFile;
    std::string inputName;
    std::string outputName;

    int sampleRate { 16000 };
    MelSpectrogram::Configuration melConfiguration;

    int numPitchBins { 360 };
    double centsOrigin { 1997.3794084376191 };
    double centsPerBin { 20.0 };
    double centsReferenceHz { 10.0 };
    int localAverageRadius { 4 };
    float salienceThreshold { 0.03f };
    int frameCountMultiple { 32 };
    double minimumFrequencyHz { 50.0 };
    double maximumFrequencyHz { 1100.0 };

    static std::optional<PitchEstimatorManifest> parse (const juce::File& file, juce::String& error);
};

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

    /** @brief Takes a shared content encoder's word for what it is over the voice's copy of it.
        @param encoder  What the installed encoder says about itself.
        @param error    Set when the encoder does not fit this voice.
        @return True when the encoder was adopted.
    */
    bool adopt (const ContentEncoderManifest& encoder, juce::String& error);

    /** @brief Takes a shared pitch estimator's word for what it is over the voice's copy of it.
        @param estimator  What the installed estimator says about itself.
        @param error      Set when the estimator does not fit this voice.
        @return True when the estimator was adopted.
    */
    bool adopt (const PitchEstimatorManifest& estimator, juce::String& error);

    /** @brief Whether the rates the graphs work at fit together.
        @param error  Set when the conditioning rate is not a whole multiple of the feature rate.
        @return True when the pipeline's frame rates line up.
    */
    [[nodiscard]] bool checkFrameRates (juce::String& error) const;

    static std::optional<ModelManifest> parse (const juce::File& file, juce::String& error);
};
}
