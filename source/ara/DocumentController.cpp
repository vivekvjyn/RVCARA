#include "DocumentController.h"

#include "PlaybackRenderer.h"

#include <engine/ConversionEngine.h>

#include <utility>

namespace rvcara::ara
{

/** One conversion, running on the render pool.

    Reads the source audio through an ARA reader, converts it, and hands the result back
    on the message thread. Holds no lock while converting — a conversion takes minutes and
    blocking the audio thread for that long is not an option — which is why the result is
    published separately, under the processing lock, once it is complete and immutable.
*/
class DocumentController::RenderJob final : public juce::ThreadPoolJob
{
public:
    RenderJob (DocumentController& ownerToUse,
               ConversionModification& modificationToUse,
               std::shared_ptr<const engine::VoiceModel> voiceToUse)
        : juce::ThreadPoolJob ("convert " + juce::String (modificationToUse.getPersistentID())),
          owner (ownerToUse),
          modification (modificationToUse),
          voice (std::move (voiceToUse))
    {
    }

    /** Asks the conversion to stop at its next checkpoint. */
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

        // Reading through an ARAAudioSourceReader is how a plugin gets at the whole of a
        // region's audio: the host owns the file and streams it on request. This is the
        // capability ARA exists to provide, and the reason a non-causal model can be used
        // at all.
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

        // The model is mono. Mixing to mono rather than converting one channel keeps a
        // stereo-recorded vocal from losing whichever channel happened to be quieter.
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

        const engine::ConversionEngine converter { *voice };

        engine::ConversionEngine::Request request;
        request.samples = mono.data();
        request.numSamples = numSamples;
        request.sampleRate = audioSource->getSampleRate();
        request.settings = modification.getSettings();

        auto outcome = converter.convert (
            request,
            [this] (float fraction) { modification.setProgress (fraction); },
            shouldAbort);

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
        result->settings = request.settings;
        result->voiceName = voice->getName();

        finish (std::move (result), {});
        return jobHasFinished;
    }

private:
    /** Hands the outcome back to the message thread.

        Publishing has to happen where the ARA model may be touched and where the host may
        be notified, which is the message thread; doing it from here would race both.
    */
    void finish (ConversionPointer conversion, juce::String error)
    {
        juce::MessageManager::callAsync (
            [ownerPointer = &owner, modificationPointer = &modification,
             conversion = std::move (conversion), error = std::move (error)]() mutable
            {
                ownerPointer->completeRender (modificationPointer, std::move (conversion), std::move (error));
            });
    }

    DocumentController& owner;
    ConversionModification& modification;
    std::shared_ptr<const engine::VoiceModel> voice;
    std::atomic<bool> shouldAbort { false };
};

// ==================================================================================

DocumentController::~DocumentController()
{
    renderPool.removeAllJobs (true, 5000);
}

std::shared_ptr<const engine::VoiceModel> DocumentController::getVoiceModel() const
{
    const juce::ScopedLock lock { voiceLock };
    return voiceModel;
}

juce::String DocumentController::getRequestedVoiceName() const
{
    const juce::ScopedLock lock { voiceLock };
    return requestedVoiceName;
}

juce::String DocumentController::getLoadError() const
{
    const juce::ScopedLock lock { voiceLock };
    return loadError;
}

void DocumentController::requestVoice (const juce::String& name)
{
    {
        const juce::ScopedLock lock { voiceLock };

        if (requestedVoiceName == name && (voiceModel != nullptr || name.isEmpty()))
            return;

        requestedVoiceName = name;
        loadError.clear();
    }

    if (name.isEmpty())
    {
        {
            const juce::ScopedWriteLock lock { processingLock };
            const juce::ScopedLock voiceScope { voiceLock };
            voiceModel.reset();
        }

        notifyStateChanged();
        return;
    }

    library.rescan();
    const auto* entry = library.findByName (name);

    if (entry == nullptr)
    {
        const juce::ScopedLock lock { voiceLock };
        loadError = "no voice named " + name + " is installed";
        notifyStateChanged();
        return;
    }

    isLoading.store (true, std::memory_order_release);
    notifyStateChanged();

    // Loading is hundreds of megabytes of graph plus, on first use, building the retrieval
    // graph, so it cannot happen on the message thread.
    const auto directory = entry->directory;

    renderPool.addJob (
        [this, directory, name]
        {
            juce::String error;
            auto loaded = engine::VoiceModel::load (directory, 0, error);

            juce::MessageManager::callAsync (
                [this, name, error, shared = std::shared_ptr<const engine::VoiceModel> { std::move (loaded) }]
                {
                    {
                        const juce::ScopedWriteLock processingScope { processingLock };
                        const juce::ScopedLock voiceScope { voiceLock };

                        // A newer request may have superseded this load while it ran.
                        if (requestedVoiceName == name)
                        {
                            voiceModel = shared;
                            loadError = shared != nullptr ? juce::String() : error;
                        }
                    }

                    isLoading.store (false, std::memory_order_release);
                    requestRenderForAllStaleModifications();
                    notifyStateChanged();
                });
        });
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
            job = found->second;
    }

    if (job == nullptr)
        return;

    job->requestAbort();

    // Wait rather than detach: the job holds a reference to the modification, which the
    // host may be about to destroy.
    renderPool.waitForJobToFinish (job, 10000);
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
        // The host has not granted read access yet. didEnableAudioSourceSamplesAccess
        // will bring us back here when it does.
        modification.setState (ConversionModification::State::queued);
        return;
    }

    cancelRender (modification);

    auto* job = new RenderJob { *this, modification, std::move (voice) };

    {
        const juce::ScopedLock lock { jobLock };
        activeJobs[&modification] = job;
    }

    modification.setState (ConversionModification::State::queued);
    modification.setProgress (0.0f);
    renderPool.addJob (job, true);

    notifyStateChanged();
}

