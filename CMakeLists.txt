cmake_minimum_required(VERSION 3.24)

project(RVCARA
    VERSION 0.1.0
    DESCRIPTION "Retrieval-based voice conversion as an ARA plug-in"
    HOMEPAGE_URL "https://github.com/vivekvjyn/RVCARA"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(RVCARA_BUILD_TESTS "Build the unit tests" ON)

set(RVCARA_ONNXRUNTIME_ROOT "" CACHE PATH
    "Existing ONNX Runtime installation to use instead of building libs/onnxruntime")

if(RVCARA_ONNXRUNTIME_ROOT)
    set(onnxruntimeRoot "${RVCARA_ONNXRUNTIME_ROOT}")
    message(STATUS "ONNX Runtime: using ${onnxruntimeRoot}")

    if(EXISTS "${onnxruntimeRoot}/include/onnxruntime/onnxruntime_cxx_api.h")
        set(onnxruntimeIncludeDir "${onnxruntimeRoot}/include/onnxruntime")
    elseif(EXISTS "${onnxruntimeRoot}/include/onnxruntime_cxx_api.h")
        set(onnxruntimeIncludeDir "${onnxruntimeRoot}/include")
    else()
        message(FATAL_ERROR "No onnxruntime_cxx_api.h under ${onnxruntimeRoot}/include")
    endif()
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/onnxruntime/cmake/CMakeLists.txt")
        message(FATAL_ERROR
            "libs/onnxruntime is empty. Run:\n"
            "    git submodule update --init --depth 1 libs/onnxruntime\n"
            "    git -C libs/onnxruntime submodule update --init --depth 1 cmake/external/onnx")
    endif()

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/onnxruntime/cmake/external/onnx/CMakeLists.txt")
        message(FATAL_ERROR
            "ONNX Runtime's schema submodule is not checked out. Run:\n"
            "    git -C libs/onnxruntime submodule update --init --depth 1 cmake/external/onnx")
    endif()

    find_package(Python3 3.10 COMPONENTS Interpreter)

    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR
            "Building ONNX Runtime needs a Python 3.10+ interpreter for its symbol-list "
            "generator (tools/ci_build/gen_def.py). Install one, or point "
            "RVCARA_ONNXRUNTIME_ROOT at a prebuilt installation.")
    endif()

    include(ExternalProject)

    set(onnxruntimeRoot "${CMAKE_BINARY_DIR}/onnxruntime-install")
    set(onnxruntimeCxxFlags "")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
        set(onnxruntimeCxxFlags "-include cstdint")
    endif()

    if(WIN32)
        set(onnxruntimeBuiltLibrary "${onnxruntimeRoot}/bin/onnxruntime.dll")
    elseif(APPLE)
        set(onnxruntimeBuiltLibrary "${onnxruntimeRoot}/lib/libonnxruntime.dylib")
    else()
        set(onnxruntimeBuiltLibrary "${onnxruntimeRoot}/lib/libonnxruntime.so")
    endif()

    message(STATUS "ONNX Runtime: building libs/onnxruntime into ${onnxruntimeRoot}")

    ExternalProject_Add(rvcara_onnxruntime_build
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/onnxruntime/cmake"
        BINARY_DIR "${CMAKE_BINARY_DIR}/onnxruntime-build"
        INSTALL_DIR "${onnxruntimeRoot}"
        CMAKE_ARGS
            --compile-no-warning-as-error
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_INSTALL_PREFIX=${onnxruntimeRoot}"
            "-DCMAKE_CXX_FLAGS=${onnxruntimeCxxFlags}"
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            -Donnxruntime_BUILD_SHARED_LIB=ON
            -Donnxruntime_BUILD_UNIT_TESTS=OFF
            -Donnxruntime_BUILD_BENCHMARKS=OFF
            -Donnxruntime_ENABLE_PYTHON=OFF
        BUILD_BYPRODUCTS "${onnxruntimeBuiltLibrary}"
        USES_TERMINAL_CONFIGURE TRUE
        USES_TERMINAL_BUILD TRUE
        USES_TERMINAL_INSTALL TRUE)

    set(onnxruntimeIncludeDir "${onnxruntimeRoot}/include/onnxruntime")
