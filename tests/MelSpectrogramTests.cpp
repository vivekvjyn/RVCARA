#include <dsp/MelSpectrogram.h>
#include <engine/BinaryMatrix.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace rvcara;
using Catch::Approx;

namespace
{
    juce::File getFixtureDirectory()
    {
        return juce::File { RVCARA_TEST_FIXTURE_DIR };
    }

    dsp::MelSpectrogram::Configuration getReferenceConfiguration()
    {
        // The values the reference RMVPE front end uses, mirrored from
        // tools/rvcara_export/pitch_estimator.py.
        dsp::MelSpectrogram::Configuration configuration;
        configuration.fftSize = 1024;
        configuration.windowSize = 1024;
        configuration.hopSizeInSamples = 160;
        configuration.numMelBins = 128;
        configuration.numBins = 513;
        configuration.magnitudeFloor = 1.0e-5f;
        configuration.isCentred = true;
        return configuration;
    }

    std::vector<float> loadFilterBank()
    {
        const auto matrix = engine::BinaryMatrix::load (getFixtureDirectory().getChildFile ("mel_filter_bank.bin"));
        REQUIRE (matrix.isValid());

        return { matrix.getData(),
                 matrix.getData() + static_cast<std::size_t> (matrix.getNumRows()) * static_cast<std::size_t> (matrix.getNumColumns()) };
    }
} // namespace

TEST_CASE ("the frame count follows the centred-framing rule", "[mel]")
{
    const dsp::MelSpectrogram spectrogram { getReferenceConfiguration(), loadFilterBank() };

    // Centred framing gives one frame per hop plus one, independent of the transform
    // size. Getting this wrong by one frame misaligns the pitch track against the
    // content features for the whole region.
    CHECK (spectrogram.getNumFrames (16000) == 101);
    CHECK (spectrogram.getNumFrames (160) == 2);
    CHECK (spectrogram.getNumFrames (32099) == 201);
    CHECK (spectrogram.getNumFrames (0) == 0);
}

TEST_CASE ("a signal shorter than the reflection is refused rather than guessed at", "[mel]")
{
    const dsp::MelSpectrogram spectrogram { getReferenceConfiguration(), loadFilterBank() };

    std::vector<float> tiny (100, 0.0f);
    std::vector<float> destination;

    CHECK (spectrogram.process (tiny.data(), static_cast<int> (tiny.size()), destination) == 0);
}

TEST_CASE ("the spectrogram matches the reference implementation", "[mel]")
{
    // The fixture was produced by tools/rvcara_export/fixtures.py using NumPy, SciPy and
    // librosa's filter bank — the same code path the exported model was validated
    // against. This is the test that would catch a symmetric Hann window, power instead
    // of magnitude, a missing reflection, or a transposed filter bank; all four produce
    // a plausible spectrogram of the wrong thing.
    const auto fixtures = getFixtureDirectory();

    const auto signal = engine::BinaryMatrix::load (fixtures.getChildFile ("test_signal.bin"));
    const auto expected = engine::BinaryMatrix::load (fixtures.getChildFile ("expected_log_mel.bin"));

    REQUIRE (signal.isValid());
    REQUIRE (expected.isValid());

    const dsp::MelSpectrogram spectrogram { getReferenceConfiguration(), loadFilterBank() };

    std::vector<float> computed;
    const auto numFrames = spectrogram.process (signal.getData(), signal.getNumColumns(), computed);

    REQUIRE (numFrames == expected.getNumColumns());
    REQUIRE (computed.size() == static_cast<std::size_t> (expected.getNumRows()) * static_cast<std::size_t> (numFrames));

    // Tolerance covers only the difference between a float32 FFT and NumPy's float64
    // one; the values themselves are natural logarithms of order 1 to 10.
    auto worstDifference = 0.0f;

    for (std::size_t index = 0; index < computed.size(); ++index)
        worstDifference = std::max (worstDifference, std::abs (computed[index] - expected.getData()[index]));

    CHECK (worstDifference < 1.0e-3f);
}

TEST_CASE ("a pure tone lands in the expected mel bin", "[mel]")
{
    // A sanity check that survives a filter bank regenerated with different parameters,
    // which the golden test above would simply fail without saying why.
    const auto configuration = getReferenceConfiguration();
    const dsp::MelSpectrogram spectrogram { configuration, loadFilterBank() };

    constexpr auto sampleRate = 16000.0;
    constexpr auto toneHz = 1000.0;
    constexpr auto numSamples = 8000;

    std::vector<float> tone (numSamples);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        tone[static_cast<std::size_t> (sampleIndex)] =
            static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * toneHz
                                          * static_cast<double> (sampleIndex) / sampleRate));

    std::vector<float> computed;
    const auto numFrames = spectrogram.process (tone.data(), numSamples, computed);
    REQUIRE (numFrames > 10);

    // Read a frame from the middle, where the window is fully inside the tone.
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

    // On the HTK mel scale, 2595 * log10(1 + f / 700), the span 30 Hz to 8 kHz places
    // 1 kHz at 34.1% of the range, so bin 43 or 44 of 128. A linear-frequency bank would
    // put it near bin 16 and a transposed one nowhere in particular.
    CHECK (peakMelBin >= 41);
    CHECK (peakMelBin <= 46);
}
