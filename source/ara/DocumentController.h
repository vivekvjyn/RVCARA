#pragma once

#include "ConversionModification.h"

#include <engine/VoiceModel.h>
#include <engine/VoiceModelLibrary.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace rvcara::ara
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
class DocumentController final : public juce::ARADocumentControllerSpecialisation,
                                private juce::ARAAudioSource::Listener
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    ~DocumentController() override;

    /** Something a renderer or editor holds while touching the model. */
    using ScopedProcessingLock = juce::ScopedTryReadLock;

    /** @returns A try-read lock over the model, for the audio thread.

        The audio thread must not block, so a failed acquisition is expected and the
        caller's correct response is to output silence for that block rather than to wait.
    */
    [[nodiscard]] ScopedProcessingLock getProcessingLock() { return ScopedProcessingLock { processingLock }; }

    // ==============================================================================
    // Voices

    /** @returns The library of installed voices. Rescan before reading it. */
    [[nodiscard]] engine::VoiceModelLibrary& getLibrary() noexcept { return library; }

    /** @returns The loaded voice, or nullptr while none is loaded. */
    [[nodiscard]] std::shared_ptr<const engine::VoiceModel> getVoiceModel() const;

    /** @returns The voice currently loading or loaded, for the editor's label. */
    [[nodiscard]] juce::String getRequestedVoiceName() const;

    /** @returns Why the last load failed, or an empty string. */
    [[nodiscard]] juce::String getLoadError() const;

    /** @returns True while a voice is being loaded. */
    [[nodiscard]] bool isLoadingVoice() const noexcept { return isLoading.load (std::memory_order_acquire); }

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
    void applySettings (ConversionModification& modification, const engine::ConversionSettings& settings);

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

    engine::VoiceModelLibrary library;

    // Guards the model against the audio thread. Read by renderers, written by anything
    // that publishes a render or swaps the voice.
    juce::ReadWriteLock processingLock;

    mutable juce::CriticalSection voiceLock;
    std::shared_ptr<const engine::VoiceModel> voiceModel;
    juce::String requestedVoiceName;
    juce::String loadError;
    std::atomic<bool> isLoading { false };

    // One thread. Renders saturate the cores ONNX Runtime is given, and a voice's ONNX
    // sessions cannot be called concurrently, so a second worker would contend rather
    // than help.
    juce::ThreadPool renderPool { juce::ThreadPoolOptions {}.withNumberOfThreads (1)
                                                            .withThreadName ("RVCARA render") };

    mutable juce::CriticalSection jobLock;
    std::map<ConversionModification*, RenderJob*> activeJobs;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DocumentController)
};

} // namespace rvcara::ara
