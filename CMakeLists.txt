cmake_minimum_required(VERSION 3.20)

set(NativePrecompiledHeaders 1)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
endif()
set(CMAKE_BUILD_TYPE_VALUES "Debug;Release" CACHE INTERNAL "List of supported build")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${CMAKE_BUILD_TYPE_VALUES})
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()
set(Force_CXX_23B 0 CACHE BOOL "Force 23B as the CXX Standard, useful for disrepancies in compiler and IDE")

project(cathook VERSION 0.0.1)
if (NOT Force_CXX_23B)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(cathook SHARED "")

set(GameSpecific 1 CACHE BOOL "Build for specific target game (As opposed to universal, but slower, lib)")
set(Game "tf2" CACHE STRING "Target game")
set(GameValues "tf2" CACHE INTERNAL "List of supported game types")
set_property(CACHE Game PROPERTY STRINGS ${GameValues})

set(Visuals_DrawType "IMGUI" CACHE STRING "Target drawing api")
set(DrawTypes "Textmode;No Visuals;Engine;IMGUI;IMGUI Streamproof" CACHE INTERNAL "Select the drawing api that cathook should use")
set_property(CACHE Visuals_DrawType PROPERTY STRINGS ${DrawTypes})
set(Visuals_EnforceStreamSafety 1 CACHE BOOL "If a streamsafe drawing method is used, this will remove features that are not streamsafe.")

set(EnableVisuals 1)
set(EnableGlezDrawing 0)
set(EnableEngineDrawing 0)
set(EnableImGuiDrawing 0)
set(ExternalDrawing 0)
set(Textmode 0)

if(Visuals_DrawType STREQUAL "Textmode")
    set(EnableVisuals 0)
    set(Textmode 1)
elseif(Visuals_DrawType STREQUAL "No Visuals")
    set(EnableVisuals 0)
elseif(Visuals_DrawType STREQUAL "Engine")
    set(EnableEngineDrawing 1)
elseif(Visuals_DrawType STREQUAL "IMGUI")
    set(EnableImGuiDrawing 1)
elseif(Visuals_DrawType STREQUAL "IMGUI Streamproof")
    set(EnableImGuiDrawing 1)
    set(ExternalDrawing 1)
endif()
if (NOT ExternalDrawing)
    set(Visuals_EnforceStreamSafety 0)
endif()

set(EnableProfiler 0 CACHE BOOL "Enable Profiler")
set(EnableGUI 1 CACHE BOOL "Enable GUI")
set(EnableIPC 1 CACHE BOOL "Enable IPC")
set(DataPath "/opt/cathook/data" CACHE FILEPATH "Data location")
set(VACBypass 0 CACHE BOOL "Textmode VAC bypass")
set(EnableTextmodeStdin 0 CACHE BOOL "Textmode Stdin -> Console bridge (EXPERIMENTAL)")
set(EnableWarnings 0 CACHE BOOL "Enable compile warnings")
set(EnableNullNexus 0 CACHE BOOL "Enable NullNexus chat and other features")
set(EnableLogging 1 CACHE BOOL "Enable logging to /tmp/")
set(EnableClip 1 CACHE BOOL "Enable anti electron/chromium freezing")
set(Unity_ZeroKernel 1 CACHE BOOL "Use a unity build for zerokernel files")
if(NOT EnableVisuals)
    set(EnableGUI 0)
endif()

if (NOT DEFINED Internal_Binarymode)
    set(Internal_Binarymode 0 CACHE INTERNAL "Build cathook to be used in 'binary mode'")
endif()
if (NOT DEFINED Internal_Symbolized)
    set(Internal_Symbolized 0 CACHE INTERNAL "Build cathook with symbols so crashes can be sent")
endif()

if (EnableVisuals)
    set(OpenGL_GL_PREFERENCE "LEGACY")
    if (EnableImGuiDrawing)
        find_package(Freetype QUIET)
        if (NOT FREETYPE_INCLUDE_DIRS AND EXISTS "${CMAKE_SOURCE_DIR}/external/freetype/ft2build.h")
            set(FREETYPE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/external/freetype" "${CMAKE_SOURCE_DIR}/external")
        endif()
        if (NOT FREETYPE_LIBRARIES AND EXISTS "/usr/lib/x86_64-linux-gnu/libfreetype.so.6")
            set(FREETYPE_LIBRARIES "/usr/lib/x86_64-linux-gnu/libfreetype.so.6")
        endif()
        if (NOT FREETYPE_INCLUDE_DIRS OR NOT FREETYPE_LIBRARIES)
            message(FATAL_ERROR "Freetype not found (no -dev headers and no vendored fallback)")
        endif()
        target_include_directories(cathook PRIVATE ${FREETYPE_INCLUDE_DIRS})
        target_link_libraries(cathook ${FREETYPE_LIBRARIES})
    endif()
endif()

find_package(Git)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/external")
find_package(SDL2 REQUIRED)
find_package(Boost REQUIRED)

if (Git_FOUND)
    execute_process(COMMAND
            "${GIT_EXECUTABLE}" log -1 --pretty=%h
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND
            "${GIT_EXECUTABLE}" log -1 --pretty=%ai
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_COMMITTER_DATE
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if (NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "local")
endif()
if (NOT GIT_COMMITTER_DATE)
    set(GIT_COMMITTER_DATE "unknown")
endif()

set(CMAKE_SKIP_RPATH true)

if(EnableIPC)
    add_subdirectory(external/simple-ipc)
    target_link_libraries(cathook SimpleIPC)
endif()

target_include_directories(cathook PRIVATE include/visual)
if(EnableVisuals)
    find_package(SDL2 REQUIRED)
    target_include_directories(cathook PRIVATE "${SDL2_INCLUDE_DIR}")
    target_link_libraries(cathook GL)
endif()

configure_file(include/config.h.in ${CMAKE_SOURCE_DIR}/include/config.h @ONLY)
configure_file(include/version.h.in ${CMAKE_BINARY_DIR}/include/version.h @ONLY)
target_include_directories(cathook BEFORE PRIVATE ${CMAKE_BINARY_DIR}/include)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS "-fexceptions -fno-gnu-unique -fpermissive")
set(CMAKE_CXX_FLAGS_DEBUG "-rdynamic -ggdb -Og")
if (Internal_Symbolized)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -ggdb -fvisibility=hidden -fvisibility-inlines-hidden")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -fvisibility=hidden -fvisibility-inlines-hidden")
endif()
if (Force_CXX_23B)
    set(CMAKE_CXX_FLAGS "-std=gnu++2b ${CMAKE_CXX_FLAGS}")
