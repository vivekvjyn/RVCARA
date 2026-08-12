#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace rvcara::engine
{

/** Finds the voice models installed on this machine.

    Discovery is separate from loading because they have very different costs. Listing
    what is available reads one small JSON per model and is cheap enough to do while
    building a menu; loading one is hundreds of megabytes and belongs on a background
    thread. The editor needs the former long before the user has chosen anything.

    Models are looked for, in order of increasing precedence:

    1. the shared application-data directory, where an installer would put voices;
    2. the per-user application-data directory, where a user's own exports go;
    3. `assets/models` relative to the running binary, so a development build finds
       what the exporter just wrote without an install step;
    4. any directory listed in the `RVCARA_MODEL_PATH` environment variable, which is
       how the tests point the library at a fixture.

    Later entries win on name collision, so a user's own export of a voice shadows a
    bundled one of the same name.
*/
class VoiceModelLibrary
{
public:
    /** One discovered model, described without loading its graphs. */
    struct Entry
    {
        juce::String name;
        juce::File directory;
        int modelSampleRate { 0 };
        juce::String trainingInfo;   ///< The checkpoint's own note, e.g. "200epoch"
        bool hasRetrieval { false };
        juce::int64 totalSizeInBytes { 0 };
    };

    VoiceModelLibrary();

    /** Rescans every search path.

        @returns The number of models found.
    */
    int rescan();

    /** @returns The models found by the last rescan, sorted by name. */
    [[nodiscard]] const std::vector<Entry>& getEntries() const noexcept { return entries; }

    /** @param name  Voice name to look for.
        @returns     The matching entry, or nullptr.
    */
    [[nodiscard]] const Entry* findByName (const juce::String& name) const noexcept;

    /** @returns The directories that were searched, for showing the user where to install. */
    [[nodiscard]] const std::vector<juce::File>& getSearchPaths() const noexcept { return searchPaths; }

    /** @returns The directory a user's own exports should go in. */
    [[nodiscard]] static juce::File getUserModelDirectory();

private:
    /** Builds the search path list for this platform. */
    void buildSearchPaths();

    /** Reads just enough of a manifest to describe a model without loading it. */
    [[nodiscard]] static std::optional<Entry> describeDirectory (const juce::File& directory);

    std::vector<juce::File> searchPaths;
    std::vector<Entry> entries;
};

} // namespace rvcara::engine
