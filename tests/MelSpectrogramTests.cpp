#include "dsp/MelSpectrogram.h"
#include "common/BinaryMatrix.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace rvcara;

namespace
{
    juce::File getFixtureDirectory()
    {
        return juce::File { RVCARA_TEST_FIXTURE_DIR };
    }

    MelSpectrogram::Configuration getReferenceConfiguration()
    {
        MelSpectrogram::Configuration configuration;
        configuration.fftSize = 1024;
        configuration.windowSize = 1024;
        configuration.hopSizeInSamples = 160;
        configuration.numMelBins = 128;
        configuration.numBins = 513;
        configuration.magnitudeFloor = 1.0e-5f;
        configuration.isCentred = true;
        return configuration;
    }
} // namespace

class MelSpectrogramTest : public testing::Test
{
protected:
    void SetUp() override
    {
        const auto bank = BinaryMatrix::load (getFixtureDirectory().getChildFile ("mel_filter_bank.bin"));
        ASSERT_TRUE (bank.isValid()) << bank.getError();

        const auto numValues = static_cast<std::size_t> (bank.getNumRows())
                             * static_cast<std::size_t> (bank.getNumColumns());

        spectrogram = std::make_unique<MelSpectrogram> (
            configuration, std::vector<float> { bank.getData(), bank.getData() + numValues });
    }

    MelSpectrogram::Configuration configuration { getReferenceConfiguration() };
    std::unique_ptr<MelSpectrogram> spectrogram;
};

TEST_F (MelSpectrogramTest, TheFrameCountFollowsTheCentredFramingRule)
{
    EXPECT_EQ (spectrogram->getNumFrames (16000), 101);
    EXPECT_EQ (spectrogram->getNumFrames (160), 2);
    EXPECT_EQ (spectrogram->getNumFrames (32099), 201);
    EXPECT_EQ (spectrogram->getNumFrames (0), 0);
}

TEST_F (MelSpectrogramTest, ASignalShorterThanTheReflectionIsRefused)
{
    const std::vector<float> tiny (100, 0.0f);
    std::vector<float> destination;

    EXPECT_EQ (spectrogram->process (tiny.data(), static_cast<int> (tiny.size()), destination), 0);
}

TEST_F (MelSpectrogramTest, TheSpectrogramMatchesTheReferenceImplementation)
{
    const auto source = BinaryMatrix::load (getFixtureDirectory().getChildFile ("test_signal.bin"));
    const auto expected = BinaryMatrix::load (getFixtureDirectory().getChildFile ("expected_log_mel.bin"));

    ASSERT_TRUE (source.isValid()) << source.getError();
    ASSERT_TRUE (expected.isValid()) << expected.getError();

    std::vector<float> computed;
    const auto numFrames = spectrogram->process (source.getData(), source.getNumColumns(), computed);

    ASSERT_EQ (numFrames, expected.getNumColumns());
    ASSERT_EQ (computed.size(),
               static_cast<std::size_t> (expected.getNumRows()) * static_cast<std::size_t> (numFrames));

    auto worstDifference = 0.0f;

    for (std::size_t index = 0; index < computed.size(); ++index)
        worstDifference = std::max (worstDifference, std::abs (computed[index] - expected.getData()[index]));

    EXPECT_LT (worstDifference, 1.0e-3f);
}

TEST_F (MelSpectrogramTest, APureToneLandsInTheExpectedMelBin)
{
    constexpr auto sampleRate = 16000.0;
    constexpr auto toneHz = 1000.0;
    constexpr auto numSamples = 8000;

    std::vector<float> tone (numSamples);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        tone[static_cast<std::size_t> (sampleIndex)] =
            static_cast<float> (std::sin (2.0 * std::numbers::pi * toneHz
                                          * static_cast<double> (sampleIndex) / sampleRate));

    std::vector<float> computed;
    const auto numFrames = spectrogram->process (tone.data(), numSamples, computed);
    ASSERT_GT (numFrames, 10);

    const auto frameIndex = numFrames / 2;

    auto peakMelBin = 0;
    auto peakValue = -std::numeric_limits<float>::infinity();

    for (int melIndex = 0; melIndex < configuration.numMelBins; ++melIndex)
    {
        const auto value = computed[static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numFrames)
                                    + static_cast<std::size_t> (frameIndex)];

        if (value > peakValue)
        {
            peakValue = value;
            peakMelBin = melIndex;
        }
    }

    EXPECT_GE (peakMelBin, 41);
    EXPECT_LE (peakMelBin, 46);
}
