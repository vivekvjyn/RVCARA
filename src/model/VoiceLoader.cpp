#include "model/VoiceLoader.h"

#include <utility>

namespace rvcara
{
VoiceLoader::VoiceLoader() = default;

VoiceLoader::~VoiceLoader()
{
    workerPool.removeAllJobs (true, 10000);
}

void VoiceLoader::addListener (Listener* listener)    { listeners.add (listener); }
void VoiceLoader::removeListener (Listener* listener) { listeners.remove (listener); }

std::shared_ptr<const VoiceModel> VoiceLoader::getVoice() const
{
    const juce::ScopedLock lock { stateLock };
    return voice;
}

juce::String VoiceLoader::getRequestedName() const
{
    const juce::ScopedLock lock { stateLock };
    return requestedName;
}

juce::String VoiceLoader::getError() const
{
    const juce::ScopedLock lock { stateLock };
    return errorMessage;
}

void VoiceLoader::notifyListeners()
{
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        return;

    juce::MessageManager::callAsync ([this] { listeners.call (&Listener::voiceStateChanged); });
}

bool VoiceLoader::requestDefaultVoice (std::function<void()> onCompleted)
{
    if (library.getEntries().empty())
        library.rescan();

    const auto& entries = library.getEntries();

    if (entries.empty())
        return false;

    request (entries.front().name, std::move (onCompleted));
    return true;
}

void VoiceLoader::request (const juce::String& name, std::function<void()> onCompleted)
{
    {
        const juce::ScopedLock lock { stateLock };

        if (requestedName == name && (voice != nullptr || name.isEmpty()))
        {
            if (onCompleted)
                juce::MessageManager::callAsync (std::move (onCompleted));

            return;
        }

        requestedName = name;
        errorMessage.clear();
    }

    if (name.isEmpty())
    {
        withModelLocked ([this]
        {
            const juce::ScopedLock lock { stateLock };
            voice.reset();
        });

        notifyListeners();

        if (onCompleted)
            juce::MessageManager::callAsync (std::move (onCompleted));

        return;
    }

    library.rescan();
    const auto* entry = library.findByName (name);

    if (entry == nullptr)
    {
        {
            const juce::ScopedLock lock { stateLock };
            errorMessage = "no voice named " + name + " is installed";
        }

        notifyListeners();

        if (onCompleted)
            juce::MessageManager::callAsync (std::move (onCompleted));

        return;
    }

    loading.store (true, std::memory_order_release);
    notifyListeners();

    const auto directory = entry->directory;

    workerPool.addJob ([this, directory, name, onCompleted = std::move (onCompleted)]() mutable
    {
        juce::String error;
        std::shared_ptr<const VoiceModel> loaded = VoiceModel::load (directory, 0, error);

        juce::MessageManager::callAsync (
            [this, name, error, loaded, onCompleted = std::move (onCompleted)]() mutable
            {
                withModelLocked ([this, &name, &error, &loaded]
                {
                    const juce::ScopedLock lock { stateLock };

                    if (requestedName == name)
                    {
                        voice = loaded;
                        errorMessage = loaded != nullptr ? juce::String() : error;
                    }
                });

                loading.store (false, std::memory_order_release);
                notifyListeners();

                if (onCompleted)
                    onCompleted();
            });
    });
}
}
