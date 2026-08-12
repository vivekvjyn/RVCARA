#pragma once

#include "MelSpectrogram.h"
#include "ZeroPhaseFilter.h"
#include "BinaryMatrix.h"
#include "ConversionSettings.h"
#include "FeatureRetriever.h"
#include "ModelManifest.h"
#include "OnnxSession.h"
#include "PitchEstimator.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace rvcara
{

/** One loaded voice: three graphs, two matrices, and the constants tying them together.

    Loading is all-or-nothing and reports why it failed. A partially loaded voice is
    never returned, because the alternative is discovering a missing codebook halfway
    through a render, on a background thread, with no good way to tell the user.

    Loading is slow — hundreds of megabytes of graph, plus building the retrieval index
    the first time — and must never happen on the audio thread or in a message-thread
    callback. The document controller loads on a background thread and publishes the
    result.

    Instances are immutable once loaded, and const member functions on them are safe to
    call from several threads. The ONNX sessions are the exception: ONNX Runtime does
    not guarantee concurrent `Run` calls on one session at these options, so a voice is
    used by one conversion worker at a time. The library hands out shared pointers, and
    the document controller serialises renders per voice.
*/
class VoiceModel
{
public:
    ~VoiceModel();

    VoiceModel (const VoiceModel&) = delete;
    VoiceModel& operator= (const VoiceModel&) = delete;

    /** Loads every asset in a model directory.

        @param directory   Directory holding `manifest.json` and the assets beside it.
        @param numThreads  Intra-operator threads for inference; zero lets ONNX Runtime
                           choose one per core.
        @param error       Set to a description of the problem on failure.
        @returns           The loaded voice, or nullptr.
    */
    static std::unique_ptr<VoiceModel> load (const juce::File& directory,
                                             int numThreads,
                                             juce::String& error);

    /** @returns The parsed manifest, the authority on every pipeline constant. */
    [[nodiscard]] const ModelManifest& getManifest() const noexcept { return manifest; }

    /** @returns The voice's name, for display. */
    [[nodiscard]] const juce::String& getName() const noexcept { return manifest.name; }

    /** @returns The directory the assets were loaded from. */
    [[nodiscard]] const juce::File& getDirectory() const noexcept { return sourceDirectory; }

    [[nodiscard]] const OnnxSession& getContentEncoder() const noexcept { return contentEncoder; }
    [[nodiscard]] const OnnxSession& getVocoder() const noexcept { return vocoder; }
    [[nodiscard]] const PitchEstimator& getPitchEstimator() const noexcept { return *pitchEstimator; }
    [[nodiscard]] const ZeroPhaseFilter& getInputFilter() const noexcept { return inputFilter; }

    /** @returns The retriever, or nullptr when the model has no codebook. */
    [[nodiscard]] const FeatureRetriever* getRetriever() const noexcept
    {
        return retriever != nullptr && retriever->isReady() ? retriever.get() : nullptr;
    }

    /** @returns Settings initialised from the manifest's defaults. */
    [[nodiscard]] ConversionSettings getDefaultSettings() const noexcept;

private:
    VoiceModel() = default;

    ModelManifest manifest;
    juce::File sourceDirectory;

    OnnxSession contentEncoder;
    OnnxSession pitchNetwork;
    OnnxSession vocoder;

    BinaryMatrix melFilterBank;
    BinaryMatrix codebook;

    std::unique_ptr<MelSpectrogram> melSpectrogram;
    std::unique_ptr<PitchEstimator> pitchEstimator;
    std::unique_ptr<FeatureRetriever> retriever;

    ZeroPhaseFilter inputFilter;
};

} // namespace rvcara
