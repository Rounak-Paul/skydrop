cmake_minimum_required(VERSION 3.20)
project(skydrop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Vendor options -----------------------------------------------------------

# OpenAL-soft: disable extras we don't need
set(ALSOFT_UTILS    OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS    OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL  OFF CACHE BOOL "" FORCE)

add_subdirectory(src/vendors/openal-soft)
add_subdirectory(src/vendors/tinyvk)

# Fix: newer Clang on macOS triggers -Werror=function-effects in CoreAudio backend
if(APPLE)
    target_compile_options(OpenAL PRIVATE -Wno-error=function-effects)
endif()

# --- Skydrop library ---------------------------------------------------------

file(GLOB_RECURSE SKYDROP_SOURCES
    src/skydrop/*.cpp
    src/skydrop/*.h
)

add_library(skydrop_lib STATIC ${SKYDROP_SOURCES})

target_include_directories(skydrop_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendors/tinyvk/vendors/stb
)

target_link_libraries(skydrop_lib PUBLIC
    tinyvk
    OpenAL::OpenAL
)

# --- HEIC/HEIF support via libheif (cross-platform) --------------------------
# Install: macOS `brew install libheif` | Ubuntu `sudo apt install libheif-dev`
find_package(PkgConfig QUIET)
set(_HEIF_FOUND FALSE)

# Try CMake config first (modern libheif installs one)
find_package(libheif CONFIG QUIET)
if(TARGET heif)
    target_link_libraries(skydrop_lib PUBLIC heif)
    target_compile_definitions(skydrop_lib PUBLIC SKYDROP_HAS_LIBHEIF)
    set(_HEIF_FOUND TRUE)
    message(STATUS "HEIC support: enabled (libheif via CMake config)")
endif()

# Fall back to pkg-config
if(NOT _HEIF_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(LIBHEIF IMPORTED_TARGET libheif)
    if(LIBHEIF_FOUND)
        target_link_libraries(skydrop_lib PUBLIC PkgConfig::LIBHEIF)
        target_compile_definitions(skydrop_lib PUBLIC SKYDROP_HAS_LIBHEIF)
        set(_HEIF_FOUND TRUE)
        message(STATUS "HEIC support: enabled (libheif ${LIBHEIF_VERSION} via pkg-config)")
    endif()
endif()

if(NOT _HEIF_FOUND)
    message(STATUS "HEIC support: disabled (install libheif for HEIC/HEIF decoding)")
    message(STATUS "  macOS:   brew install libheif")
    message(STATUS "  Ubuntu:  sudo apt install libheif-dev")
    message(STATUS "  Fedora:  sudo dnf install libheif-devel")
endif()

# macOS: ImageIO fallback for formats libheif doesn't cover (WebP, AVIF…)
if(APPLE)
    target_link_libraries(skydrop_lib PUBLIC
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework ImageIO"
    )
endif()

target_compile_definitions(skydrop_lib PUBLIC
    SKYDROP_VERSION="${PROJECT_VERSION}"
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(skydrop_lib PUBLIC SKYDROP_DEBUG)
endif()

# --- Skydrop executable -------------------------------------------------------

add_executable(skydrop src/main.cpp)
target_link_libraries(skydrop PRIVATE skydrop_lib)
