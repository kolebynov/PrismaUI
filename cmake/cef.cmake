# CEF dependency setup for PrismaUI.
# This module intentionally makes CEF available to the build without changing
# PrismaUI's plugin runtime startup path.

set(PRISMAUI_CEF_VERSION "156.0.15+gcf671c0+chromium-156.0.8078.80")

set(PRISMAUI_CEF_ARCHIVE
    ""
    CACHE FILEPATH "Path to a local CEF binary distribution archive; empty uses the pinned PRISMAUI_CEF_VERSION")

if(PRISMAUI_CEF_ARCHIVE)
    set(_PRISMAUI_CEF_ARCHIVE "${PRISMAUI_CEF_ARCHIVE}")
else()
    set(_PRISMAUI_CEF_ARCHIVE "${CMAKE_SOURCE_DIR}/external/cef_binary_${PRISMAUI_CEF_VERSION}_windows64.tar.bz2")
endif()

if(DEFINED BUILD_ROOT)
    set(_PRISMAUI_CEF_DEFAULT_EXTRACT_DIR "${BUILD_ROOT}/external_builds/cef")
else()
    set(_PRISMAUI_CEF_DEFAULT_EXTRACT_DIR "${CMAKE_BINARY_DIR}/external_builds/cef")
endif()

set(PRISMAUI_CEF_EXTRACT_DIR
    "${_PRISMAUI_CEF_DEFAULT_EXTRACT_DIR}"
    CACHE PATH "Directory used for the extracted CEF binary distribution")

get_filename_component(_PRISMAUI_CEF_ARCHIVE "${_PRISMAUI_CEF_ARCHIVE}" ABSOLUTE)
get_filename_component(PRISMAUI_CEF_EXTRACT_DIR "${PRISMAUI_CEF_EXTRACT_DIR}" ABSOLUTE)

