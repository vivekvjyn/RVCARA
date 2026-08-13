#pragma once

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace rvcara
{
/** @brief Log-mel spectrogram front end for the pitch estimator. */
class MelSpectrogram
{
public:
    /** @brief Transform, framing and mel parameters, taken from the manifest. */
    struct Configuration
    {
        int fftSize { 1024 };
        int windowSize { 1024 };
        int hopSizeInSamples { 160 };
        int numMelBins { 128 };
        int numBins { 513 };
        float magnitudeFloor { 1.0e-5f };
        bool isCentred { true };
    };

    MelSpectrogram (const Configuration& configuration, std::vector<float> filterBank);

    [[nodiscard]] int getNumFrames (int numSamples) const noexcept;

    int process (const float* samples, int numSamples, std::vector<float>& destination) const;

    [[nodiscard]] int getMinimumNumSamples() const noexcept;

    [[nodiscard]] const Configuration& getConfiguration() const noexcept { return config; }

private:
    Configuration config;
    std::vector<float> melFilterBank;
    std::vector<float> analysisWindow;

    std::unique_ptr<juce::dsp::FFT> transform;
};
}
