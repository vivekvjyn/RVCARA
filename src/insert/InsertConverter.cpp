#include "insert/InsertConverter.h"

#include "model/ConversionEngine.h"

#include <algorithm>

namespace rvcara
{
class InsertConverter::RenderJob final : public juce::ThreadPoolJob
{
public:
    RenderJob (InsertConverter& ownerToUse,
               std::shared_ptr<const VoiceModel> voiceToUse,
               std::vector<float> audioToConvert,
               ConversionSettings settingsToUse,
               double sampleRateToUse,
               int firstSampleToUse,
               int lastSampleToUse)
        : juce::ThreadPoolJob ("insert conversion"),
          owner (ownerToUse),
          voice (std::move (voiceToUse)),
          audio (std::move (audioToConvert)),
          settings (settingsToUse),
          sampleRate (sampleRateToUse),
          firstSample (firstSampleToUse),
          lastSample (lastSampleToUse)
    {
    }

    JobStatus runJob() override
    {
        owner.state.store (State::rendering, std::memory_order_release);
        owner.progress.store (0.0f, std::memory_order_relaxed);

        const ConversionEngine converter { *voice };

        ConversionEngine::Request request;
        request.samples = audio.data();
        request.numSamples = static_cast<int> (audio.size());
        request.sampleRate = sampleRate;
        request.settings = settings;

        auto outcome = converter.convert (
            request,
            [this] (float fraction) { owner.progress.store (fraction, std::memory_order_relaxed); },
            owner.shouldAbortRender);

        if (owner.shouldAbortRender.load() || shouldExit())
        {
            finish (nullptr, {});
            return jobHasFinished;
        }

        if (! outcome.isValid)
        {
            finish (nullptr, outcome.error);
            return jobHasFinished;
        }

        auto result = std::make_shared<ConversionResult>();
        result->samples = std::move (outcome.samples);
        result->fundamentalFrequencyHz = std::move (outcome.fundamentalFrequencyHz);
        result->pitchFrameRate = outcome.pitchFrameRate;
        result->settings = settings;
        result->voiceName = voice->getName();

        finish (std::move (result), {});
        return jobHasFinished;
    }

private:
    void finish (ConversionPointer result, juce::String error)
    {
        juce::MessageManager::callAsync (
            [ownerPointer = &owner, result = std::move (result), error = std::move (error),
             first = firstSample, last = lastSample]() mutable
            {
                ownerPointer->convertedFirstSample.store (first, std::memory_order_relaxed);
                ownerPointer->convertedLastSample.store (last, std::memory_order_relaxed);
                ownerPointer->completeRender (std::move (result), std::move (error));
            });
    }

