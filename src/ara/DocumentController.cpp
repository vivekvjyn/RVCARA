#include "ara/DocumentController.h"

#include "ara/PlaybackRenderer.h"

#include "model/ConversionEngine.h"

#include <utility>

namespace rvcara
{
class DocumentController::RenderJob final : public juce::ThreadPoolJob
{
public:
    RenderJob (DocumentController& ownerToUse,
               ConversionModification& modificationToUse,
               std::shared_ptr<const VoiceModel> voiceToUse,
               double outputSampleRateToUse,
               std::uint64_t generationToUse)
        : juce::ThreadPoolJob ("convert " + juce::String (modificationToUse.getPersistentID())),
          owner (ownerToUse),
          modification (modificationToUse),
          voice (std::move (voiceToUse)),
          outputSampleRate (outputSampleRateToUse),
          generation (generationToUse)
    {
    }

    void requestAbort() noexcept { shouldAbort.store (true); }

    JobStatus runJob() override
    {
        modification.setState (ConversionModification::State::rendering);
        modification.setProgress (0.0f);

        auto* audioSource = modification.getAudioSource();

        if (audioSource == nullptr || voice == nullptr)
        {
            finish (nullptr, "no audio source or voice");
            return jobHasFinished;
        }

        const auto numSamples = static_cast<int> (audioSource->getSampleCount());
        const auto numChannels = audioSource->getChannelCount();

        if (numSamples <= 0 || numChannels <= 0)
        {
            finish (nullptr, "audio source is empty");
            return jobHasFinished;
        }

        juce::AudioBuffer<float> sourceAudio { numChannels, numSamples };
        {
            juce::ARAAudioSourceReader reader { audioSource };

            if (! reader.read (&sourceAudio, 0, numSamples, 0, true, true))
            {
                finish (nullptr, "could not read the audio source");
                return jobHasFinished;
            }
        }

        if (shouldAbort.load() || shouldExit())
        {
            finish (nullptr, {});
            return jobHasFinished;
        }

        std::vector<float> mono (static_cast<std::size_t> (numSamples), 0.0f);

        for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex)
        {
            const auto* channel = sourceAudio.getReadPointer (channelIndex);

            for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
                mono[static_cast<std::size_t> (sampleIndex)] += channel[sampleIndex];
        }

        if (numChannels > 1)
        {
            const auto scale = 1.0f / static_cast<float> (numChannels);

            for (auto& sample : mono)
                sample *= scale;
        }

        const ConversionEngine converter { *voice };

        ConversionEngine::Request request;
        request.samples = mono.data();
        request.numSamples = numSamples;
        request.sampleRate = audioSource->getSampleRate();
        request.outputSampleRate = outputSampleRate;
        request.settings = modification.getSettings();

        auto outcome = converter.convert (
            request,
            [this] (float fraction) { modification.setProgress (fraction); },
            shouldAbort,
            [this, &request] (ConversionEngine::Result partial)
            {
                publish (std::move (partial), request.settings);
            });

        if (shouldAbort.load() || shouldExit())
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
        result->sampleRate = outputSampleRate;
        result->settings = request.settings;
        result->voiceName = voice->getName();

        finish (std::move (result), {});
        return jobHasFinished;
    }

private:
    void publish (ConversionEngine::Result partial, const ConversionSettings& settings)
    {
        auto conversion = std::make_shared<ConversionResult>();
        conversion->samples = std::move (partial.samples);
        conversion->fundamentalFrequencyHz = std::move (partial.fundamentalFrequencyHz);
        conversion->pitchFrameRate = partial.pitchFrameRate;
        conversion->sampleRate = outputSampleRate;
        conversion->isPartial = true;
        conversion->settings = settings;
        conversion->voiceName = voice->getName();

        juce::MessageManager::callAsync (
            [ownerPointer = &owner, modificationPointer = &modification, jobGeneration = generation,
             conversion = std::move (conversion)]() mutable
            {
                ownerPointer->publishPartialRender (modificationPointer, jobGeneration, std::move (conversion));
            });
    }

    void finish (ConversionPointer conversion, juce::String error)
    {
        juce::MessageManager::callAsync (
            [ownerPointer = &owner, modificationPointer = &modification, jobGeneration = generation,
             conversion = std::move (conversion), error = std::move (error)]() mutable
            {
                ownerPointer->completeRender (modificationPointer, jobGeneration,
                                              std::move (conversion), std::move (error));
            });
    }

    DocumentController& owner;
    ConversionModification& modification;
    std::shared_ptr<const VoiceModel> voice;
    double outputSampleRate;
    std::uint64_t generation;
    std::atomic<bool> shouldAbort { false };
};

