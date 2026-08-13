#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>

namespace rvcara
{
/** @brief A memory-mapped, row-major float32 matrix, validated against its header on load. */
class BinaryMatrix
{
public:
    BinaryMatrix() = default;

    static BinaryMatrix load (const juce::File& file);

    [[nodiscard]] bool isValid() const noexcept { return data != nullptr; }

    [[nodiscard]] const juce::String& getError() const noexcept { return error; }

    [[nodiscard]] int getNumRows() const noexcept { return numRows; }
    [[nodiscard]] int getNumColumns() const noexcept { return numColumns; }

    [[nodiscard]] const float* getData() const noexcept { return data; }

    [[nodiscard]] const float* getRow (int rowIndex) const noexcept
    {
        jassert (isValid() && rowIndex >= 0 && rowIndex < numRows);
        return data + static_cast<std::size_t> (rowIndex) * static_cast<std::size_t> (numColumns);
    }

private:
    std::shared_ptr<juce::MemoryMappedFile> mapping;
    const float* data { nullptr };
    int numRows { 0 };
    int numColumns { 0 };
    juce::String error;
};
}
