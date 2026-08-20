#pragma once

#include "dsp/MelSpectrogram.h"
#include "dsp/ZeroPhaseFilter.h"
#include "common/BinaryMatrix.h"
#include "common/ConversionSettings.h"
#include "model/FeatureRetriever.h"
#include "model/ModelManifest.h"
#include "model/OnnxSession.h"
#include "model/PitchEstimator.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace rvcara
{
/** @brief One loaded voice: three graphs, two matrices, and the manifest tying them together. */
class VoiceModel
{
public:
    ~VoiceModel();

    VoiceModel (const VoiceModel&) = delete;
    VoiceModel& operator= (const VoiceModel&) = delete;

    static std::unique_ptr<VoiceModel> load (const juce::File& directory,
                                             int numThreads,
                                             juce::String& error);

    [[nodiscard]] const ModelManifest& getManifest() const noexcept { return manifest; }

    [[nodiscard]] const juce::String& getName() const noexcept { return manifest.name; }

    [[nodiscard]] const juce::File& getDirectory() const noexcept { return sourceDirectory; }

    [[nodiscard]] const OnnxSession& getContentEncoder() const noexcept { return *contentEncoder; }
    [[nodiscard]] const OnnxSession& getVocoder() const noexcept { return vocoder; }
    [[nodiscard]] const PitchEstimator& getPitchEstimator() const noexcept { return *pitchEstimator; }
    [[nodiscard]] const ZeroPhaseFilter& getInputFilter() const noexcept { return inputFilter; }

    [[nodiscard]] const FeatureRetriever* getRetriever() const noexcept
    {
        return retriever != nullptr && retriever->isReady() ? retriever.get() : nullptr;
    }

    [[nodiscard]] ConversionSettings getDefaultSettings() const noexcept;

private:
    VoiceModel() = default;

    ModelManifest manifest;
    juce::File sourceDirectory;

    /** @brief Borrowed rather than owned: every voice runs the same two graphs. */
    std::shared_ptr<const OnnxSession> contentEncoder;
    std::shared_ptr<const OnnxSession> pitchNetwork;

    OnnxSession vocoder;

    BinaryMatrix melFilterBank;
    BinaryMatrix codebook;

    std::unique_ptr<MelSpectrogram> melSpectrogram;
    std::unique_ptr<PitchEstimator> pitchEstimator;
    std::unique_ptr<FeatureRetriever> retriever;

    ZeroPhaseFilter inputFilter;
};
}
