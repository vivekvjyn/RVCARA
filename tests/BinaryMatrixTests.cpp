#include <engine/BinaryMatrix.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace rvcara::engine;

namespace
{
    /** Writes a matrix file with an overridable header, so malformed cases can be built. */
    juce::File writeMatrixFile (const juce::String& name,
                                const std::vector<float>& values,
                                int numRows,
                                int numColumns,
                                const char* magic = "RVCARAM1",
                                std::uint32_t version = 1,
                                std::uint32_t elementType = 0,
                                int bytesToTruncate = 0)
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("rvcara-tests")
                              .getChildFile (name);

        file.getParentDirectory().createDirectory();
        file.deleteFile();

        juce::MemoryOutputStream stream;
        stream.write (magic, 8);
        stream.writeInt (static_cast<int> (version));
        stream.writeInt (static_cast<int> (elementType));
        stream.writeInt (numRows);
        stream.writeInt (numColumns);
        stream.writeInt64 (0);

        const auto payloadBytes = static_cast<int> (values.size() * sizeof (float)) - bytesToTruncate;

        if (payloadBytes > 0)
            stream.write (values.data(), static_cast<std::size_t> (payloadBytes));

        file.replaceWithData (stream.getData(), stream.getDataSize());
        return file;
    }
} // namespace

TEST_CASE ("a well-formed matrix round-trips", "[matrix]")
{
    const std::vector<float> values { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
    const auto file = writeMatrixFile ("valid.bin", values, 2, 3);

    const auto matrix = BinaryMatrix::load (file);

    REQUIRE (matrix.isValid());
    CHECK (matrix.getError().isEmpty());
    CHECK (matrix.getNumRows() == 2);
    CHECK (matrix.getNumColumns() == 3);

    // Row-major: the second row starts at the fourth value.
    CHECK (matrix.getRow (0)[0] == 1.0f);
    CHECK (matrix.getRow (0)[2] == 3.0f);
    CHECK (matrix.getRow (1)[0] == 4.0f);
    CHECK (matrix.getRow (1)[2] == 6.0f);

    file.deleteFile();
}

TEST_CASE ("malformed matrices are refused with a reason", "[matrix]")
{
    const std::vector<float> values { 1.0f, 2.0f, 3.0f, 4.0f };

    SECTION ("a missing file")
    {
        const auto matrix = BinaryMatrix::load (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                    .getChildFile ("rvcara-does-not-exist.bin"));
        CHECK_FALSE (matrix.isValid());
        CHECK (matrix.getError().contains ("no such file"));
    }

    SECTION ("wrong magic")
    {
        const auto file = writeMatrixFile ("badmagic.bin", values, 2, 2, "NOTARVCA");
        const auto matrix = BinaryMatrix::load (file);

        CHECK_FALSE (matrix.isValid());
        CHECK (matrix.getError().contains ("not an RVCARA matrix"));
        file.deleteFile();
    }

    SECTION ("a future format version")
    {
        const auto file = writeMatrixFile ("version.bin", values, 2, 2, "RVCARAM1", 99);
        const auto matrix = BinaryMatrix::load (file);

        CHECK_FALSE (matrix.isValid());
        CHECK (matrix.getError().contains ("format version"));
        file.deleteFile();
    }

    SECTION ("an unsupported element type")
    {
        const auto file = writeMatrixFile ("dtype.bin", values, 2, 2, "RVCARAM1", 1, 7);
        const auto matrix = BinaryMatrix::load (file);

        CHECK_FALSE (matrix.isValid());
        CHECK (matrix.getError().contains ("element type"));
        file.deleteFile();
    }

    SECTION ("a truncated payload")
    {
        // The case that matters in the field: an interrupted download or a partial copy
        // of a 180 MB codebook. Without the length check this would read past the map.
        const auto file = writeMatrixFile ("truncated.bin", values, 2, 2, "RVCARAM1", 1, 0, 4);
        const auto matrix = BinaryMatrix::load (file);

        CHECK_FALSE (matrix.isValid());
        CHECK (matrix.getError().contains ("truncated"));
        file.deleteFile();
    }

    SECTION ("empty dimensions")
    {
        const auto file = writeMatrixFile ("empty.bin", values, 0, 4);
        const auto matrix = BinaryMatrix::load (file);

        CHECK_FALSE (matrix.isValid());
        CHECK (matrix.getError().contains ("empty dimensions"));
        file.deleteFile();
    }
}
