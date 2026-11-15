# vloomdaw-stems: the stem separator, a helper process of the app (stems/main.cpp).
#
# demucs.cpp (MIT) runs the Demucs models with Eigen (MPL-2.0) alone. Both are
# pinned: demucs.cpp to a commit, Eigen to the commit demucs.cpp's own
# submodule names. Only the library's sources are built, none of its
# command-line programs.
CPMAddPackage(
    NAME demucs_cpp
    GITHUB_REPOSITORY sevagh/demucs.cpp
    GIT_TAG 4edae497a114de1ccb17ac9b4a2b8fc0144714d7
    GIT_SHALLOW FALSE
    GIT_SUBMODULES "vendor/eigen"
    DOWNLOAD_ONLY YES)

# Always optimised, in debug builds too: the model does billions of
# multiplications per second of audio, and unoptimised it would take hours.
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/O2 /Ob2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-O3 -DNDEBUG")
endif()

# AVX2 makes it about twice as fast. The app does not offer stem separation
# on a processor without it (core/StemModels: stemSeparationSupported()).
option(VLOOMDAW_STEMS_AVX2 "Build the stem separator for AVX2 processors (x86-64)" ON)
set(vloomdaw_stems_arch "")
if(VLOOMDAW_STEMS_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    if(MSVC)
        set(vloomdaw_stems_arch /arch:AVX2)
    else()
        set(vloomdaw_stems_arch -mavx2 -mfma)
    endif()
endif()

file(GLOB vloomdaw_demucs_sources CONFIGURE_DEPENDS "${demucs_cpp_SOURCE_DIR}/src/*.cpp")
add_library(vloomdaw_demucs STATIC ${vloomdaw_demucs_sources})
target_include_directories(vloomdaw_demucs SYSTEM PUBLIC
    "${demucs_cpp_SOURCE_DIR}/src"
    "${demucs_cpp_SOURCE_DIR}/vendor/eigen")
target_compile_options(vloomdaw_demucs PUBLIC ${vloomdaw_stems_arch})
# Not our code: its warnings are not ours to fail on.
if(MSVC)
    target_compile_options(vloomdaw_demucs PRIVATE /w /bigobj)
    target_compile_definitions(vloomdaw_demucs PUBLIC _USE_MATH_DEFINES)
else()
    target_compile_options(vloomdaw_demucs PRIVATE -w)
endif()
set_target_properties(vloomdaw_demucs PROPERTIES
    CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(vloomdaw-stems main.cpp ../core/StemChunks.cpp)
target_include_directories(vloomdaw-stems PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(vloomdaw-stems PRIVATE vloomdaw_demucs)
set_target_properties(vloomdaw-stems PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(MSVC)
    target_compile_options(vloomdaw-stems PRIVATE /bigobj)
endif()
find_package(Threads REQUIRED)
target_link_libraries(vloomdaw-stems PRIVATE Threads::Threads)
vloom_set_warnings(vloomdaw-stems)
