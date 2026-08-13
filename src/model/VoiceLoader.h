#pragma once

#include "model/VoiceModel.h"
#include "model/VoiceModelLibrary.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace rvcara
{
/** @brief Owns the installed voices, the loaded one, and the worker thread everything slow runs on. */
class VoiceLoader
{
public:
    VoiceLoader();
    ~VoiceLoader();

    VoiceLoader (const VoiceLoader&) = delete;
    VoiceLoader& operator= (const VoiceLoader&) = delete;

    /** @brief Notified on the message thread when a load starts, finishes or fails. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void voiceStateChanged() = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    [[nodiscard]] VoiceModelLibrary& getLibrary() noexcept { return library; }

    [[nodiscard]] std::shared_ptr<const VoiceModel> getVoice() const;

    [[nodiscard]] juce::String getRequestedName() const;

    [[nodiscard]] juce::String getError() const;

    [[nodiscard]] bool isLoading() const noexcept { return loading.load (std::memory_order_acquire); }

    void request (const juce::String& name, std::function<void()> onCompleted = {});

    bool requestDefaultVoice (std::function<void()> onCompleted = {});

    [[nodiscard]] juce::ThreadPool& getWorkerPool() noexcept { return workerPool; }

    template <typename Action>
    void withModelLocked (Action&& action)
    {
        const juce::ScopedWriteLock lock { modelLock };
        action();
    }

    [[nodiscard]] juce::ScopedTryReadLock getModelReadLock() { return juce::ScopedTryReadLock { modelLock }; }

private:
    void notifyListeners();

    VoiceModelLibrary library;

    juce::ReadWriteLock modelLock;

    mutable juce::CriticalSection stateLock;
    std::shared_ptr<const VoiceModel> voice;
    juce::String requestedName;
    juce::String errorMessage;
    std::atomic<bool> loading { false };

    juce::ThreadPool workerPool { juce::ThreadPoolOptions {}.withNumberOfThreads (1)
                                                            .withThreadName ("RVCARA worker") };

    juce::ListenerList<Listener> listeners;
};
}