DocumentController::DocumentController (const ARA::PlugIn::PlugInEntry* entry,
                                        const ARA::ARADocumentControllerHostInstance* instance)
    : juce::ARADocumentControllerSpecialisation (entry, instance)
{
    loader.addListener (this);

    loader.requestDefaultVoice ([this] { requestRenderForAllStaleModifications(); });
}

DocumentController::~DocumentController()
{
    loader.removeListener (this);
    loader.getWorkerPool().removeAllJobs (true, 10000);
}

std::shared_ptr<const VoiceModel> DocumentController::getVoiceModel() const
{
    return loader.getVoice();
}

juce::String DocumentController::getRequestedVoiceName() const
{
    return loader.getRequestedName();
}

juce::String DocumentController::getLoadError() const
{
    return loader.getError();
}

void DocumentController::voiceStateChanged()
{
    notifyStateChanged();
}

void DocumentController::requestVoice (const juce::String& name)
{
    loader.request (name, [this] { requestRenderForAllStaleModifications(); });
}

std::vector<ConversionModification*> DocumentController::getModifications()
{
    std::vector<ConversionModification*> modifications;

    if (auto* document = getDocument())
        for (auto* audioSource : document->getAudioSources<juce::ARAAudioSource>())
            for (auto* modification : audioSource->getAudioModifications<ConversionModification>())
                modifications.push_back (modification);

    return modifications;
}

void DocumentController::cancelRender (ConversionModification& modification)
{
    RenderJob* job = nullptr;

    {
        const juce::ScopedLock lock { jobLock };
        const auto found = activeJobs.find (&modification);

        if (found != activeJobs.end())
            job = found->second.job;
    }

    if (job == nullptr)
        return;

    job->requestAbort();

    loader.getWorkerPool().waitForJobToFinish (job, 10000);
}

void DocumentController::requestRender (ConversionModification& modification)
{
    auto voice = getVoiceModel();

    if (voice == nullptr)
    {
        modification.setState (ConversionModification::State::idle);
        return;
    }

    if (modification.getSettings().isBypassed)
    {
        modification.setState (ConversionModification::State::idle);
        return;
    }

    auto* audioSource = modification.getAudioSource();

    if (audioSource == nullptr || ! audioSource->isSampleAccessEnabled())
    {
        modification.setState (ConversionModification::State::queued);
        return;
    }

    const auto sessionRate = getSessionSampleRate();

    if (sessionRate <= 0.0)
    {
        modification.setState (ConversionModification::State::queued);
        return;
    }

    cancelRender (modification);

    const auto generation = nextGeneration++;
    auto* job = new RenderJob { *this, modification, std::move (voice), sessionRate, generation };

    {
        const juce::ScopedLock lock { jobLock };
        activeJobs[&modification] = { job, generation };
    }

    modification.setState (ConversionModification::State::queued);
    modification.setProgress (0.0f);
    loader.getWorkerPool().addJob (job, true);

    notifyStateChanged();
}

void DocumentController::requestRenderForAllStaleModifications()
{
    const auto sessionRate = getSessionSampleRate();

    for (auto* modification : getModifications())
        if (! modification->isConversionCurrent (sessionRate))
            requestRender (*modification);
}

void DocumentController::setSessionSampleRate (const PlaybackRenderer* renderer, double sampleRate)
{
    const auto unanimousRate = [&]
    {
        const juce::ScopedLock lock { rateLock };
        rendererSampleRates[renderer] = sampleRate;

        const auto first = rendererSampleRates.begin()->second;

        for (const auto& [ignored, rate] : rendererSampleRates)
            if (! juce::approximatelyEqual (rate, first))
                return 0.0;

        return first;
    }();

    if (unanimousRate <= 0.0 || juce::approximatelyEqual (unanimousRate, getSessionSampleRate()))
        return;

    sessionSampleRate.store (unanimousRate, std::memory_order_release);

    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        return;

    juce::MessageManager::callAsync ([this] { requestRenderForAllStaleModifications(); });
}

void DocumentController::forgetRenderer (const PlaybackRenderer* renderer)
{
    const juce::ScopedLock lock { rateLock };
    rendererSampleRates.erase (renderer);
}

void DocumentController::applySettings (ConversionModification& modification,
                                        const ConversionSettings& settings)
{
    if (! modification.setSettings (settings))
        return;

    requestRender (modification);
    notifyStateChanged();
}

void DocumentController::applyVoice (ConversionModification& modification, const juce::String& name)
{
    if (! modification.setVoiceName (name))
        return;

    requestVoice (name);
    requestRender (modification);
    notifyStateChanged();
}