    InsertConverter& owner;
    std::shared_ptr<const VoiceModel> voice;
    std::vector<float> audio;
    ConversionSettings settings;
    double sampleRate;
    int firstSample;
    int lastSample;
};

InsertConverter::InsertConverter()
{
    startTimer (200);
}

InsertConverter::~InsertConverter()
{
    stopTimer();
    release();
}

void InsertConverter::prepare (double sampleRate, int)
{
    hostSampleRate = sampleRate;
    numCaptureSamples = static_cast<int> (maximumCaptureSeconds * sampleRate);

    capturedAudio.assign (static_cast<std::size_t> (numCaptureSamples), 0.0f);
    convertedAudio.assign (static_cast<std::size_t> (numCaptureSamples), 0.0f);

    reset();
}

void InsertConverter::release()
{
    shouldAbortRender.store (true);

    if (voiceLoader != nullptr && activeJob != nullptr)
    {
        voiceLoader->getWorkerPool().waitForJobToFinish (activeJob, 10000);
        activeJob = nullptr;
    }

    capturedAudio.clear();
    convertedAudio.clear();
    numCaptureSamples = 0;
}

void InsertConverter::reset()
{
    shouldAbortRender.store (true);

    hasCaptureOrigin.store (false, std::memory_order_release);
    firstCapturedSample.store (0, std::memory_order_relaxed);
    lastCapturedSample.store (0, std::memory_order_relaxed);
    hasNewAudio.store (false, std::memory_order_relaxed);
    reachedLimit.store (false, std::memory_order_relaxed);

    {
        const juce::ScopedLock lock { settingsLock };
        conversion.reset();
        errorMessage.clear();
    }

    state.store (State::idle, std::memory_order_release);
    progress.store (0.0f, std::memory_order_relaxed);
}

void InsertConverter::setSettings (const ConversionSettings& newSettings)
{
    const juce::ScopedLock lock { settingsLock };
    settings = newSettings;
}

ConversionSettings InsertConverter::getSettings() const
{
    const juce::ScopedLock lock { settingsLock };
    return settings;
}

ConversionPointer InsertConverter::getConversion() const
{
    const juce::ScopedLock lock { settingsLock };
    return conversion;
}

juce::String InsertConverter::getError() const
{
    const juce::ScopedLock lock { settingsLock };
    return errorMessage;
}

double InsertConverter::getCapturedSeconds() const
{
    if (! hasCaptureOrigin.load (std::memory_order_acquire))
        return 0.0;

    const auto first = firstCapturedSample.load (std::memory_order_relaxed);
    const auto last = lastCapturedSample.load (std::memory_order_relaxed);

    return static_cast<double> (std::max (last - first, 0)) / hostSampleRate;
}

bool InsertConverter::isConversionStale() const
{
    const juce::ScopedLock lock { settingsLock };

    if (conversion == nullptr)
        return getCapturedSeconds() > 0.0;

    return conversion->settings != settings
        || (voiceLoader != nullptr && conversion->voiceName != voiceLoader->getRequestedName());
}

void InsertConverter::process (juce::AudioBuffer<float>& buffer,
                               const juce::AudioPlayHead::PositionInfo& positionInfo)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numCaptureSamples == 0 || numSamples <= 0)
        return;

    receivedAudio.store (true, std::memory_order_relaxed);

    const auto position = positionInfo.getTimeInSamples();

    if (! position.hasValue())
        return;

    sawTransport.store (true, std::memory_order_relaxed);

    const auto songPosition = *position;

    if (voiceLoader == nullptr)
        return;

    const auto lock = voiceLoader->getModelReadLock();

    if (! lock.isLocked())
        return;

    if (positionInfo.getIsPlaying())
    {
        if (! hasCaptureOrigin.load (std::memory_order_acquire))
        {
            captureOrigin.store (songPosition, std::memory_order_relaxed);
            hasCaptureOrigin.store (true, std::memory_order_release);
            firstCapturedSample.store (0, std::memory_order_relaxed);
            lastCapturedSample.store (0, std::memory_order_relaxed);
        }

        const auto origin = captureOrigin.load (std::memory_order_relaxed);
        const auto writeStart = static_cast<int> (songPosition - origin);

        if (writeStart >= 0 && writeStart + numSamples <= numCaptureSamples)
        {
            const auto scale = 1.0f / static_cast<float> (std::max (numChannels, 1));

            for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
            {
                float sum = 0.0f;

                for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex)
                    sum += buffer.getReadPointer (channelIndex)[sampleIndex];

                capturedAudio[static_cast<std::size_t> (writeStart + sampleIndex)] = sum * scale;
            }

            const auto previousFirst = firstCapturedSample.load (std::memory_order_relaxed);
            const auto previousLast = lastCapturedSample.load (std::memory_order_relaxed);

            if (previousLast == 0 || writeStart < previousFirst)
                firstCapturedSample.store (writeStart, std::memory_order_relaxed);

            if (writeStart + numSamples > previousLast)
                lastCapturedSample.store (writeStart + numSamples, std::memory_order_relaxed);

            hasNewAudio.store (true, std::memory_order_relaxed);
            lastCaptureTimeMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

            if (state.load (std::memory_order_acquire) == State::idle)
                state.store (State::capturing, std::memory_order_release);
        }
        else if (writeStart + numSamples > numCaptureSamples)
        {
            reachedLimit.store (true, std::memory_order_relaxed);
        }
    }

    const auto currentSettings = [this]
    {
        const juce::ScopedLock settingsScope { settingsLock };
        return settings;
    }();

    if (currentSettings.isBypassed || state.load (std::memory_order_acquire) != State::ready)
        return;

    if (! hasCaptureOrigin.load (std::memory_order_acquire))
        return;

    const auto origin = captureOrigin.load (std::memory_order_relaxed);
    const auto readStart = static_cast<int> (songPosition - origin);

    const auto convertedFirst = convertedFirstSample.load (std::memory_order_relaxed);
    const auto convertedLast = convertedLastSample.load (std::memory_order_relaxed);

    const auto firstToReplace = std::max (readStart, convertedFirst);
    const auto lastToReplace = std::min (readStart + numSamples, convertedLast);

    if (lastToReplace <= firstToReplace)
        return;

    const auto offsetInBuffer = firstToReplace - readStart;
    const auto numToReplace = lastToReplace - firstToReplace;

    for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex)
        buffer.copyFrom (channelIndex,
                         offsetInBuffer,
                         convertedAudio.data() + firstToReplace,
                         numToReplace);
}

