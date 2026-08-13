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

# ==================================================================================
# ONNX Runtime
#
# The prebuilt release is used rather than a submodule. Building it from source takes tens
# of minutes and pulls in its own dependency tree for nothing: the CPU execution provider is
# the only one used here, and the released binaries are what Microsoft tests. Pinned by
# SHA256 so a build cannot silently pick up different bits.
#
# Set RVCARA_ONNXRUNTIME_ROOT to use an existing installation, which is also the escape
# hatch for platforms with no published archive.
# ==================================================================================

set(RVCARA_ONNXRUNTIME_VERSION "1.23.2" CACHE STRING "ONNX Runtime release to download")
set(RVCARA_ONNXRUNTIME_ROOT "" CACHE PATH "Existing ONNX Runtime installation, if any")

if(RVCARA_ONNXRUNTIME_ROOT)
    set(onnxruntimeRoot "${RVCARA_ONNXRUNTIME_ROOT}")
    message(STATUS "ONNX Runtime: using ${onnxruntimeRoot}")
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(archive "onnxruntime-linux-x64-${RVCARA_ONNXRUNTIME_VERSION}.tgz")
        set(archiveHash "1fa4dcaef22f6f7d5cd81b28c2800414350c10116f5fdd46a2160082551c5f9b")
    elseif(APPLE)
        # universal2 carries arm64 and x86_64, so one download serves either or both.
        set(archive "onnxruntime-osx-universal2-${RVCARA_ONNXRUNTIME_VERSION}.tgz")
        set(archiveHash "49ae8e3a66ccb18d98ad3fe7f5906b6d7887df8a5edd40f49eb2b14e20885809")
    elseif(WIN32)
        set(archive "onnxruntime-win-x64-${RVCARA_ONNXRUNTIME_VERSION}.zip")
        set(archiveHash "0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd")
    else()
        message(FATAL_ERROR
            "No pinned ONNX Runtime archive for ${CMAKE_SYSTEM_NAME}. "
            "Build it yourself and set RVCARA_ONNXRUNTIME_ROOT.")
    endif()

    if(NOT RVCARA_ONNXRUNTIME_VERSION STREQUAL "1.23.2")
        message(WARNING
            "The pinned checksums are for 1.23.2; ${RVCARA_ONNXRUNTIME_VERSION} will fail its "
            "hash check. Set RVCARA_ONNXRUNTIME_ROOT, or update the hashes here.")
    endif()

    include(FetchContent)
    FetchContent_Declare(onnxruntime
        URL "https://github.com/microsoft/onnxruntime/releases/download/v${RVCARA_ONNXRUNTIME_VERSION}/${archive}"
        URL_HASH "SHA256=${archiveHash}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

    message(STATUS "ONNX Runtime: fetching ${archive}")
    FetchContent_MakeAvailable(onnxruntime)
    set(onnxruntimeRoot "${onnxruntime_SOURCE_DIR}")
endif()

if(NOT EXISTS "${onnxruntimeRoot}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "No ONNX Runtime headers under ${onnxruntimeRoot}/include")
endif()

if(WIN32)
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/onnxruntime.lib")
    set(onnxruntimeLibrary "${onnxruntimeRoot}/lib/onnxruntime.dll")
elseif(APPLE)
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/libonnxruntime.dylib")
    set(onnxruntimeLibrary "${onnxruntimeImplib}")
else()
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/libonnxruntime.so")
    set(onnxruntimeLibrary "${onnxruntimeImplib}")
endif()

if(NOT EXISTS "${onnxruntimeImplib}")
    message(FATAL_ERROR "No ONNX Runtime library at ${onnxruntimeImplib}")
endif()

add_library(rvcara_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(rvcara_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${onnxruntimeLibrary}"
    IMPORTED_IMPLIB "${onnxruntimeImplib}"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntimeRoot}/include")

set(RVCARA_ONNXRUNTIME_LIBRARY_DIR "${onnxruntimeRoot}/lib")

# A plug-in cannot rely on the host's library search path, so every artefact gets its own
# copy. The whole lib directory is copied on Linux and macOS because the loader follows the
# versioned soname, not the bare filename.
function(rvcara_copy_onnxruntime_beside target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${onnxruntimeLibrary}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                    "${RVCARA_ONNXRUNTIME_LIBRARY_DIR}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    endif()
endfunction()

# ==================================================================================
# Submodules
# ==================================================================================

foreach(submodule JUCE ARA_SDK hnswlib)
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

# JUCE's headers are not ours to fix, and the strict tier further down finds several hundred
# old-style casts and float promotions inside them — noise that buries anything real in our own
# code. Promoting the module include directories to system includes silences warnings that
# originate in those headers while leaving our sources fully checked. Done by rewriting the
# targets' properties because JUCE's own CMake exports them as ordinary include directories.
function(rvcara_treat_includes_as_system directory)
    get_property(targetsInDirectory DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)

    foreach(target ${targetsInDirectory})
        get_target_property(includeDirectories ${target} INTERFACE_INCLUDE_DIRECTORIES)

        if(includeDirectories)
            set_target_properties(${target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${includeDirectories}")
        endif()
    endforeach()

    foreach(subdirectory ${subdirectories})
        rvcara_treat_includes_as_system("${subdirectory}")
    endforeach()
endfunction()

rvcara_treat_includes_as_system("${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE")

# Defined by JUCE's CMake utilities, so it can only be called after JUCE is added — but it
# must still precede any target depending on the ARA SDK. JUCE compiles the ARA library's
# sources itself, so an include path is all it needs from us.
juce_set_ara_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/libs/ARA_SDK")

# hnswlib is header only, and its own CMakeLists builds examples and tests we have no use
# for, so it is consumed as an interface target rather than added as a subdirectory.
add_library(rvcara_hnswlib INTERFACE)
target_include_directories(rvcara_hnswlib SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libs/hnswlib")

# ==================================================================================
# Warnings
#
# JUCE ships its modules as interface sources, so the module .cpp and .c files compile as
# part of whichever target links them, and target-level flags reach JUCE's code too. JUCE is
# not clean under a strict set — its converters promote float to double by design and the
# vendored FLAC and Ogg are C, which rejects the C++-only flags. Hence two tiers: a
# target-wide set restricted to C++, and a stricter per-source set for our own DSP.
# ==================================================================================

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
            -Wold-style-cast)
    endforeach()
endfunction()

# ==================================================================================
# The conversion engine
#
# Built as a static library separate from the plug-in, which is what makes it testable: it
# knows nothing about ARA, plug-in formats or the message thread, and the tests link it
# directly without instantiating a plug-in.
# ==================================================================================

set(RVCARA_ENGINE_SOURCES
    src/BinaryMatrix.cpp
    src/ConversionEngine.cpp
    src/FeatureRetriever.cpp
    src/MelSpectrogram.cpp
    src/ModelManifest.cpp
    src/OnnxSession.cpp
    src/PitchEstimator.cpp
    src/SincResampler.cpp
    src/VoiceLoader.cpp
    src/VoiceModel.cpp
    src/VoiceModelLibrary.cpp
    src/ZeroPhaseFilter.cpp)

add_library(rvcara_engine STATIC ${RVCARA_ENGINE_SOURCES})
target_include_directories(rvcara_engine PUBLIC src)

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

# JUCE_STANDALONE_APPLICATION is deliberately absent: the plug-in client defines it per
# format target, and setting it here would redefine it for the standalone build.
target_compile_definitions(rvcara_engine PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

# Where this working copy keeps its exported voices, so a build finds them without an install
# step. It has to be an absolute path decided here: the plug-in binary ends up in the host's
# plug-in folder, from which the repository is not reachable by any relative path.
target_compile_definitions(rvcara_engine PRIVATE
    RVCARA_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")

# hnswlib's headers trip -Wall on unused parameters and sign comparisons in ways that are
# not ours to fix, and they are included by one translation unit.
if(NOT MSVC)
    set_property(SOURCE src/FeatureRetriever.cpp APPEND PROPERTY COMPILE_OPTIONS
        -Wno-unused-parameter -Wno-sign-compare)
endif()

# ==================================================================================
# The plug-in
#
# IS_ARA_EFFECT is what turns this from an ordinary VST3/AU into one a compatible host can
# load with timeline access. It defines JucePlugin_Enable_ARA, enables the ARA code paths in
# JUCE's VST3 and AU wrappers, and requires createARAFactory() alongside createPluginFilter().
#
# The two ARA identifiers are part of the compatibility surface. ARA_FACTORY_ID must change
# when the analysis or playback capabilities change; ARA_DOCUMENT_ARCHIVE_ID must change when
# the archive format changes incompatibly. The escaped quotes are required — JUCE passes the
# value straight into a preprocessor define, so it has to carry its own literal quotes.
#
# ARA_ANALYSIS_TYPES is left at its default of none: pitch is estimated but never exposed to
# the host as readable content, and claiming a capability the document controller does not
# implement would have hosts querying for content readers that do not exist.
# ==================================================================================

juce_add_plugin(RVCARA
    PLUGIN_MANUFACTURER_CODE Pinn
    PLUGIN_CODE Rvca
    MANUFACTURER_NAME "Pitch Innovations"
    MANUFACTURER_WEBSITE "https://github.com/vivekvjyn/RVCARA"
    COMPANY_NAME "Pitch Innovations"
    BUNDLE_ID "com.pitchinnovations.rvcara"
    PRODUCT_NAME "RVCARA"
    DESCRIPTION "Retrieval-based voice conversion as an ARA plug-in"

    FORMATS AU VST3 Standalone
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
    src/ConversionModification.cpp
    src/DocumentController.cpp
    src/InsertConverter.cpp
    src/PitchCurveView.cpp
    src/PlaybackRenderer.cpp
    src/PluginEditor.cpp
    src/PluginProcessor.cpp)

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

# The DSP and the pipeline are where a silent float-to-double promotion or a C-style cast
# actually costs something. FeatureRetriever is excluded because hnswlib's headers fail it.
rvcara_apply_strict_warnings(
    src/ConversionEngine.cpp
    src/ConversionModification.cpp
    src/DocumentController.cpp
    src/InsertConverter.cpp
    src/MelSpectrogram.cpp
    src/PitchCurveView.cpp
    src/PitchEstimator.cpp
    src/PlaybackRenderer.cpp
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    src/SincResampler.cpp
    src/ZeroPhaseFilter.cpp)

foreach(format Standalone VST3 AU)
    if(TARGET RVCARA_${format})
        rvcara_copy_onnxruntime_beside(RVCARA_${format})
    endif()
endforeach()

if(RVCARA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(libs/Catch2)
    add_subdirectory(tests)
endif()