endif()

if(WIN32)
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/onnxruntime.lib")
    set(onnxruntimeLibrary "${onnxruntimeRoot}/bin/onnxruntime.dll")
elseif(APPLE)
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/libonnxruntime.dylib")
    set(onnxruntimeLibrary "${onnxruntimeImplib}")
else()
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/libonnxruntime.so")
    set(onnxruntimeLibrary "${onnxruntimeImplib}")
endif()

file(MAKE_DIRECTORY "${onnxruntimeIncludeDir}")

add_library(rvcara_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(rvcara_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${onnxruntimeLibrary}"
    IMPORTED_IMPLIB "${onnxruntimeImplib}"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntimeIncludeDir}")

set(RVCARA_ONNXRUNTIME_LIBRARY_DIR "${onnxruntimeRoot}/lib")

function(rvcara_wait_for_onnxruntime target)
    if(TARGET rvcara_onnxruntime_build)
        add_dependencies(${target} rvcara_onnxruntime_build)
    endif()
endfunction()

set(rvcaraCopyOnnxRuntimeScript "${CMAKE_BINARY_DIR}/rvcara_copy_onnxruntime.cmake")

file(WRITE "${rvcaraCopyOnnxRuntimeScript}" "\
file(GLOB libraries \"\${libraryDir}/libonnxruntime*\")

foreach(library \${libraries})
    if(NOT IS_DIRECTORY \"\${library}\")
        file(COPY \"\${library}\" DESTINATION \"\${destination}\" FOLLOW_SYMLINK_CHAIN)
    endif()
endforeach()
")

function(rvcara_copy_onnxruntime_beside target)
    rvcara_wait_for_onnxruntime(${target})

    if(APPLE)
        set(loaderRelativePath "@loader_path")
    else()
        set(loaderRelativePath "$ORIGIN")
    endif()

    set_target_properties(${target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${loaderRelativePath}")

    if(WIN32)
        add_custom_command(TARGET ${target} PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${onnxruntimeLibrary}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    else()
        add_custom_command(TARGET ${target} PRE_LINK
            COMMAND ${CMAKE_COMMAND}
                    "-DlibraryDir=${RVCARA_ONNXRUNTIME_LIBRARY_DIR}"
                    "-Ddestination=$<TARGET_FILE_DIR:${target}>"
                    -P "${rvcaraCopyOnnxRuntimeScript}" VERBATIM)
    endif()
endfunction()

foreach(submodule JUCE ARA_SDK googletest hnswlib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/README.md"
       AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/CMakeLists.txt")
        message(FATAL_ERROR "libs/${submodule} is empty. See the build steps in README.md.")
    endif()
endforeach()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/ARA_SDK/ARA_Library/PlugIn/ARAPlug.h")
    message(FATAL_ERROR
        "The ARA SDK's own submodules are not checked out. Run:\n"
        "    git -C libs/ARA_SDK submodule update --init --depth 1 ARA_API ARA_Library")
endif()

add_subdirectory(libs/JUCE)

juce_set_ara_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/libs/ARA_SDK")

add_library(rvcara_hnswlib INTERFACE)
target_include_directories(rvcara_hnswlib SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libs/hnswlib")

add_library(rvcara_warnings INTERFACE)

if(MSVC)
    target_compile_options(rvcara_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
else()
    target_compile_options(rvcara_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic
            -Wshadow                # a shadowed sampleRate is a real bug here
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual>)  # catches a mistyped ARA override
endif()

function(rvcara_apply_strict_warnings)
    if(MSVC)
        return()
    endif()

    foreach(sourceFile ${ARGN})
        set_property(SOURCE "${sourceFile}" APPEND PROPERTY COMPILE_OPTIONS
            -Wdouble-promotion      # float32 audio silently widened to double
            -Wfloat-conversion
            -Wconversion            # and every other implicit narrowing
            -Wsign-conversion       # a negative index into a std::size_t
            -Wold-style-cast
            -Wuseless-cast
            -Wcast-qual
            -Wextra-semi)
    endforeach()
endfunction()

set(RVCARA_ENGINE_SOURCES
    src/common/BinaryMatrix.cpp
    src/dsp/MelSpectrogram.cpp
    src/dsp/SincResampler.cpp
    src/dsp/ZeroPhaseFilter.cpp
    src/model/ConversionEngine.cpp
    src/model/FeatureRetriever.cpp
    src/model/ModelManifest.cpp
    src/model/OnnxSession.cpp
    src/model/PitchEstimator.cpp
    src/model/VoiceLoader.cpp
    src/model/VoiceModel.cpp
    src/model/VoiceModelLibrary.cpp)

add_library(rvcara_engine STATIC ${RVCARA_ENGINE_SOURCES})
target_include_directories(rvcara_engine PUBLIC src)

rvcara_wait_for_onnxruntime(rvcara_engine)

target_link_libraries(rvcara_engine
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_dsp
        rvcara_onnxruntime
    PRIVATE
        rvcara_hnswlib
        rvcara_warnings)

target_compile_definitions(rvcara_engine PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_compile_definitions(rvcara_engine PRIVATE
    RVCARA_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")

if(NOT MSVC)
    set_property(SOURCE src/model/FeatureRetriever.cpp APPEND PROPERTY COMPILE_OPTIONS
        -Wno-unused-parameter -Wno-sign-compare)
endif()

set(RVCARA_FORMATS VST3 LV2)

if(APPLE)
    list(APPEND RVCARA_FORMATS AU)
endif()

juce_add_plugin(RVCARA
    PLUGIN_MANUFACTURER_CODE Pinn
    PLUGIN_CODE Rvca
    MANUFACTURER_NAME "Pitch Innovations"
    MANUFACTURER_WEBSITE "https://github.com/vivekvjyn/RVCARA"
    COMPANY_NAME "Pitch Innovations"
    BUNDLE_ID "com.pitchinnovations.rvcara"
    PRODUCT_NAME "RVCARA"
    DESCRIPTION "Retrieval-based voice conversion as an ARA plug-in"

    FORMATS ${RVCARA_FORMATS}
    LV2URI "https://github.com/vivekvjyn/RVCARA"
    IS_ARA_EFFECT TRUE
    ARA_FACTORY_ID "\"com.pitchinnovations.rvcara.arafactory.0.1.0\""
    ARA_DOCUMENT_ARCHIVE_ID "\"com.pitchinnovations.rvcara.aradocumentarchive.1\""

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE)

set(RVCARA_PLUGIN_SOURCES
    src/Editor.cpp
    src/Processor.cpp
    src/ara/ConversionModification.cpp
    src/ara/DocumentController.cpp
    src/ara/PlaybackRenderer.cpp
    src/insert/InsertConverter.cpp
    src/ui/PanelLookAndFeel.cpp
    src/ui/PitchCurveView.cpp)

target_sources(RVCARA PRIVATE ${RVCARA_PLUGIN_SOURCES})
target_include_directories(RVCARA PRIVATE src)

target_compile_definitions(RVCARA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(RVCARA
    PRIVATE
        rvcara_engine
        rvcara_warnings
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

rvcara_apply_strict_warnings(
    src/dsp/SincResampler.cpp
    src/dsp/ZeroPhaseFilter.cpp)

foreach(format ${RVCARA_FORMATS})
    if(TARGET RVCARA_${format})
        rvcara_copy_onnxruntime_beside(RVCARA_${format})
    endif()
endforeach()

find_package(Doxygen)

if(DOXYGEN_FOUND)
    add_custom_target(rvcara_docs
        COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        COMMENT "Generating API documentation"
        VERBATIM)
endif()

if(RVCARA_BUILD_TESTS)
    enable_testing()
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(libs/googletest)
    add_subdirectory(tests)
endif()