void DocumentController::requestRenderForAllStaleModifications()
{
    for (auto* modification : getModifications())
        if (! modification->isConversionCurrent())
            requestRender (*modification);
}

void DocumentController::applySettings (ConversionModification& modification,
                                        const engine::ConversionSettings& settings)
{
    if (! modification.setSettings (settings))
        return;

    // The cached render is kept rather than cleared. A stale voice is better than silence
    // for the minutes a re-render can take, and the editor shows the staleness.
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
                                         ConversionPointer conversion,
                                         juce::String error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    {
        const juce::ScopedLock lock { jobLock };
        activeJobs.erase (modification);
    }

    if (modification == nullptr)
        return;

    if (conversion != nullptr)
    {
        {
            const juce::ScopedWriteLock lock { processingLock };
            modification->setConversion (std::move (conversion));
        }

        modification->setState (ConversionModification::State::ready);
        modification->setProgress (1.0f);
        modification->setError ({});

        // Tell the host the modification's audio changed, so it re-reads what it has
        // cached for the affected regions. Without this a host that caches aggressively
        // would keep playing the previous render.
        modification->notifyContentChanged (juce::ARAContentUpdateScopes::samplesAreAffected(), true);
    }
    else if (error.isNotEmpty())
    {
        modification->setState (ConversionModification::State::failed);
        modification->setError (error);
    }
    else
    {
        // Aborted, not failed.
        modification->setState (ConversionModification::State::idle);
    }

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

// ==================================================================================
// ARA overrides

juce::ARAAudioModification* DocumentController::doCreateAudioModification (
    juce::ARAAudioSource* audioSource,
    ARA::ARAAudioModificationHostRef hostRef,
    const juce::ARAAudioModification* optionalModificationToClone) noexcept
{
    auto* modification = new ConversionModification { audioSource, hostRef, optionalModificationToClone };

    if (const auto* source = dynamic_cast<const ConversionModification*> (optionalModificationToClone))
    {
        // Cloning happens when the user duplicates a region. Carrying the settings and
        // voice across means the copy sounds like the original immediately, and it will
        // re-render rather than share the cached audio.
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

    // Access has just been granted, so anything that was waiting on it can now run.
    for (auto* modification : audioSource->getAudioModifications<ConversionModification>())
        if (! modification->isConversionCurrent())
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
        // Keyed by the modification's own persistent ID, which is what the restore filter
        // maps back onto objects in the document being restored into.
        output.writeString (juce::String (modification->getPersistentID()));
        modification->writeToArchive (output);
    }

    // juce::ARAOutputStream reports write failures through the host's callbacks rather
    // than a flag, so there is nothing further to check here.
    return true;
}

bool DocumentController::doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                                     const juce::ARARestoreObjectsFilter* filter) noexcept
{
    const auto numModifications = input.readInt();

    for (int index = 0; index < numModifications; ++index)
    {
        const auto persistentID = input.readString();

        // The filter maps the archive's IDs onto the objects in the document being
        // restored into, which may differ from the one that was archived.
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
            // Skip a record for an object this document does not have, rather than
            // abandoning the whole archive.
            ConversionModification::readAndDiscard (input);
        }

        if (input.failed())
            return false;
    }

    // Rendered audio is not archived, so a restored session converts on open.
    requestRenderForAllStaleModifications();

    return true;
}

} // namespace rvcara::ara