void InsertConverter::timerCallback()
{
    if (! hasNewAudio.load (std::memory_order_relaxed))
        return;

    const auto elapsed = juce::Time::getMillisecondCounter()
                       - lastCaptureTimeMs.load (std::memory_order_relaxed);

    if (elapsed < static_cast<juce::uint32> (quietMillisecondsBeforeRender))
        return;

    hasNewAudio.store (false, std::memory_order_relaxed);
    requestConversion();
}

void InsertConverter::requestConversion()
{
    if (voiceLoader == nullptr || numCaptureSamples == 0)
        return;

    auto voice = voiceLoader->getVoice();

    if (voice == nullptr)
        return;

    const auto first = firstCapturedSample.load (std::memory_order_relaxed);
    const auto last = lastCapturedSample.load (std::memory_order_relaxed);
    const auto numSamples = last - first;

    if (numSamples <= 0)
        return;

    const auto currentSettings = getSettings();

    if (currentSettings.isBypassed)
        return;

    shouldAbortRender.store (true);

    if (activeJob != nullptr)
    {
        voiceLoader->getWorkerPool().waitForJobToFinish (activeJob, 10000);
        activeJob = nullptr;
    }

    shouldAbortRender.store (false);

    std::vector<float> audio;

    voiceLoader->withModelLocked ([this, first, numSamples, &audio]
    {
        audio.assign (capturedAudio.begin() + first, capturedAudio.begin() + first + numSamples);
    });

    auto* job = new RenderJob { *this,        std::move (voice), std::move (audio),
                                currentSettings, hostSampleRate,  first,
                                last };

    activeJob = job;
    state.store (State::queued, std::memory_order_release);
    progress.store (0.0f, std::memory_order_relaxed);

    voiceLoader->getWorkerPool().addJob (job, true);
}

void InsertConverter::completeRender (ConversionPointer result, juce::String error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    activeJob = nullptr;

    if (result == nullptr)
    {
        if (error.isNotEmpty())
        {
            {
                const juce::ScopedLock lock { settingsLock };
                errorMessage = error;
            }

            state.store (State::failed, std::memory_order_release);
        }
        else
        {
            state.store (getCapturedSeconds() > 0.0 ? State::capturing : State::idle,
                         std::memory_order_release);
        }

        return;
    }

    const auto first = convertedFirstSample.load (std::memory_order_relaxed);
    const auto available = std::min (static_cast<int> (result->samples.size()),
                                     numCaptureSamples - first);

    if (available <= 0)
    {
        state.store (State::failed, std::memory_order_release);
        return;
    }

    if (voiceLoader != nullptr)
    {
        voiceLoader->withModelLocked ([this, first, available, &result]
        {
            std::copy (result->samples.begin(),
                       result->samples.begin() + available,
                       convertedAudio.begin() + first);
        });
    }

    convertedLastSample.store (first + available, std::memory_order_relaxed);

    {
        const juce::ScopedLock lock { settingsLock };
        conversion = result;
        errorMessage.clear();
    }

    progress.store (1.0f, std::memory_order_relaxed);
    state.store (State::ready, std::memory_order_release);
}
}
