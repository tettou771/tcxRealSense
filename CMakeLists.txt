# =============================================================================
# tcxRealSense - Intel RealSense backend for tcxDepthCamera
# =============================================================================
# Windows SDK installers ship no CMake config. Locate their include/import lib/
# runtime directly. Linux packages provide realsense2Config.cmake; a manual
# include/library search covers installations without it (not hardware-verified).

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxRealSense)

file(GLOB ADDON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB ADDON_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

# Anchor the base addon at TrussC, including when this addon is symlinked in.
if(DEFINED TRUSSC_DIR)
    set(_TC_DEPTHCAMERA_SRC "${TRUSSC_DIR}/../addons/tcxDepthCamera/src")
else()
    set(_TC_DEPTHCAMERA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../tcxDepthCamera/src")
endif()
target_include_directories(${ADDON_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${_TC_DEPTHCAMERA_SRC}")
target_link_libraries(${ADDON_NAME} PUBLIC TrussC)

# -DREALSENSE_SDK_DIR wins; the environment variable of the same name (CI,
# shells) is the fallback.
if(NOT REALSENSE_SDK_DIR AND DEFINED ENV{REALSENSE_SDK_DIR})
    set(REALSENSE_SDK_DIR "$ENV{REALSENSE_SDK_DIR}")
endif()
set(REALSENSE_SDK_DIR "${REALSENSE_SDK_DIR}" CACHE PATH "Intel RealSense SDK 2.0 root")
set(_TCRS_TARGET "")
set(_TCRS_DOWNLOAD "https://github.com/realsenseai/librealsense/releases")

if(WIN32)
    # An explicit SDK root wins, even if incomplete: report the error rather
    # than silently selecting a different installation.
    # Environment and command-line paths may use backslashes; normalize them.
    if(REALSENSE_SDK_DIR)
        file(TO_CMAKE_PATH "${REALSENSE_SDK_DIR}" _tcrs_explicit)
        set(_tcrs_roots "${_tcrs_explicit}")
    else()
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _tcrs_home)
        set(_tcrs_roots
            "${_tcrs_home}/Documents/RealSense SDK 2.0"
            "C:/Program Files (x86)/Intel RealSense SDK 2.0")
    endif()
    foreach(_root IN LISTS _tcrs_roots)
        if(EXISTS "${_root}/include/librealsense2/rs.hpp" AND
           EXISTS "${_root}/lib/x64/realsense2.lib" AND
           EXISTS "${_root}/bin/x64/realsense2.dll")
            add_library(tcxRealSense_realsense2 SHARED IMPORTED)
            set_target_properties(tcxRealSense_realsense2 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_root}/include"
                IMPORTED_IMPLIB "${_root}/lib/x64/realsense2.lib"
                IMPORTED_LOCATION "${_root}/bin/x64/realsense2.dll")
            set(_TCRS_TARGET tcxRealSense_realsense2)
            message(STATUS "tcxRealSense: found RealSense SDK at ${_root}")
            break()
        endif()
    endforeach()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(realsense2 QUIET CONFIG)
    if(TARGET realsense2::realsense2)
        set(_TCRS_TARGET realsense2::realsense2)
    else()
        find_path(REALSENSE_INCLUDE_DIR librealsense2/rs.hpp
            HINTS "${REALSENSE_SDK_DIR}/include")
        find_library(REALSENSE_LIBRARY NAMES realsense2
            HINTS "${REALSENSE_SDK_DIR}/lib" "${REALSENSE_SDK_DIR}/lib64")
        if(REALSENSE_INCLUDE_DIR AND REALSENSE_LIBRARY)
            add_library(tcxRealSense_realsense2 SHARED IMPORTED)
            set_target_properties(tcxRealSense_realsense2 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${REALSENSE_INCLUDE_DIR}"
                IMPORTED_LOCATION "${REALSENSE_LIBRARY}")
            set(_TCRS_TARGET tcxRealSense_realsense2)
        endif()
    endif()
else()
    message(FATAL_ERROR "tcxRealSense: v1 targets Windows; Linux wiring is experimental. macOS is out of scope.")
endif()

if(NOT _TCRS_TARGET)
    message(FATAL_ERROR
        "tcxRealSense: Intel RealSense SDK 2.0 was not found.\n"
        "  Download it from ${_TCRS_DOWNLOAD}\n"
        "  Windows: install the x64 SDK or pass -DREALSENSE_SDK_DIR=\"<sdk>\".\n"
        "  Required: include/librealsense2/rs.hpp, lib/x64/realsense2.lib, bin/x64/realsense2.dll.\n"
        "  Linux: install librealsense2-dev, set realsense2_DIR to its CMake config,\n"
        "  or pass REALSENSE_SDK_DIR for the manual include/lib search.")
endif()

target_link_libraries(${ADDON_NAME} PUBLIC ${_TCRS_TARGET})
message(STATUS "tcxRealSense: using RealSense SDK target ${_TCRS_TARGET}")

# Co-locate the runtime with any app consuming the addon. The Windows imported
# target separates its link-time .lib from the runtime .dll, just like a Linux
# package target's IMPORTED_LOCATION points at its .so.
if(COMMAND tc_addon_bundle_file)
    get_target_property(_TCRS_RUNTIME ${_TCRS_TARGET} IMPORTED_LOCATION)
    if(NOT _TCRS_RUNTIME)
        get_target_property(_tcrs_configs ${_TCRS_TARGET} IMPORTED_CONFIGURATIONS)
        foreach(_config RELEASE RELWITHDEBINFO NOCONFIG ${_tcrs_configs})
            string(TOUPPER "${_config}" _config)
            get_target_property(_TCRS_RUNTIME ${_TCRS_TARGET} "IMPORTED_LOCATION_${_config}")
            if(_TCRS_RUNTIME)
                break()
            endif()
        endforeach()
    endif()
    if(_TCRS_RUNTIME AND EXISTS "${_TCRS_RUNTIME}")
        tc_addon_bundle_file("${_TCRS_RUNTIME}")
        message(STATUS "tcxRealSense: will bundle runtime ${_TCRS_RUNTIME}")
    endif()
endif()
