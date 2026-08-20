#pragma once

#include "common/ConversionSettings.h"
#include "common/PitchEdit.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace rvcara
{
/** @brief A finished conversion: the audio, the pitch tracks it followed, and what produced it. */
struct ConversionResult
{
    std::vector<float> samples;
    std::vector<float> fundamentalFrequencyHz;

    /** @brief The melody as it was sung, which the editor draws the notes against. */
    std::vector<float> sourceFundamentalFrequencyHz;

    double pitchFrameRate { 100.0 };

    double sampleRate { 0.0 };

    /** @brief True while this covers only the start of the region, with more still rendering. */
    bool isPartial { false };

    ConversionSettings settings;
    PitchEdit pitchEdit;
    juce::String voiceName;

    [[nodiscard]] int getNumSamples() const noexcept { return static_cast<int> (samples.size()); }
};

using ConversionPointer = std::shared_ptr<const ConversionResult>;
}
