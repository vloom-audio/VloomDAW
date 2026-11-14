# FFmpeg (libavformat, libavcodec, libavutil, libswresample) decodes the
# sample files the engine does not read itself: MP3, FLAC, Ogg, AIFF, M4A
# (audio/FileDecoder). Provides the target vloomdaw::ffmpeg and, on Windows,
# VLOOMDAW_FFMPEG_DLLS: the DLLs to put next to every executable using it.
#
# Where it comes from, in this order:
#   1. FFMPEG_ROOT: a folder with include/, lib/ (and bin/ on Windows), e.g.
#      the prefix scripts/build-ffmpeg.sh installs into (release builds).
#   2. Windows: a pinned LGPL shared build from BtbN/FFmpeg-Builds,
#      downloaded at configure time and checked against its SHA-256.
#   3. Otherwise pkg-config: the system's FFmpeg (brew install ffmpeg,
#      apt install libavformat-dev libavcodec-dev libswresample-dev).
# FFmpeg 5.1 or later is needed (the AVChannelLayout API).

set(FFMPEG_ROOT "" CACHE PATH "FFmpeg prefix (include/, lib/, bin/); empty: see cmake/FFmpeg.cmake")

if(NOT FFMPEG_ROOT AND WIN32)
    # FFmpeg 8.1.3 (n8.1.3-35-gf1acb0a266), win64, LGPL, shared. BtbN keeps
    # dated autobuilds for a long time but not forever: when this one is gone,
    # pick a newer 8.1 build and update the URL and hash.
    set(_ffmpeg_name "ffmpeg-n8.1.3-35-gf1acb0a266-win64-lgpl-shared-8.1")
    set(_ffmpeg_url
        "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-11-13-12-43/${_ffmpeg_name}.zip")
    set(_ffmpeg_sha256 "57cb10e4703b88edf55aa49e94dee19fd12f555f82ed3493a8cd70c847699996")
    set(_ffmpeg_dir "${CMAKE_BINARY_DIR}/_deps/ffmpeg")
    set(_ffmpeg_zip "${_ffmpeg_dir}/${_ffmpeg_name}.zip")
    if(NOT EXISTS "${_ffmpeg_dir}/${_ffmpeg_name}/include/libavcodec/avcodec.h")
        message(STATUS "VloomDAW: downloading FFmpeg (${_ffmpeg_name})")
        file(DOWNLOAD "${_ffmpeg_url}" "${_ffmpeg_zip}"
            EXPECTED_HASH SHA256=${_ffmpeg_sha256} TLS_VERIFY ON SHOW_PROGRESS)
        file(ARCHIVE_EXTRACT INPUT "${_ffmpeg_zip}" DESTINATION "${_ffmpeg_dir}")
        file(REMOVE "${_ffmpeg_zip}")
    endif()
    set(FFMPEG_ROOT "${_ffmpeg_dir}/${_ffmpeg_name}")
endif()

add_library(vloomdaw_ffmpeg INTERFACE)
add_library(vloomdaw::ffmpeg ALIAS vloomdaw_ffmpeg)

if(FFMPEG_ROOT)
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h PATHS "${FFMPEG_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
    set(_ffmpeg_libs "")
    foreach(lib IN ITEMS avformat avcodec swresample avutil)
        find_library(FFMPEG_${lib}_LIBRARY NAMES ${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
        list(APPEND _ffmpeg_libs "${FFMPEG_${lib}_LIBRARY}")
    endforeach()
    target_include_directories(vloomdaw_ffmpeg SYSTEM INTERFACE "${FFMPEG_INCLUDE_DIR}")
    target_link_libraries(vloomdaw_ffmpeg INTERFACE ${_ffmpeg_libs})
    if(EXISTS "${FFMPEG_ROOT}/lib/pkgconfig/libavcodec.pc" AND NOT WIN32)
        # A static build (scripts/build-ffmpeg.sh) also needs what FFmpeg links.
        find_package(PkgConfig REQUIRED)
        set(ENV{PKG_CONFIG_PATH} "${FFMPEG_ROOT}/lib/pkgconfig")
        pkg_check_modules(FFMPEG_STATIC REQUIRED libavformat libavcodec libswresample libavutil)
        # pkg-config splits "-framework CoreVideo" into two items, and CMake
        # drops the repeated "-framework" ones, leaving "-lCoreVideo": keep
        # each pair as one flag.
        set(_ffmpeg_static "")
        set(_framework OFF)
        foreach(_flag IN LISTS FFMPEG_STATIC_STATIC_LDFLAGS)
            if(_framework)
                list(APPEND _ffmpeg_static "-framework ${_flag}")
                set(_framework OFF)
            elseif(_flag STREQUAL "-framework")
                set(_framework ON)
            else()
                list(APPEND _ffmpeg_static "${_flag}")
            endif()
        endforeach()
        target_link_libraries(vloomdaw_ffmpeg INTERFACE ${_ffmpeg_static})
    endif()
    if(WIN32)
        file(GLOB VLOOMDAW_FFMPEG_DLLS
            "${FFMPEG_ROOT}/bin/avformat-*.dll" "${FFMPEG_ROOT}/bin/avcodec-*.dll"
            "${FFMPEG_ROOT}/bin/avutil-*.dll" "${FFMPEG_ROOT}/bin/swresample-*.dll")
    endif()
    message(STATUS "VloomDAW: FFmpeg from ${FFMPEG_ROOT}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat>=59.27.100 libavcodec>=59.37.100 libswresample>=4.7.100 libavutil>=57.28.100)
    target_link_libraries(vloomdaw_ffmpeg INTERFACE PkgConfig::FFMPEG)
    message(STATUS "VloomDAW: FFmpeg ${FFMPEG_libavcodec_VERSION} (libavcodec) from pkg-config")
endif()

# Copies the FFmpeg DLLs next to `target` after each build (Windows), so it
# runs from the build folder.
function(vloomdaw_copy_ffmpeg_dlls target)
    if(WIN32 AND VLOOMDAW_FFMPEG_DLLS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VLOOMDAW_FFMPEG_DLLS} $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endif()
endfunction()
