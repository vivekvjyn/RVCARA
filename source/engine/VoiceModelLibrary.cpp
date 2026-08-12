#include "VoiceModelLibrary.h"

#include <algorithm>

namespace rvcara::engine
{

namespace
{
    const char* applicationFolderName = "RVCARA";
    const char* modelsFolderName = "models";
    const char* environmentVariableName = "RVCARA_MODEL_PATH";
} // namespace

VoiceModelLibrary::VoiceModelLibrary()
{
    buildSearchPaths();
}

juce::File VoiceModelLibrary::getUserModelDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (applicationFolderName)
        .getChildFile (modelsFolderName);
}

void VoiceModelLibrary::buildSearchPaths()
{
    searchPaths.clear();

    searchPaths.push_back (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                               .getChildFile (applicationFolderName)
                               .getChildFile (modelsFolderName));

    searchPaths.push_back (getUserModelDirectory());

    // A development build runs from a build tree; walk up looking for the repository's
    // assets directory so freshly exported voices are found without installing them.
    auto candidate = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    for (int depth = 0; depth < 8 && candidate.exists(); ++depth)
    {
        const auto assets = candidate.getChildFile ("assets").getChildFile (modelsFolderName);

        if (assets.isDirectory())
        {
            searchPaths.push_back (assets);
            break;
        }

        candidate = candidate.getParentDirectory();
    }

    // `override` would be legal as an identifier but reads as the contextual keyword.
    if (const auto extraPaths = juce::SystemStats::getEnvironmentVariable (environmentVariableName, {});
        extraPaths.isNotEmpty())
    {
       #if JUCE_WINDOWS
        const auto separator = ";";
       #else
        const auto separator = ":";
       #endif

        for (const auto& path : juce::StringArray::fromTokens (extraPaths, separator, {}))
            if (path.isNotEmpty())
                searchPaths.push_back (juce::File (path.trim()));
    }
}

std::optional<VoiceModelLibrary::Entry> VoiceModelLibrary::describeDirectory (const juce::File& directory)
{
    const auto manifestFile = directory.getChildFile ("manifest.json");

    if (! manifestFile.existsAsFile())
        return std::nullopt;

    const auto parsed = juce::JSON::parse (manifestFile.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
        return std::nullopt;

    Entry entry;
    entry.name = root->getProperty ("name").toString();
    entry.directory = directory;
    entry.modelSampleRate = static_cast<int> (static_cast<double> (root->getProperty ("modelSampleRate")));

    if (entry.name.isEmpty())
        entry.name = directory.getFileName();

    if (const auto retrievalValue = root->getProperty ("retrieval"); auto* retrieval = retrievalValue.getDynamicObject())
        entry.hasRetrieval = static_cast<double> (retrieval->getProperty ("numVectors")) > 0.0;

    if (const auto provenanceValue = root->getProperty ("provenance"); auto* provenance = provenanceValue.getDynamicObject())
        entry.trainingInfo = provenance->getProperty ("trainingInfo").toString();

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false))
        entry.totalSizeInBytes += file.getSize();

    return entry;
}

int VoiceModelLibrary::rescan()
{
    entries.clear();

    for (const auto& searchPath : searchPaths)
    {
        if (! searchPath.isDirectory())
            continue;

        for (const auto& child : searchPath.findChildFiles (juce::File::findDirectories, false))
        {
            auto described = describeDirectory (child);

            if (! described.has_value())
                continue;

            // Later search paths take precedence, so replace rather than skip.
            const auto existing = std::find_if (entries.begin(), entries.end(),
                                                [&described] (const Entry& candidate)
                                                { return candidate.name == described->name; });

            if (existing != entries.end())
                *existing = std::move (*described);
            else
                entries.push_back (std::move (*described));
        }
    }

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& first, const Entry& second)
               { return first.name.compareIgnoreCase (second.name) < 0; });

    return static_cast<int> (entries.size());
}

const VoiceModelLibrary::Entry* VoiceModelLibrary::findByName (const juce::String& name) const noexcept
{
    const auto found = std::find_if (entries.begin(), entries.end(),
                                     [&name] (const Entry& entry) { return entry.name == name; });

    return found != entries.end() ? &*found : nullptr;
}

} // namespace rvcara::engine
