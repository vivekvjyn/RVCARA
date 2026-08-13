#pragma once

#include "ConversionModification.h"
#include "VoiceLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace rvcara
{

/** The plugin's ARA model: owns the voice, the renders, and the lock that guards them.

    One document controller exists per ARA document — in practice, per session that has
    RVCARA on a track — and every plugin instance in that session shares it. That is what
    makes ARA the right fit for this plugin rather than an inconvenience: the voice model
    is hundreds of megabytes and the retrieval graph takes seconds to build, and sharing
    one document controller means that cost is paid once for the session no matter how
    many regions are converted.

    Responsibilities, in the order they matter:

    - **Own the loaded voice.** Loading happens on a background thread and the result is
      published here.
    - **Drive rendering.** Conversions run on a thread pool, one region at a time per
      voice, because ONNX Runtime sessions are not safe to call concurrently and each
      render already saturates the cores it is given.
    - **Own the processing lock.** Renderers take it for reading, model changes take it
      for writing. This is the pattern JUCE's own ARA demo uses, and the reason it is
      correct here is that a render swap replaces a whole buffer that the audio thread may
      be part way through reading.
    - **Persist.** Settings and voice choice go into the session archive; rendered audio
      does not.
*/
// juce::ARAAudioSource::Listener is deliberately not inherited here: the specialisation
// already derives from it, and naming it again would make that base ambiguous while adding
// nothing — didEnableAudioSourceSamplesAccess below overrides it either way.
class DocumentController final : public juce::ARADocumentControllerSpecialisation,
                                private VoiceLoader::Listener
{
public:
    /** Constructed by ARA through the factory in PluginProcessor.cpp.

        Declared rather than inherited so the loader's listener can be registered here; a
        listener attached lazily on first use would miss the load that the host kicks off
        while restoring a session.
    */
    DocumentController (const ARA::PlugIn::PlugInEntry* entry,
                        const ARA::ARADocumentControllerHostInstance* instance);

    ~DocumentController() override;

    /** Something a renderer or editor holds while touching the model. */
    using ScopedProcessingLock = juce::ScopedTryReadLock;

    /** @returns A try-read lock over the model, for the audio thread.

        The audio thread must not block, so a failed acquisition is expected and the
        caller's correct response is to output silence for that block rather than to wait.
    */
    [[nodiscard]] ScopedProcessingLock getProcessingLock() { return loader.getModelReadLock(); }

    // ==============================================================================
    // Voices

    /** @returns The loader that owns the voices, shared with the insert-mode converter. */
    [[nodiscard]] VoiceLoader& getVoiceLoader() noexcept { return loader; }

    /** @returns The library of installed voices. Rescan before reading it. */
    [[nodiscard]] VoiceModelLibrary& getLibrary() noexcept { return loader.getLibrary(); }

    /** @returns The loaded voice, or nullptr while none is loaded. */
    [[nodiscard]] std::shared_ptr<const VoiceModel> getVoiceModel() const;

    /** @returns The voice currently loading or loaded, for the editor's label. */
    [[nodiscard]] juce::String getRequestedVoiceName() const;

    /** @returns Why the last load failed, or an empty string. */
    [[nodiscard]] juce::String getLoadError() const;

    /** @returns True while a voice is being loaded. */
    [[nodiscard]] bool isLoadingVoice() const noexcept { return loader.isLoading(); }

    /** Loads a voice by name, on a background thread, and re-renders everything after.

        Calling this with the name already loaded is a no-op, so the editor can call it
        freely on selection changes.

        @param name  Voice name from the library, or empty to unload.
    */
    void requestVoice (const juce::String& name);

    // ==============================================================================
    // Rendering

    /** Queues a conversion for one modification, cancelling any render already running
        for it.

        @param modification  What to convert.
    */
    void requestRender (ConversionModification& modification);

    /** Queues a conversion for every modification whose cache is stale. */
    void requestRenderForAllStaleModifications();

    /** Applies settings to a modification and queues a re-render if they changed.

        Routed through the document controller rather than set on the modification
        directly so that invalidation and requeueing cannot be forgotten at a call site.

        @param modification  What to change.
        @param settings      The new controls.
    */
    void applySettings (ConversionModification& modification, const ConversionSettings& settings);

    /** Applies a voice choice to a modification and queues a re-render if it changed. */
    void applyVoice (ConversionModification& modification, const juce::String& name);

    /** @returns Every modification in the document, for the editor and for bulk re-render. */
    [[nodiscard]] std::vector<ConversionModification*> getModifications();

    /** Something that wants to know when a render finishes or a voice finishes loading. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void conversionStateChanged() = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

protected:
    // ==============================================================================
    // ARADocumentControllerSpecialisation

    juce::ARAAudioModification* doCreateAudioModification (juce::ARAAudioSource* audioSource,
                                                           ARA::ARAAudioModificationHostRef hostRef,
                                                           const juce::ARAAudioModification* optionalModificationToClone) noexcept override;

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    bool doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                     const juce::ARARestoreObjectsFilter* filter) noexcept override;

    bool doStoreObjectsToStream (juce::ARAOutputStream& output,
                                 const juce::ARAStoreObjectsFilter* filter) noexcept override;

    void didEnableAudioSourceSamplesAccess (juce::ARAAudioSource* audioSource, bool enable) noexcept override;

private:
    /** A render in flight, on the thread pool. */
    class RenderJob;

    /** Publishes a finished render and notifies the host, on the message thread. */
    void completeRender (ConversionModification* modification, ConversionPointer conversion, juce::String error);

    /** Tells listeners and the host that something visible changed. */
    void notifyStateChanged();

    /** Cancels and waits out any render for a modification. */
    void cancelRender (ConversionModification& modification);

    void voiceStateChanged() override;

    // Voices, the model lock and the worker thread all live here, shared with the
    // insert-mode path so the two cannot drift.
    VoiceLoader loader;

    mutable juce::CriticalSection jobLock;
    std::map<ConversionModification*, RenderJob*> activeJobs;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DocumentController)
};

} // namespace rvcara
