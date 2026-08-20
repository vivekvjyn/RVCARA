#pragma once

#include "ara/ConversionModification.h"
#include "model/VoiceLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace rvcara
{
class PlaybackRenderer;

/** @brief Owns the ARA model, the voice, the render pool and the processing lock. */
class DocumentController final : public juce::ARADocumentControllerSpecialisation,
                                private VoiceLoader::Listener
{
public:
    DocumentController (const ARA::PlugIn::PlugInEntry* entry,
                        const ARA::ARADocumentControllerHostInstance* instance);

    ~DocumentController() override;

    using ScopedProcessingLock = juce::ScopedTryReadLock;

    [[nodiscard]] ScopedProcessingLock getProcessingLock() { return loader.getModelReadLock(); }

    [[nodiscard]] VoiceLoader& getVoiceLoader() noexcept { return loader; }

    [[nodiscard]] VoiceModelLibrary& getLibrary() noexcept { return loader.getLibrary(); }

    [[nodiscard]] std::shared_ptr<const VoiceModel> getVoiceModel() const;

    [[nodiscard]] juce::String getRequestedVoiceName() const;

    [[nodiscard]] juce::String getLoadError() const;

    [[nodiscard]] bool isLoadingVoice() const noexcept { return loader.isLoading(); }

    void requestVoice (const juce::String& name);

    /** @brief Tells the controller which rate a renderer has been prepared at.
        @param renderer    The renderer reporting the rate.
        @param sampleRate  The rate the host prepared it at.
    */
    void setSessionSampleRate (const PlaybackRenderer* renderer, double sampleRate);

    /** @brief Drops a renderer's contribution to the session rate.
        @param renderer  The renderer being destroyed.
    */
    void forgetRenderer (const PlaybackRenderer* renderer);

    /** @brief The rate conversions are rendered at, or zero until a renderer reports one. */
    [[nodiscard]] double getSessionSampleRate() const noexcept
    {
        return sessionSampleRate.load (std::memory_order_acquire);
    }

    void requestRender (ConversionModification& modification);

    void requestRenderForAllStaleModifications();

    /** @brief Splits a region into notes, unless it already has them or is being split now.
        @param modification  The region to split.
    */
    void requestNoteDetection (ConversionModification& modification);

    void applySettings (ConversionModification& modification, const ConversionSettings& settings);

    /** @brief Adopts an edited note list and re-renders the region to match it.
        @param modification  The region the editor was showing.
        @param edit          The notes as the user left them.
    */
    void applyPitchEdit (ConversionModification& modification, PitchEdit edit);

    void applyVoice (ConversionModification& modification, const juce::String& name);

    [[nodiscard]] std::vector<ConversionModification*> getModifications();

    /** @brief Notified on the message thread when a conversion or the voice changes. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void conversionStateChanged() = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

protected:

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
    class AbortableJob;

    /** @brief One job in flight for one modification. The generation tells a message that
               arrives late from one that is still current, without comparing job pointers the
               pool may already have freed and reused.
    */
    struct ActiveJob
    {
        AbortableJob* job { nullptr };
        std::uint64_t generation { 0 };
    };

    class RenderJob;
    class DetectJob;

    void completeRender (ConversionModification* modification,
                         std::uint64_t generation,
                         ConversionPointer conversion,
                         juce::String error);

    void completeDetection (ConversionModification* modification,
                            std::uint64_t generation,
                            PitchEdit edit,
                            juce::String error);

    void publishPartialRender (ConversionModification* modification,
                               std::uint64_t generation,
                               ConversionPointer conversion);

    void notifyStateChanged();

    /** @brief The jobs in flight, one entry per modification. */
    using JobMap = std::map<ConversionModification*, ActiveJob>;

    void cancelRender (ConversionModification& modification);

    void voiceStateChanged() override;

    VoiceLoader loader;

    /** @brief Note detection gets a thread of its own, so the notes arrive while the voice is
               still being converted rather than after it.
    */
    juce::ThreadPool detectionPool { juce::ThreadPoolOptions {}.withNumberOfThreads (1)
                                                               .withThreadName ("RVCARA notes") };

    mutable juce::CriticalSection jobLock;
    JobMap activeJobs;
    JobMap activeDetections;
    std::uint64_t nextGeneration { 1 };

    mutable juce::CriticalSection rateLock;
    std::map<const PlaybackRenderer*, double> rendererSampleRates;
    std::atomic<double> sessionSampleRate { 0.0 };

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DocumentController)
};
}
