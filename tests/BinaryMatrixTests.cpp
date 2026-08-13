#include "common/BinaryMatrix.h"

#include <gtest/gtest.h>

#include <vector>

using namespace rvcara;

class BinaryMatrixTest : public testing::Test
{
protected:
    void TearDown() override { directory.deleteRecursively(); }

    juce::File write (const juce::String& name,
                      int numRows,
                      int numColumns,
                      const char* magic = "RVCARAM1",
                      std::uint32_t version = 1,
                      std::uint32_t elementType = 0,
                      int bytesToTruncate = 0) const
    {
        directory.createDirectory();

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

        const auto file = directory.getChildFile (name);
        file.replaceWithData (stream.getData(), stream.getDataSize());
        return file;
    }

    const juce::File directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("rvcara-tests");

    const std::vector<float> values { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
};

TEST_F (BinaryMatrixTest, AWellFormedMatrixRoundTrips)
{
    const auto matrix = BinaryMatrix::load (write ("valid.bin", 2, 3));

    ASSERT_TRUE (matrix.isValid()) << matrix.getError();
    EXPECT_TRUE (matrix.getError().isEmpty());
    EXPECT_EQ (matrix.getNumRows(), 2);
    EXPECT_EQ (matrix.getNumColumns(), 3);
    EXPECT_EQ (matrix.getRow (0)[0], 1.0f);
    EXPECT_EQ (matrix.getRow (0)[2], 3.0f);
    EXPECT_EQ (matrix.getRow (1)[0], 4.0f);
    EXPECT_EQ (matrix.getRow (1)[2], 6.0f);
}

TEST_F (BinaryMatrixTest, AMissingFileIsRefused)
{
    const auto matrix = BinaryMatrix::load (directory.getChildFile ("does-not-exist.bin"));

    EXPECT_FALSE (matrix.isValid());
    EXPECT_TRUE (matrix.getError().contains ("no such file"));
}

TEST_F (BinaryMatrixTest, WrongMagicIsRefused)
{
    const auto matrix = BinaryMatrix::load (write ("magic.bin", 2, 2, "NOTARVCA"));

    EXPECT_FALSE (matrix.isValid());
    EXPECT_TRUE (matrix.getError().contains ("not an RVCARA matrix"));
}

TEST_F (BinaryMatrixTest, AFutureFormatVersionIsRefused)
{
    const auto matrix = BinaryMatrix::load (write ("version.bin", 2, 2, "RVCARAM1", 99));

    EXPECT_FALSE (matrix.isValid());
    EXPECT_TRUE (matrix.getError().contains ("format version"));
}

TEST_F (BinaryMatrixTest, AnUnsupportedElementTypeIsRefused)
{
    const auto matrix = BinaryMatrix::load (write ("type.bin", 2, 2, "RVCARAM1", 1, 7));

    EXPECT_FALSE (matrix.isValid());
    EXPECT_TRUE (matrix.getError().contains ("element type"));
}

TEST_F (BinaryMatrixTest, ATruncatedPayloadIsRefused)
{
    const auto matrix = BinaryMatrix::load (write ("truncated.bin", 2, 3, "RVCARAM1", 1, 0, 4));

    EXPECT_FALSE (matrix.isValid());
    EXPECT_TRUE (matrix.getError().contains ("truncated"));
}

TEST_F (BinaryMatrixTest, EmptyDimensionsAreRefused)
{
    const auto matrix = BinaryMatrix::load (write ("empty.bin", 0, 4));

    EXPECT_FALSE (matrix.isValid());
    EXPECT_TRUE (matrix.getError().contains ("empty dimensions"));
}
