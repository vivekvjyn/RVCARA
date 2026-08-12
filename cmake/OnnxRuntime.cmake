# ONNX Runtime, the inference engine the conversion engine runs on.
#
# The prebuilt release archives are used rather than a submodule. Building ONNX
# Runtime from source takes tens of minutes and pulls in its own dependency tree
# for no benefit here: the CPU execution provider is the only one RVCARA uses, and
# the released binaries are what Microsoft tests. The archives are pinned by SHA256
# so a build cannot silently pick up different bits.
#
# Set RVCARA_ONNXRUNTIME_ROOT to use an installation already on the machine, which
# is also the escape hatch for platforms with no published archive.

set(RVCARA_ONNXRUNTIME_VERSION "1.23.2" CACHE STRING
    "ONNX Runtime version to download when RVCARA_ONNXRUNTIME_ROOT is unset")
set(RVCARA_ONNXRUNTIME_ROOT "" CACHE PATH
    "Existing ONNX Runtime installation; leave empty to download the pinned release")

function(_rvcara_onnxruntime_archive out_name out_hash)
    set(version "${RVCARA_ONNXRUNTIME_VERSION}")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(name "onnxruntime-linux-x64-${version}")
        set(hash "1fa4dcaef22f6f7d5cd81b28c2800414350c10116f5fdd46a2160082551c5f9b")
        set(extension "tgz")
    elseif(APPLE)
        # The universal2 archive carries both arm64 and x86_64, so one download
        # serves an Apple Silicon build, an Intel build and a universal binary.
        set(name "onnxruntime-osx-universal2-${version}")
        set(hash "49ae8e3a66ccb18d98ad3fe7f5906b6d7887df8a5edd40f49eb2b14e20885809")
        set(extension "tgz")
    elseif(WIN32)
        set(name "onnxruntime-win-x64-${version}")
        set(hash "0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd")
        set(extension "zip")
    else()
        message(FATAL_ERROR
            "No pinned ONNX Runtime archive for ${CMAKE_SYSTEM_NAME}. "
            "Build ONNX Runtime yourself and point RVCARA_ONNXRUNTIME_ROOT at it.")
    endif()

    if(NOT RVCARA_ONNXRUNTIME_VERSION STREQUAL "1.23.2")
        message(WARNING
            "RVCARA_ONNXRUNTIME_VERSION is ${RVCARA_ONNXRUNTIME_VERSION}, but the pinned "
            "checksums are for 1.23.2. The download will fail its hash check; set "
            "RVCARA_ONNXRUNTIME_ROOT instead, or update the hashes in this file.")
    endif()

    set(${out_name} "${name}.${extension}" PARENT_SCOPE)
    set(${out_hash} "${hash}" PARENT_SCOPE)
endfunction()

function(rvcara_add_onnxruntime)
    if(TARGET rvcara::onnxruntime)
        return()
    endif()

    if(RVCARA_ONNXRUNTIME_ROOT)
        set(rootDirectory "${RVCARA_ONNXRUNTIME_ROOT}")
        message(STATUS "ONNX Runtime: using ${rootDirectory}")
    else()
        _rvcara_onnxruntime_archive(archiveName archiveHash)

        include(FetchContent)
        FetchContent_Declare(onnxruntime
            URL "https://github.com/microsoft/onnxruntime/releases/download/v${RVCARA_ONNXRUNTIME_VERSION}/${archiveName}"
            URL_HASH "SHA256=${archiveHash}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

        message(STATUS "ONNX Runtime: fetching ${archiveName}")
        FetchContent_MakeAvailable(onnxruntime)
        set(rootDirectory "${onnxruntime_SOURCE_DIR}")
    endif()

    set(includeDirectory "${rootDirectory}/include")
    if(NOT EXISTS "${includeDirectory}/onnxruntime_cxx_api.h")
        message(FATAL_ERROR
            "ONNX Runtime headers not found under ${includeDirectory}. "
            "If RVCARA_ONNXRUNTIME_ROOT is set, it should be the directory holding "
            "include/ and lib/.")
    endif()

    # The runtime is a shared library on every platform. Static linking is possible
    # but needs an archive Microsoft does not publish, and the shared library is
    # what gets copied into the plugin bundle anyway.
    if(WIN32)
        set(importLibrary "${rootDirectory}/lib/onnxruntime.lib")
        set(runtimeLibrary "${rootDirectory}/lib/onnxruntime.dll")
    elseif(APPLE)
        set(importLibrary "${rootDirectory}/lib/libonnxruntime.dylib")
        set(runtimeLibrary "${importLibrary}")
    else()
        set(importLibrary "${rootDirectory}/lib/libonnxruntime.so")
        set(runtimeLibrary "${importLibrary}")
    endif()

    if(NOT EXISTS "${importLibrary}")
        message(FATAL_ERROR "ONNX Runtime library not found at ${importLibrary}")
    endif()

    add_library(rvcara_onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(rvcara_onnxruntime PROPERTIES
        IMPORTED_LOCATION "${runtimeLibrary}"
        IMPORTED_IMPLIB "${importLibrary}"
        INTERFACE_INCLUDE_DIRECTORIES "${includeDirectory}")
    add_library(rvcara::onnxruntime ALIAS rvcara_onnxruntime)

    set(RVCARA_ONNXRUNTIME_LIBRARY "${runtimeLibrary}" CACHE INTERNAL
        "Shared library to place beside built artefacts")
    set(RVCARA_ONNXRUNTIME_LIBRARY_DIR "${rootDirectory}/lib" CACHE INTERNAL
        "Directory holding the ONNX Runtime shared library and its soname links")
endfunction()

# Copy the runtime next to a built binary.
#
# A plugin cannot rely on the host's library search path, so every format target
# gets its own copy. On Linux and macOS the whole lib directory is copied because
# the loader follows the versioned soname, not the bare filename.
function(rvcara_copy_onnxruntime_beside target)
    if(NOT RVCARA_ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR "rvcara_add_onnxruntime must be called first")
    endif()

    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RVCARA_ONNXRUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                    "${RVCARA_ONNXRUNTIME_LIBRARY_DIR}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()
