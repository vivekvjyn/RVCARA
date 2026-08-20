#include "model/VoiceModelLibrary.h"

#include <algorithm>

namespace rvcara
{
namespace
{
    const char* applicationFolderName = "RVCARA";
    const char* modelsFolderName = "models";
    const char* environmentVariableName = "RVCARA_MODEL_PATH";
}

VoiceModelLibrary::VoiceModelLibrary()
    : searchPaths (buildSearchPaths())
{
}

juce::File VoiceModelLibrary::getUserModelDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (applicationFolderName)
        .getChildFile (modelsFolderName);
}

std::vector<juce::File> VoiceModelLibrary::buildSearchPaths()
{
    std::vector<juce::File> paths;

    paths.push_back (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                         .getChildFile (applicationFolderName)
                         .getChildFile (modelsFolderName));

    paths.push_back (getUserModelDirectory());

   #if defined (RVCARA_DEVELOPMENT_MODEL_PATH)
    paths.push_back (juce::File { RVCARA_DEVELOPMENT_MODEL_PATH });
   #endif

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
                paths.push_back (juce::File (path.trim()));
    }

    return paths;
}

namespace
{
    template <typename Matches>
    juce::File findAsset (const std::vector<juce::File>& searchPaths,
                          const juce::String& name,
                          Matches&& matches)
    {
        juce::File firstCandidate;

        for (const auto& searchPath : searchPaths)
        {
            const auto candidate = searchPath.getChildFile (name);

            if (matches (candidate))
                return candidate;

            if (firstCandidate == juce::File())
                firstCandidate = candidate;
        }

        return firstCandidate;
    }
} // namespace

juce::File VoiceModelLibrary::findAssetDirectory (const juce::String& name)
{
    return findAsset (buildSearchPaths(), name,
                      [] (const juce::File& candidate) { return candidate.isDirectory(); });
}

juce::File VoiceModelLibrary::findAssetFile (const juce::String& name)
{
    return findAsset (buildSearchPaths(), name,
                      [] (const juce::File& candidate) { return candidate.existsAsFile(); });
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
}
