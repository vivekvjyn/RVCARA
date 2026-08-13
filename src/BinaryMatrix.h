#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>

namespace rvcara
{

/** A dense float32 matrix read from the exporter's binary container.

    Two exported assets are large constant matrices — the retrieval codebook, which
    for a few minutes of training audio runs to around 180 MB, and the mel filter
    bank. Loading either through a parser would mean holding two copies; instead the
    file is memory-mapped and the data used in place, so the codebook costs no
    resident memory until the pages are touched and is shared between plugin
    instances by the operating system's page cache.

    The format is written by the exporter's `binary_matrix.py`, and is spelled out here
    because this is now the only description of it in the tree: a 32-byte header of
    magic, version, element type and dimensions, then row-major float32. The header is
    validated on load, including that the payload length agrees with the declared
    dimensions, so a truncated download fails here rather than as a segmentation fault
    during a render.
*/
class BinaryMatrix
{
public:
    BinaryMatrix() = default;

    /** Memory-maps a matrix file.

        @param file  The file to open.
        @returns     A loaded matrix, or an empty one if the file is missing or malformed;
                     check with isValid() and read getError() for the reason.
    */
    static BinaryMatrix load (const juce::File& file);

    /** @returns Whether a matrix was successfully loaded. */
    [[nodiscard]] bool isValid() const noexcept { return data != nullptr; }

    /** @returns Why loading failed, or an empty string on success. */
    [[nodiscard]] const juce::String& getError() const noexcept { return error; }

    [[nodiscard]] int getNumRows() const noexcept { return numRows; }
    [[nodiscard]] int getNumColumns() const noexcept { return numColumns; }

    /** @returns Pointer to the whole payload, row-major, or nullptr if invalid. */
    [[nodiscard]] const float* getData() const noexcept { return data; }

    /** @param rowIndex  Row to address; not bounds-checked in release builds.
        @returns         Pointer to the start of that row.
    */
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

} // namespace rvcara
