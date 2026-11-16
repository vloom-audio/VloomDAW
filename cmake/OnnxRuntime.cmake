# ONNX Runtime with DirectML, for the stem separator's graphics-card path
# (src/stems/Onnx.cpp). Windows x64 only: DirectML runs on any DirectX 12
# graphics card, whoever made it (NVIDIA, AMD, Intel), which is why it was
# chosen over CUDA. Elsewhere the separator is built without it and the
# models that need it are not offered.
#
# Provides the target vloomdaw::onnxruntime and VLOOMDAW_ONNX_DLLS (the DLLs
# to put next to the helper) when VLOOMDAW_STEMS_ONNX ends up ON.
#
# Both come as NuGet packages (zip files), pinned by version and SHA-256:
#   Microsoft.ML.OnnxRuntime.DirectML  MIT
#   Microsoft.AI.DirectML              Microsoft's licence: free to
#       redistribute with an application, but not open source. It is a
#       system component of Windows shipped in a newer version than the one
#       Windows carries; see docs/features.md.

set(_onnx_default OFF)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(_onnx_default ON)
endif()
option(VLOOMDAW_STEMS_ONNX "Stem separator: ONNX models on the graphics card (Windows x64)" ${_onnx_default})
if(NOT VLOOMDAW_STEMS_ONNX)
    return()
endif()
if(NOT WIN32)
    message(FATAL_ERROR "VLOOMDAW_STEMS_ONNX is for Windows x64 only")
endif()

set(_ort_version 1.24.4)
set(_ort_sha256 57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681)
set(_dml_version 1.15.4)
set(_dml_sha256 4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9)
set(_onnx_dir "${CMAKE_BINARY_DIR}/_deps/onnxruntime")

function(_vloomdaw_nuget id version sha256 marker)
    string(TOLOWER "${id}" lower)
    set(dest "${_onnx_dir}/${lower}-${version}")
    if(NOT EXISTS "${dest}/${marker}")
        set(zip "${_onnx_dir}/${lower}-${version}.zip")
        message(STATUS "VloomDAW: downloading ${id} ${version}")
        file(DOWNLOAD "https://api.nuget.org/v3-flatcontainer/${lower}/${version}/${lower}.${version}.nupkg"
            "${zip}" EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON)
        file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${dest}")
        file(REMOVE "${zip}")
    endif()
    set(_nuget_dir "${dest}" PARENT_SCOPE)
endfunction()

_vloomdaw_nuget(Microsoft.ML.OnnxRuntime.DirectML ${_ort_version} ${_ort_sha256}
    runtimes/win-x64/native/onnxruntime.dll)
set(_ort "${_nuget_dir}")
_vloomdaw_nuget(Microsoft.AI.DirectML ${_dml_version} ${_dml_sha256} bin/x64-win/DirectML.dll)
set(_dml "${_nuget_dir}")

add_library(vloomdaw_onnxruntime SHARED IMPORTED GLOBAL)
add_library(vloomdaw::onnxruntime ALIAS vloomdaw_onnxruntime)
set_target_properties(vloomdaw_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_ort}/runtimes/win-x64/native/onnxruntime.dll"
    IMPORTED_IMPLIB "${_ort}/runtimes/win-x64/native/onnxruntime.lib")
target_include_directories(vloomdaw_onnxruntime SYSTEM INTERFACE "${_ort}/build/native/include")

set(VLOOMDAW_DIRECTML_INCLUDE "${_dml}/include")
set(VLOOMDAW_ONNX_DLLS
    "${_ort}/runtimes/win-x64/native/onnxruntime.dll"
    "${_ort}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
    "${_dml}/bin/x64-win/DirectML.dll")
# Their licences go along with the DLLs.
set(VLOOMDAW_ONNX_LICENSES
    "${_ort}/LICENSE" "${_ort}/ThirdPartyNotices.txt" "${_dml}/LICENSE.txt")
message(STATUS "VloomDAW: ONNX Runtime ${_ort_version} with DirectML ${_dml_version}")
