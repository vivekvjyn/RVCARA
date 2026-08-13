#pragma once

#include "VoiceModel.h"
#include "VoiceModelLibrary.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace rvcara
{

/** Owns the installed voices, the loaded one, and the thread everything slow runs on.

    Extracted so that the two ways this plugin can work share it. When the host loads
    RVCARA through ARA, the document controller owns a loader; when it is loaded as an
    ordinary insert there is no document controller, and the processor owns one instead.
    Both need identical behaviour — asynchronous loading, a single voice at a time, a way to
    report progress — and duplicating that in two places is how the two paths drift apart.

    Loading is slow: hundreds of megabytes of ONNX graph, plus building the retrieval graph
    the first time a voice is used. It therefore happens on the worker thread and the result
    is published here, with listeners notified on the message thread.

    The same single-threaded pool serves loads and conversions. That is deliberate rather
    than a limitation: ONNX Runtime sessions are not safe to call concurrently at these
    options, each conversion already saturates the cores it is given, and serialising a load
    behind a render means a voice change cannot tear a conversion that is half finished.
*/
class VoiceLoader
{
public:
    VoiceLoader();
    ~VoiceLoader();

    VoiceLoader (const VoiceLoader&) = delete;
    VoiceLoader& operator= (const VoiceLoader&) = delete;

    /** Notified when a load starts, finishes or fails. Called on the message thread. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void voiceStateChanged() = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** @returns The library of installed voices. */
    [[nodiscard]] VoiceModelLibrary& getLibrary() noexcept { return library; }

    /** @returns The loaded voice, or nullptr.

        Returned by shared pointer so a conversion already in flight keeps the voice it
        started with alive even if the user switches away mid-render.
    */
    [[nodiscard]] std::shared_ptr<const VoiceModel> getVoice() const;

    /** @returns The name most recently requested, whether or not it finished loading. */
    [[nodiscard]] juce::String getRequestedName() const;

    /** @returns Why the last load failed, or an empty string. */
    [[nodiscard]] juce::String getError() const;

    /** @returns True while a load is running. */
    [[nodiscard]] bool isLoading() const noexcept { return loading.load (std::memory_order_acquire); }

    /** Requests a voice by name, loading it on the worker thread.

        A no-op when the named voice is already loaded, so callers may invoke it freely on
        every selection change.

        @param name        Voice name from the library, or empty to unload.
        @param onCompleted  Called on the message thread once the load settles, successfully
                            or not. Used by callers that need to requeue work afterwards.
    */
    void request (const juce::String& name, std::function<void()> onCompleted = {});

    /** Requests whichever voice a fresh instance should start with.

        There is nothing to choose when one voice is installed, and choosing is not the
        interesting part of the plug-in when several are, so an instance loads the first voice
        it finds rather than waiting to be told. Scanning is cheap — one small JSON per voice —
        and the load itself runs on the worker thread as any other does.

        @param onCompleted  As for request(). Not called when there is nothing to load.
        @returns            False when no voice is installed, so a caller can say so.
    */
    bool requestDefaultVoice (std::function<void()> onCompleted = {});

    /** @returns The shared worker pool, for callers that queue their own render jobs. */
    [[nodiscard]] juce::ThreadPool& getWorkerPool() noexcept { return workerPool; }

    /** Runs @c action under the write side of the model lock.

        Anything that replaces state the audio thread reads has to hold this. Exposed as a
        function rather than as the lock itself so callers cannot hold it across a slow
        operation by accident.
    */
    template <typename Action>
    void withModelLocked (Action&& action)
    {
        const juce::ScopedWriteLock lock { modelLock };
        action();
    }

    /** @returns A try-read lock for the audio thread.

        The audio thread must not block, so failing to acquire is expected and the correct
        response is to fall back to dry audio for that block rather than to wait.
    */
    [[nodiscard]] juce::ScopedTryReadLock getModelReadLock() { return juce::ScopedTryReadLock { modelLock }; }

private:
    /** Notifies listeners on the message thread. */
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

} // namespace rvcara
