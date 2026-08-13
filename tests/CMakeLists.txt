add_executable(rvcara_tests
    BinaryMatrixTests.cpp
    ConversionSettingsTests.cpp
    MelSpectrogramTests.cpp
    PitchConversionsTests.cpp
    SincResamplerTests.cpp
    ZeroPhaseFilterTests.cpp)

target_link_libraries(rvcara_tests
    PRIVATE
        rvcara_engine
        rvcara_warnings
        GTest::gtest_main)

target_compile_definitions(rvcara_tests PRIVATE
    RVCARA_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

rvcara_copy_onnxruntime_beside(rvcara_tests)

rvcara_apply_strict_warnings(
    ConversionSettingsTests.cpp
    PitchConversionsTests.cpp
    SincResamplerTests.cpp)

include(GoogleTest)
gtest_discover_tests(rvcara_tests)