if(NOT EXISTS "${_PRISMAUI_CEF_ARCHIVE}")
    get_filename_component(_PRISMAUI_CEF_ARCHIVE_NAME "${_PRISMAUI_CEF_ARCHIVE}" NAME)
    # CEF build filenames contain '+' which must be percent-encoded in the download URL.
    string(REPLACE "+" "%2B" _PRISMAUI_CEF_ARCHIVE_URL_NAME "${_PRISMAUI_CEF_ARCHIVE_NAME}")
    set(_PRISMAUI_CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${_PRISMAUI_CEF_ARCHIVE_URL_NAME}")

    message(STATUS "CEF archive not found, downloading: ${_PRISMAUI_CEF_DOWNLOAD_URL}")
    get_filename_component(_PRISMAUI_CEF_ARCHIVE_DIR "${_PRISMAUI_CEF_ARCHIVE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_PRISMAUI_CEF_ARCHIVE_DIR}")

    set(_PRISMAUI_CEF_ARCHIVE_PART "${_PRISMAUI_CEF_ARCHIVE}.part")
    file(DOWNLOAD
        "${_PRISMAUI_CEF_DOWNLOAD_URL}"
        "${_PRISMAUI_CEF_ARCHIVE_PART}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        STATUS _PRISMAUI_CEF_DOWNLOAD_STATUS)
    list(GET _PRISMAUI_CEF_DOWNLOAD_STATUS 0 _PRISMAUI_CEF_DOWNLOAD_CODE)
    list(GET _PRISMAUI_CEF_DOWNLOAD_STATUS 1 _PRISMAUI_CEF_DOWNLOAD_MESSAGE)

    if(NOT _PRISMAUI_CEF_DOWNLOAD_CODE EQUAL 0)
        file(REMOVE "${_PRISMAUI_CEF_ARCHIVE_PART}")
        message(FATAL_ERROR
            "CEF binary archive not found and download failed.\n"
            "URL: ${_PRISMAUI_CEF_DOWNLOAD_URL}\n"
            "Error: ${_PRISMAUI_CEF_DOWNLOAD_MESSAGE}\n"
            "Place ${_PRISMAUI_CEF_ARCHIVE_NAME} in the external directory "
            "or configure PRISMAUI_CEF_ARCHIVE to a local CEF binary archive.")
    endif()

    file(RENAME "${_PRISMAUI_CEF_ARCHIVE_PART}" "${_PRISMAUI_CEF_ARCHIVE}")
    message(STATUS "CEF archive downloaded: ${_PRISMAUI_CEF_ARCHIVE}")
endif()

# A CEF archive holds a single top-level directory named after the archive, so the extracted root is
# looked up by that exact name: other CEF versions extracted next to it are never picked up.
get_filename_component(_PRISMAUI_CEF_ARCHIVE_NAME "${_PRISMAUI_CEF_ARCHIVE}" NAME)
string(REGEX REPLACE "\\.tar\\.bz2$" "" _PRISMAUI_CEF_ROOT_NAME "${_PRISMAUI_CEF_ARCHIVE_NAME}")
set(_PRISMAUI_CEF_ROOT "${PRISMAUI_CEF_EXTRACT_DIR}/${_PRISMAUI_CEF_ROOT_NAME}")
set(_PRISMAUI_CEF_ROOT_MARKERS
    "${_PRISMAUI_CEF_ROOT}/cmake/FindCEF.cmake"
    "${_PRISMAUI_CEF_ROOT}/include/cef_app.h"
    "${_PRISMAUI_CEF_ROOT}/libcef_dll/CMakeLists.txt")

function(_prismaui_cef_root_extracted out_var)
    set(_result TRUE)
    foreach(_marker IN LISTS _PRISMAUI_CEF_ROOT_MARKERS)
        if(NOT EXISTS "${_marker}")
            set(_result FALSE)
        endif()
    endforeach()
    set(${out_var} ${_result} PARENT_SCOPE)
endfunction()

_prismaui_cef_root_extracted(_PRISMAUI_CEF_ROOT_FOUND)
if(NOT _PRISMAUI_CEF_ROOT_FOUND)
    message(STATUS "Extracting CEF archive: ${_PRISMAUI_CEF_ARCHIVE}")
    file(MAKE_DIRECTORY "${PRISMAUI_CEF_EXTRACT_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT "${_PRISMAUI_CEF_ARCHIVE}"
        DESTINATION "${PRISMAUI_CEF_EXTRACT_DIR}")
    _prismaui_cef_root_extracted(_PRISMAUI_CEF_ROOT_FOUND)
endif()

if(NOT _PRISMAUI_CEF_ROOT_FOUND)
    message(FATAL_ERROR
        "CEF extraction did not produce the expected binary distribution root: ${_PRISMAUI_CEF_ROOT}\n"
        "The archive must contain a top-level directory named after the archive file "
        "(${_PRISMAUI_CEF_ROOT_NAME}) with cmake/FindCEF.cmake, include/cef_app.h, libcef_dll/CMakeLists.txt")
endif()

set(PRISMAUI_CEF_ROOT "${_PRISMAUI_CEF_ROOT}" CACHE PATH "Extracted CEF binary distribution root" FORCE)
set(CEF_ROOT "${PRISMAUI_CEF_ROOT}")

set(_PRISMAUI_CEF_REQUIRED_PATHS
    "${CEF_ROOT}/cmake/FindCEF.cmake"
    "${CEF_ROOT}/include/cef_app.h"
    "${CEF_ROOT}/libcef_dll/CMakeLists.txt"
    "${CEF_ROOT}/Debug/libcef.lib"
    "${CEF_ROOT}/Release/libcef.lib"
    "${CEF_ROOT}/Resources/locales")

foreach(_required_path IN LISTS _PRISMAUI_CEF_REQUIRED_PATHS)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "CEF package validation failed; missing required path: ${_required_path}")
    endif()
endforeach()

message(STATUS "PrismaUI CEF archive: ${_PRISMAUI_CEF_ARCHIVE}")
message(STATUS "PrismaUI CEF root: ${CEF_ROOT}")

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")

# PrismaUI will run CEF unsandboxed when the runtime migration reaches initialization.
# Keep this explicit so the subprocess target does not require bootstrap/sandbox wiring.
set(USE_SANDBOX OFF CACHE BOOL "Disable CEF sandbox for PrismaUI" FORCE)

# The upstream CEF CMake files clear global CMAKE_CXX_FLAGS for Ninja builds.
# Preserve PrismaUI's existing compiler flag configuration and apply CEF flags only
# through CEF target macros/functions below.
set(_PRISMAUI_CEF_SAVED_FLAG_VARS
    CMAKE_C_FLAGS
    CMAKE_C_FLAGS_DEBUG
    CMAKE_C_FLAGS_RELEASE
    CMAKE_CXX_FLAGS
    CMAKE_CXX_FLAGS_DEBUG
    CMAKE_CXX_FLAGS_RELEASE)

foreach(_flag_var IN LISTS _PRISMAUI_CEF_SAVED_FLAG_VARS)
    set(_PRISMAUI_CEF_SAVED_${_flag_var} "${${_flag_var}}")
endforeach()

find_package(CEF REQUIRED)

foreach(_flag_var IN LISTS _PRISMAUI_CEF_SAVED_FLAG_VARS)
    set(${_flag_var} "${_PRISMAUI_CEF_SAVED_${_flag_var}}")
endforeach()

if(NOT TARGET libcef_dll_wrapper)
    set(_PRISMAUI_CEF_SAVED_CMAKE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
    set(CMAKE_CXX_STANDARD 20)
    add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/external_builds/libcef_dll_wrapper")
    set(CMAKE_CXX_STANDARD "${_PRISMAUI_CEF_SAVED_CMAKE_CXX_STANDARD}")
endif()

if(NOT TARGET libcef_lib)
    ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
endif()

message(STATUS "PrismaUI CEF binary dir: ${CEF_BINARY_DIR}")
message(STATUS "PrismaUI CEF resource dir: ${CEF_RESOURCE_DIR}")

function(prismaui_configure_cef_executable target_name)
    SET_EXECUTABLE_TARGET_PROPERTIES(${target_name})
    target_include_directories(${target_name}
        PRIVATE
        "${CEF_ROOT}"
        "${CEF_ROOT}/include")
    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()

function(_prismaui_copy_cef_single_file target_name source_file target_file)
    string(FIND "${source_file}" "$<CONFIGURATION>" _configuration_pos)
    if(NOT _configuration_pos EQUAL -1)
        string(REPLACE "$<CONFIGURATION>" "Release" _existing_source_file "${source_file}")
        if(NOT EXISTS "${_existing_source_file}")
            string(REPLACE "$<CONFIGURATION>" "Debug" _existing_source_file "${source_file}")
        endif()
    else()
        set(_existing_source_file "${source_file}")
    endif()

    get_filename_component(_target_parent "${target_file}" DIRECTORY)

    if(IS_DIRECTORY "${_existing_source_file}")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${target_file}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${source_file}" "${target_file}"
            VERBATIM)
    else()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_target_parent}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source_file}" "${target_file}"
            VERBATIM)
    endif()
endfunction()

function(prismaui_copy_cef_runtime target_name target_dir)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${target_dir}"
        VERBATIM)

    foreach(_cef_binary_file IN LISTS CEF_BINARY_FILES)
        get_filename_component(_cef_binary_name "${_cef_binary_file}" NAME)
        _prismaui_copy_cef_single_file(
            ${target_name}
            "${CEF_BINARY_DIR}/${_cef_binary_file}"
            "${target_dir}/${_cef_binary_name}")
    endforeach()

    foreach(_cef_resource_file IN LISTS CEF_RESOURCE_FILES)
        get_filename_component(_cef_resource_name "${_cef_resource_file}" NAME)
        _prismaui_copy_cef_single_file(
            ${target_name}
            "${CEF_RESOURCE_DIR}/${_cef_resource_file}"
            "${target_dir}/${_cef_resource_name}")
    endforeach()
endfunction()
