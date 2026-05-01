cmake_minimum_required(VERSION 3.14)
project(smx-sdk-lite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hide symbols by default (only SMX_API symbols are exported)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# macOS Homebrew paths — always prepend these so pkg-config and find_package can locate them
if(APPLE)
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
    # Also explicitly add to search paths in case pkg-config or find_package misses them
    list(APPEND CMAKE_INCLUDE_PATH "/opt/homebrew/include" "/usr/local/include")
    list(APPEND CMAKE_LIBRARY_PATH "/opt/homebrew/lib" "/usr/local/lib")
endif()

# Find hidapi
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(HIDAPI hidapi-hidraw)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi-libusb)
    endif()
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi)
    endif()
endif()

if(NOT HIDAPI_FOUND)
    find_package(hidapi REQUIRED)
    set(HIDAPI_LIBRARIES hidapi::hidapi)
    set(HIDAPI_INCLUDE_DIRS "")
else()
    # When pkg-config finds hidapi, HIDAPI_INCLUDE_DIRS points to the hidapi subdirectory,
    # but we need the parent directory since code does #include <hidapi/hidapi.h>
    get_filename_component(HIDAPI_INCLUDE_DIRS_PARENT "${HIDAPI_INCLUDE_DIRS}" DIRECTORY)
    set(HIDAPI_INCLUDE_DIRS "${HIDAPI_INCLUDE_DIRS_PARENT}")
endif()

# Library (use -DBUILD_SHARED_LIBS=ON for shared, default is static)
add_library(smx-lite
    SMXConfigPacket.cpp
    SMXDeviceConnection.cpp
    SMX.cpp
)

target_include_directories(smx-lite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${HIDAPI_INCLUDE_DIRS})
target_link_libraries(smx-lite PUBLIC ${HIDAPI_LIBRARIES})
if(HIDAPI_LIBRARY_DIRS)
    target_link_directories(smx-lite PUBLIC ${HIDAPI_LIBRARY_DIRS})
endif()
target_compile_definitions(smx-lite PRIVATE SMX_EXPORTS)

find_package(Threads REQUIRED)
target_link_libraries(smx-lite PUBLIC Threads::Threads)

# macOS: build universal binary (Intel + Apple Silicon) if not already specified
# Note: This only works if all dependencies (hidapi, etc.) support universal builds.
# On Apple Silicon Macs with Homebrew, hidapi is typically arm64-only, so the
# universal build will fail. You can:
# - Build arm64-only:  cmake .. -DCMAKE_OSX_ARCHITECTURES=arm64
# - Install x86_64 hidapi: brew install hidapi --HEAD or use a different package manager
# - Use an Intel Mac to build the x86_64 version
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    # Try to detect if we can build universal
    # For now, only build universal if explicitly requested via CMAKE_OSX_ARCHITECTURES
    # set_target_properties(smx-lite PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")
endif()
