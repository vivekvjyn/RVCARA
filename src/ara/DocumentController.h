#pragma once

#include "ara/ConversionModification.h"
#include "model/VoiceLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
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

    void applySettings (ConversionModification& modification, const ConversionSettings& settings);

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
    class RenderJob;

    void completeRender (ConversionModification* modification, ConversionPointer conversion, juce::String error);

    void notifyStateChanged();

    void cancelRender (ConversionModification& modification);

    void voiceStateChanged() override;

    VoiceLoader loader;

    mutable juce::CriticalSection jobLock;
    std::map<ConversionModification*, RenderJob*> activeJobs;

    mutable juce::CriticalSection rateLock;
    std::map<const PlaybackRenderer*, double> rendererSampleRates;
    std::atomic<double> sessionSampleRate { 0.0 };

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DocumentController)
};
}
