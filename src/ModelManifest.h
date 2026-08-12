#pragma once

#include "MelSpectrogram.h"
#include "ZeroPhaseFilter.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace rvcara
{

/** Every constant the pipeline needs, parsed from a model's `manifest.json`.

    The manifest exists so that none of these numbers are compiled into the plugin.
    That is not tidiness for its own sake: RVC models differ in sample rate, feature
    width, speaker count and upsampling factor, and a plugin with 40000 written into
    it can only ever play one kind of voice. Reading them per model means a 32 kHz or
    48 kHz voice, or a multi-speaker one, loads without a release.

    Parsing is strict. A missing required field is an error rather than a default,
    because a silently defaulted hop size produces a voice that sings the right notes
    at the wrong speed, and that is much harder to diagnose than a refusal to load.
*/
struct ModelManifest
{
    /** The manifest schema this build understands.

        A file declaring a higher version is refused: it may require a pipeline step
        this engine does not implement, and rendering it anyway would produce
        confidently wrong audio.
    */
    static constexpr int supportedSchemaVersion = 1;

    juce::String name;

    int modelSampleRate { 40000 };      ///< Rate the vocoder synthesises at
    int featureDim { 768 };             ///< Width of the content-encoder output
    int latentDim { 192 };              ///< Width of the vocoder's latent, and of its noise input
    int numSpeakers { 1 };
    int speakerId { 0 };
    int upsampleFactor { 400 };         ///< Output samples per conditioning frame

    // Graph filenames and the tensor names to bind. Held as std::string because ONNX
    // Runtime takes const char* and these must outlive the call.
    juce::String contentEncoderFile;
    juce::String pitchEstimatorFile;
    juce::String vocoderFile;

    std::string contentEncoderInput;
    std::string contentEncoderOutput;
    std::string pitchEstimatorInput;
    std::string pitchEstimatorOutput;
    std::vector<std::string> vocoderInputs;   ///< In the order the graph declares them
    std::string vocoderOutput;

    // Content encoder
    int contentSampleRate { 16000 };
    int contentFrameRate { 50 };
    int contentMinimumNumSamples { 400 };

    // Pitch front end
    MelSpectrogram::Configuration melConfiguration;
    juce::String melFilterBankFile;
    int pitchSampleRate { 16000 };

    // Pitch decoding
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

    // Retrieval
    juce::String retrievalFile;
    int numRetrievalVectors { 0 };
    int numNeighbours { 8 };
    int graphDegree { 16 };
    int constructionCandidateListSize { 200 };
    int searchCandidateListSize { 64 };

    // Long-input splitting
    double contextPaddingSeconds { 3.0 };
    double splitSearchRadiusSeconds { 10.0 };
    double chunkStrideSeconds { 60.0 };
    double maximumChunkSeconds { 65.0 };

    // Input high-pass
    std::vector<BiquadCoefficients> highPassSections;
    int highPassPadLength { 18 };

    // Initial control values
    float defaultPitchShiftSemitones { 0.0f };
    float defaultRetrievalRatio { 0.75f };
    float defaultConsonantProtection { 0.33f };
    float defaultEnvelopeFollowRatio { 0.0f };
    int defaultLatentNoiseSeed { 1 };

    /** @returns Conditioning frames per second, 100 for every model so far. */
    [[nodiscard]] int getPitchFrameRate() const noexcept
    {
        return melConfiguration.hopSizeInSamples > 0
             ? pitchSampleRate / melConfiguration.hopSizeInSamples
             : 100;
    }

    /** @returns How many times a content frame is repeated to reach the conditioning rate.

        The content encoder emits 50 frames a second and the vocoder wants 100, so this
        is 2 in practice. Derived rather than stored so the two rates cannot disagree.
    */
    [[nodiscard]] int getFeatureUpsampleFactor() const noexcept
    {
        return contentFrameRate > 0 ? getPitchFrameRate() / contentFrameRate : 2;
    }

    /** @returns Whether a retrieval codebook was exported for this model. */
    [[nodiscard]] bool hasRetrieval() const noexcept
    {
        return retrievalFile.isNotEmpty() && numRetrievalVectors > 0;
    }

    /** Parses a manifest.

        @param file   The `manifest.json` to read.
        @param error  Set to a description of the problem on failure.
        @returns      The parsed manifest, or nullopt.
    */
    static std::optional<ModelManifest> parse (const juce::File& file, juce::String& error);
};

} // namespace rvcara
