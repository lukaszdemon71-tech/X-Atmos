cmake_minimum_required(VERSION 3.18)
project(AtmoForge VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ── X-Plane SDK ────────────────────────────────────────────────────────────
# Note on versions: building needs SDK 4.4.0 because the browser window's
# declarations live there. The binary that comes out still runs on every
# X-Plane 12 — the browser entry points are resolved at run time, and an
# X-Plane that lacks them gets the native panel instead.
# Needs SDK 4.4.0 or newer. The browser window — XPLMCreateWindow_t's
# contentType, XPLMWindowSetURL and XPLMWindowAddBrowserFunction — is gated
# behind XPLM440, not XPLM410 as an earlier version of this file assumed.
# The plugin still loads on older X-Plane: it resolves those entry points at
# run time and simply does not open its window.
set(XPLANE_SDK_PATH "" CACHE PATH "X-Plane SDK root, the folder containing CHeaders/")
if(NOT XPLANE_SDK_PATH)
    message(FATAL_ERROR
        "Set XPLANE_SDK_PATH to your X-Plane SDK root, e.g.\n"
        "  cmake -B build -DXPLANE_SDK_PATH=C:/XPSDK")
endif()

find_path(XPLM_INCLUDE_DIR XPLMPlugin.h
    HINTS "${XPLANE_SDK_PATH}/CHeaders/XPLM" "${XPLANE_SDK_PATH}/SDK/CHeaders/XPLM"
    NO_DEFAULT_PATH REQUIRED)

if(NOT EXISTS "${XPLM_INCLUDE_DIR}/XPLMWeather.h")
    message(FATAL_ERROR "This SDK is too old: XPLMWeather.h is missing. Use SDK 4.4.0 or newer.")
endif()
# Check for the browser window itself rather than trusting the folder name.
file(READ "${XPLM_INCLUDE_DIR}/XPLMDisplay.h" AF_DISPLAY_HEADER)
if(NOT AF_DISPLAY_HEADER MATCHES "XPLMWindowSetURL")
    message(FATAL_ERROR
        "This SDK has no browser window API (XPLMWindowSetURL). Use SDK 4.4.0 or newer:\n"
        "  https://developer.x-plane.com/sdk/")
endif()

# ── Sources ────────────────────────────────────────────────────────────────
add_library(AtmoForge MODULE
    src/AtmoForge.cpp
    src/atmo.cpp
    src/datarefs.cpp
    src/http.cpp
    src/metar.cpp
    src/presets.cpp
    src/settings.cpp
    src/ui.cpp
    src/ui_native.cpp
    src/weather.cpp
    src/weather_parse.cpp
)

set_target_properties(AtmoForge PROPERTIES PREFIX "" SUFFIX ".xpl")
target_include_directories(AtmoForge PRIVATE src "${XPLM_INCLUDE_DIR}")

target_compile_definitions(AtmoForge PRIVATE
    XPLM440=1 XPLM430=1 XPLM420=1 XPLM411=1 XPLM410=1 XPLM400=1
    XPLM303=1 XPLM301=1 XPLM300=1 XPLM210=1 XPLM200=1
    $<$<PLATFORM_ID:Windows>:IBM=1>
    $<$<PLATFORM_ID:Linux>:LIN=1>
    $<$<PLATFORM_ID:Darwin>:APL=1>
    $<$<PLATFORM_ID:Windows>:_USE_MATH_DEFINES>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

find_package(Threads REQUIRED)
target_link_libraries(AtmoForge PRIVATE Threads::Threads)

# ════════════════════════════════════════════════════════════════════════════
#  Platform specifics
# ════════════════════════════════════════════════════════════════════════════

if(WIN32)
    find_file(XPLM_LIBRARY NAMES XPLM_64.lib
        HINTS "${XPLANE_SDK_PATH}/Libraries/Win" "${XPLANE_SDK_PATH}/SDK/Libraries/Win"
        NO_DEFAULT_PATH REQUIRED)
    # WinHTTP replaces libcurl on Windows, so there is no third-party
    # dependency to install. opengl32 is NOT needed: nothing here draws.
    # opengl32 is needed by the native panel, which draws its own widgets.
    target_link_libraries(AtmoForge PRIVATE "${XPLM_LIBRARY}" winhttp opengl32)
    if(MSVC)
        target_compile_options(AtmoForge PRIVATE /W4 /EHsc /permissive-)
    endif()

elseif(APPLE)
    find_library(XPLM_LIBRARY NAMES XPLM
        HINTS "${XPLANE_SDK_PATH}/Libraries/Mac" "${XPLANE_SDK_PATH}/SDK/Libraries/Mac"
        NO_DEFAULT_PATH REQUIRED)
    find_package(CURL REQUIRED)
    set_target_properties(AtmoForge PROPERTIES
        OSX_ARCHITECTURES "x86_64;arm64"
        MACOSX_BUNDLE FALSE)
    target_compile_options(AtmoForge PRIVATE -Wall -Wextra -fvisibility=hidden -mmacosx-version-min=11.0)
    target_link_options(AtmoForge PRIVATE -bundle -mmacosx-version-min=11.0)
    find_package(OpenGL REQUIRED)
    target_link_libraries(AtmoForge PRIVATE "${XPLM_LIBRARY}" CURL::libcurl OpenGL::GL)

else()  # Linux
    find_package(CURL REQUIRED)
    target_compile_options(AtmoForge PRIVATE -Wall -Wextra -fvisibility=hidden -fPIC)
    # No --no-undefined here. X-Plane resolves the XPLM symbols at load time,
    # so requiring them at link time fails the build outright; version 0.1
    # could not be built on Linux for exactly this reason.
    target_link_options(AtmoForge PRIVATE -Wl,-rpath,'$ORIGIN')
    find_package(OpenGL REQUIRED)
    target_link_libraries(AtmoForge PRIVATE CURL::libcurl OpenGL::GL)
endif()

# ── Lay out the plugin folder next to the binary ───────────────────────────
if(WIN32)
    set(AF_PLATFORM_DIR win_x64)
elseif(APPLE)
    set(AF_PLATFORM_DIR mac_x64)
else()
    set(AF_PLATFORM_DIR lin_x64)
endif()

set(AF_STAGE "${CMAKE_BINARY_DIR}/AtmoForge")
add_custom_command(TARGET AtmoForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AF_STAGE}/${AF_PLATFORM_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:AtmoForge>" "${AF_STAGE}/${AF_PLATFORM_DIR}/AtmoForge.xpl"
    # The UI is loaded from disk at run time, so it has to travel with the .xpl.
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui" "${AF_STAGE}/${AF_PLATFORM_DIR}/ui"
    COMMENT "Staging AtmoForge/ ready to copy into Resources/plugins")

# ── Tests ──────────────────────────────────────────────────────────────────
option(ATMOFORGE_TESTS "Build the off-sim test suite" OFF)
if(ATMOFORGE_TESTS)
    enable_testing()
    add_executable(atmoforge_tests
        tests/test_core.cpp
        src/weather_parse.cpp
        src/metar.cpp
        src/presets.cpp
        src/settings.cpp)
    target_include_directories(atmoforge_tests PRIVATE src)
    add_test(NAME core COMMAND atmoforge_tests)
endif()