endif()

target_compile_definitions(cathook PRIVATE
        _POSIX=1
        FREETYPE_GL_USE_VAO=1
        RAD_TELEMETRY_DISABLED=1
        LINUX=1
        USE_SDL=1
        _LINUX=1
        POSIX=1
        GNUC=1
        NO_MALLOC_OVERRIDE=1
        PLATFORM_64BITS=1
        X64BITS=1)

target_include_directories(cathook SYSTEM PRIVATE
        "external/source-sdk-2013-headers/src/mathlib"
        "external/source-sdk-2013-headers/src/public"
        "external/source-sdk-2013-headers/src/common"
        "external/source-sdk-2013-headers/src/public/tier0"
        "external/source-sdk-2013-headers/src/public/tier1"
        "external/source-sdk-2013-headers/src/public/steam"
        "external/source-sdk-2013-headers/src/tier1"
        "external/source-sdk-2013-headers/src"
        ${Boost_INCLUDE_DIRS})

if(EnableWarnings)
    target_compile_options(cathook PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-class-memaccess -Wno-strict-aliasing -Wno-sign-compare)
else()
    target_compile_options(cathook PRIVATE -w)
endif()

target_include_directories(cathook PRIVATE include include/hooks "${CMAKE_SOURCE_DIR}/libs/funchook/include")

add_custom_target(data
        bash ./install-data "${DataPath}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VERBATIM)

set(TF2_LINUX64_DIR "/run/media/sex/tf/SteamLibrary/steamapps/common/Team Fortress 2/bin/linux64" CACHE PATH "TF2 linux64 binary directory for libtier0/libvstdlib")
if(NOT EXISTS "${TF2_LINUX64_DIR}/libtier0.so")
    foreach(_tf2_candidate
            "/run/media/sex/tf/SteamLibrary/steamapps/common/Team Fortress 2/bin/linux64"
            "/run/media/sex/Nowy/SteamLibrary/steamapps/common/Team Fortress 2/bin/linux64"
            "$ENV{HOME}/.local/share/Steam/steamapps/common/Team Fortress 2/bin/linux64"
            "$ENV{HOME}/.steam/steam/steamapps/common/Team Fortress 2/bin/linux64")
        if(EXISTS "${_tf2_candidate}/libtier0.so")
            set(TF2_LINUX64_DIR "${_tf2_candidate}" CACHE PATH "TF2 linux64 binary directory for libtier0/libvstdlib" FORCE)
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${TF2_LINUX64_DIR}/libtier0.so")
    message(FATAL_ERROR "TF2 linux64 binaries not found (libtier0.so). Set TF2_LINUX64_DIR.")
endif()
target_link_directories(cathook PRIVATE "${TF2_LINUX64_DIR}")
target_link_libraries(cathook
        "${CMAKE_SOURCE_DIR}/libs/funchook/libfunchook.a"
        "${CMAKE_SOURCE_DIR}/libs/funchook/libdistorm.a"
        tier0
        vstdlib
        atomic
        xcb
        dl
        pthread)
set_target_properties(cathook PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src)
add_subdirectory(include)
add_subdirectory(external)
add_subdirectory(modules)
target_sources(cathook PRIVATE "${CMAKE_SOURCE_DIR}/src/core/entry.cpp")

set_source_files_properties(
        src/classinfo/classinfo.cpp
        src/classinfo/dynamic.gen.cpp
        src/classinfo/dump.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/classinfo/classinfo.cpp PROPERTIES COMPILE_OPTIONS "-O0")

target_precompile_headers(cathook PRIVATE "${CMAKE_SOURCE_DIR}/include/common.hpp")

if (NOT Internal_Symbolized)
    add_custom_command(TARGET cathook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cathook> "${CMAKE_SOURCE_DIR}/bin/$<TARGET_FILE_NAME:cathook>")
else()
    message("Symbolized build")
    set(Internal_Symbolized 0 CACHE INTERNAL "Build cathook with symbols so crashes can be sent" FORCE)
endif()
