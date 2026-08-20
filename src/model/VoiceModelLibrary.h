#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace rvcara
{
/** @brief Finds the voices installed on this machine, without loading them. */
class VoiceModelLibrary
{
public:
    /** @brief One discovered voice, described without loading it. */
    struct Entry
    {
        juce::String name;
        juce::File directory;
        int modelSampleRate { 0 };
        juce::String trainingInfo;
        bool hasRetrieval { false };
        juce::int64 totalSizeInBytes { 0 };
    };

    VoiceModelLibrary();

    int rescan();

    [[nodiscard]] const std::vector<Entry>& getEntries() const noexcept { return entries; }

    [[nodiscard]] const Entry* findByName (const juce::String& name) const noexcept;

    [[nodiscard]] const std::vector<juce::File>& getSearchPaths() const noexcept { return searchPaths; }

    [[nodiscard]] static juce::File getUserModelDirectory();

    /** @brief Every directory voices and shared assets are looked for in, most general first. */
    [[nodiscard]] static std::vector<juce::File> buildSearchPaths();

    /** @brief Finds a shared asset directory installed beside the voices.
        @param name  The directory's name, such as "GAME".
        @return The first search path that holds it, or a file that does not exist.
    */
    [[nodiscard]] static juce::File findAssetDirectory (const juce::String& name);

private:

    [[nodiscard]] static std::optional<Entry> describeDirectory (const juce::File& directory);

    std::vector<juce::File> searchPaths;
    std::vector<Entry> entries;
};
}
