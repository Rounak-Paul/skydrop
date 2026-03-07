cmake_minimum_required(VERSION 3.20)

# Enable Objective-C/C++ on Apple before project() call
if(APPLE)
    project(skydrop LANGUAGES C CXX OBJC OBJCXX)
else()
    project(skydrop LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------
# OpenAL-soft (static)
# ----------------------------------------------------------
set(LIBTYPE STATIC CACHE STRING "OpenAL library type" FORCE)
set(ALSOFT_UTILS           OFF CACHE BOOL "" FORCE)
set(ALSOFT_NO_CONFIG_UTIL  ON  CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS           OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL         OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG          OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA       OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS  OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS           OFF CACHE BOOL "" FORCE)

add_subdirectory(src/vendors/openal-soft EXCLUDE_FROM_ALL)

# Suppress warnings from openal-soft that are treated as errors by newer Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(OpenAL PRIVATE -Wno-function-effects -Wno-deprecated-declarations -Wno-deprecated-anon-enum-enum-conversion)
endif()

# ----------------------------------------------------------
# TinyVK (static – its own CMakeLists handles Vulkan + ImGui)
# ----------------------------------------------------------
add_subdirectory(src/vendors/tinyvk EXCLUDE_FROM_ALL)

# ----------------------------------------------------------
# Skydrop executable
# ----------------------------------------------------------
add_executable(skydrop
    src/skydrop/main.cc
    src/skydrop/application.cc
)

target_include_directories(skydrop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/skydrop
    # openal-soft headers (also pulled in from OpenAL::OpenAL interface,
    # but explicit here for clarity when AL_LIBTYPE_STATIC is needed)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendors/openal-soft/include
)

# Required when linking OpenAL as a static library
target_compile_definitions(skydrop PRIVATE AL_LIBTYPE_STATIC)

target_link_libraries(skydrop PRIVATE
    tinyvk
    OpenAL::OpenAL
)

set_target_properties(skydrop PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
