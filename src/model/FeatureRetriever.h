#pragma once

#include "common/BinaryMatrix.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace hnswlib
{
    template <typename Type> class HierarchicalNSW;
    template <typename Type> class SpaceInterface;
}

namespace rvcara
{
/** @brief Blends content features toward the trained singer's, by approximate nearest-neighbour search over the codebook. */
class FeatureRetriever
{
public:
    FeatureRetriever();
    ~FeatureRetriever();

    FeatureRetriever (const FeatureRetriever&) = delete;
    FeatureRetriever& operator= (const FeatureRetriever&) = delete;

    /** @brief Search parameters, taken from the manifest. */
    struct Configuration
    {
        int numNeighbours { 8 };
        int graphDegree { 16 };
        int constructionCandidateListSize { 200 };
        int searchCandidateListSize { 64 };
    };

    bool prepare (const BinaryMatrix& codebook,
                  const Configuration& configuration,
                  const juce::File& cacheFile);

    void blend (float* features, int numFrames, float retrievalRatio) const;

    [[nodiscard]] bool isReady() const noexcept { return index != nullptr; }

    [[nodiscard]] const juce::String& getError() const noexcept { return error; }

    [[nodiscard]] int getNumVectors() const noexcept { return numVectors; }

private:
    void retrieveFrame (const float* feature, float* destination) const;

    const BinaryMatrix* sourceCodebook { nullptr };
    std::unique_ptr<hnswlib::SpaceInterface<float>> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index;

    Configuration config;
    int featureDim { 0 };
    int numVectors { 0 };
    juce::String error;
};
}
