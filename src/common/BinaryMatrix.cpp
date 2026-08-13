#include "common/BinaryMatrix.h"

namespace rvcara
{
namespace
{
    constexpr std::size_t headerSize = 32;
    constexpr int expectedFormatVersion = 1;
    constexpr int elementTypeFloat32 = 0;
    constexpr char expectedMagic[] = "RVCARAM1";
    constexpr std::size_t magicLength = 8;

    std::uint32_t readLittleEndianUInt32 (const std::uint8_t* bytes, std::size_t offset) noexcept
    {
        return static_cast<std::uint32_t> (bytes[offset])
             | (static_cast<std::uint32_t> (bytes[offset + 1]) << 8)
             | (static_cast<std::uint32_t> (bytes[offset + 2]) << 16)
             | (static_cast<std::uint32_t> (bytes[offset + 3]) << 24);
    }
}

BinaryMatrix BinaryMatrix::load (const juce::File& file)
{
    BinaryMatrix matrix;

    if (! file.existsAsFile())
    {
        matrix.error = "no such file: " + file.getFullPathName();
        return matrix;
    }

    auto mapping = std::make_shared<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readOnly);

    if (mapping->getData() == nullptr)
    {
        matrix.error = "could not map " + file.getFileName();
        return matrix;
    }

    const auto numBytes = mapping->getSize();

    if (numBytes < headerSize)
    {
        matrix.error = file.getFileName() + " is too short to hold a header";
        return matrix;
    }

    const auto* bytes = static_cast<const std::uint8_t*> (mapping->getData());

    for (std::size_t byteIndex = 0; byteIndex < magicLength; ++byteIndex)
    {
        if (bytes[byteIndex] != static_cast<std::uint8_t> (expectedMagic[byteIndex]))
        {
            matrix.error = file.getFileName() + " is not an RVCARA matrix";
            return matrix;
        }
    }

    const auto version = static_cast<int> (readLittleEndianUInt32 (bytes, 8));
    const auto elementType = static_cast<int> (readLittleEndianUInt32 (bytes, 12));
    const auto rows = static_cast<int> (readLittleEndianUInt32 (bytes, 16));
    const auto columns = static_cast<int> (readLittleEndianUInt32 (bytes, 20));

    if (version != expectedFormatVersion)
    {
        matrix.error = file.getFileName() + " has format version " + juce::String (version)
                     + ", expected " + juce::String (expectedFormatVersion);
        return matrix;
    }

    if (elementType != elementTypeFloat32)
    {
        matrix.error = file.getFileName() + " holds element type " + juce::String (elementType)
                     + ", only float32 is supported";
        return matrix;
    }

    if (rows <= 0 || columns <= 0)
    {
        matrix.error = file.getFileName() + " declares empty dimensions";
        return matrix;
    }

    const auto expectedBytes = headerSize
                             + static_cast<std::size_t> (rows) * static_cast<std::size_t> (columns) * sizeof (float);

    if (numBytes < expectedBytes)
    {
        matrix.error = file.getFileName() + " is truncated: header declares "
                     + juce::String (static_cast<juce::int64> (expectedBytes)) + " bytes, file holds "
                     + juce::String (static_cast<juce::int64> (numBytes));
        return matrix;
    }

    matrix.mapping = std::move (mapping);
    matrix.data = reinterpret_cast<const float*> (bytes + headerSize);
    matrix.numRows = rows;
    matrix.numColumns = columns;

    return matrix;
}
}