void DocumentController::completeRender (ConversionModification* modification,
                                         std::uint64_t generation,
                                         ConversionPointer conversion,
                                         juce::String error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (modification == nullptr)
        return;

    {
        const juce::ScopedLock lock { jobLock };
        const auto found = activeJobs.find (modification);

        if (found == activeJobs.end() || found->second.generation != generation)
            return;

        activeJobs.erase (found);
    }

    if (conversion != nullptr)
    {
        loader.withModelLocked ([modification, &conversion]
        {
            modification->setConversion (std::move (conversion));
        });

        modification->setState (ConversionModification::State::ready);
        modification->setProgress (1.0f);
        modification->setError ({});

        modification->notifyContentChanged (juce::ARAContentUpdateScopes::samplesAreAffected(), true);
    }
    else if (error.isNotEmpty())
    {
        modification->setState (ConversionModification::State::failed);
        modification->setError (error);
    }
    else
    {
        modification->setState (ConversionModification::State::idle);
    }

    notifyStateChanged();
}

void DocumentController::publishPartialRender (ConversionModification* modification,
                                                std::uint64_t generation,
                                                ConversionPointer conversion)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (modification == nullptr || conversion == nullptr)
        return;

    {
        const juce::ScopedLock lock { jobLock };
        const auto found = activeJobs.find (modification);

        if (found == activeJobs.end() || found->second.generation != generation)
            return;
    }

    loader.withModelLocked ([modification, &conversion]
    {
        modification->setConversion (std::move (conversion));
    });

    modification->notifyContentChanged (juce::ARAContentUpdateScopes::samplesAreAffected(), true);

    notifyStateChanged();
}

void DocumentController::notifyStateChanged()
{
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        return;

    juce::MessageManager::callAsync ([this] { listeners.call (&Listener::conversionStateChanged); });
}

void DocumentController::addListener (Listener* listener)    { listeners.add (listener); }
void DocumentController::removeListener (Listener* listener) { listeners.remove (listener); }

juce::ARAAudioModification* DocumentController::doCreateAudioModification (
    juce::ARAAudioSource* audioSource,
    ARA::ARAAudioModificationHostRef hostRef,
    const juce::ARAAudioModification* optionalModificationToClone) noexcept
{
    auto* modification = new ConversionModification { audioSource, hostRef, optionalModificationToClone };

    if (const auto* source = dynamic_cast<const ConversionModification*> (optionalModificationToClone))
    {
        modification->setSettings (source->getSettings());
        modification->setVoiceName (source->getVoiceName());
    }
    else
    {
        if (auto voice = getVoiceModel())
        {
            modification->setSettings (voice->getDefaultSettings());
            modification->setVoiceName (voice->getName());
        }
        else
        {
            modification->setVoiceName (getRequestedVoiceName());
        }
    }

    return modification;
}

juce::ARAPlaybackRenderer* DocumentController::doCreatePlaybackRenderer() noexcept
{
    return new PlaybackRenderer { getDocumentController(), *this };
}

void DocumentController::didEnableAudioSourceSamplesAccess (juce::ARAAudioSource* audioSource, bool enable) noexcept
{
    if (! enable || audioSource == nullptr)
        return;

    const auto sessionRate = getSessionSampleRate();

    for (auto* modification : audioSource->getAudioModifications<ConversionModification>())
        if (! modification->isConversionCurrent (sessionRate))
            requestRender (*modification);
}

bool DocumentController::doStoreObjectsToStream (juce::ARAOutputStream& output,
                                                 const juce::ARAStoreObjectsFilter* filter) noexcept
{
    if (filter == nullptr)
        return false;

    const auto& modifications = filter->getAudioModificationsToStore<ConversionModification>();

    output.writeInt (static_cast<int> (modifications.size()));

    for (const auto* modification : modifications)
    {
        output.writeString (juce::String (modification->getPersistentID()));
        modification->writeToArchive (output);
    }

    return true;
}

bool DocumentController::doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                                     const juce::ARARestoreObjectsFilter* filter) noexcept
{
    const auto numModifications = input.readInt();

    for (int index = 0; index < numModifications; ++index)
    {
        const auto persistentID = input.readString();

        auto* modification = filter != nullptr
                           ? filter->getAudioModificationToRestoreStateWithID<ConversionModification> (
                                 persistentID.toRawUTF8())
                           : nullptr;

        if (modification != nullptr)
        {
            if (! modification->readFromArchive (input))
                return false;
        }
        else
        {
            ConversionModification::readAndDiscard (input);
        }

        if (input.failed())
            return false;
    }

    for (const auto* modification : getModifications())
    {
        if (const auto name = modification->getVoiceName(); name.isNotEmpty())
        {
            requestVoice (name);
            break;
        }
    }

    requestRenderForAllStaleModifications();

    return true;
}
}
