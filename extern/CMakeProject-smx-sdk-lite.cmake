
# CMakeProject-smx-sdk-lite.cmake
# Defines the smx-sdk-lite static library for use in the main project

set(SMX_SDK_LITE_SRC
    "smx-sdk-lite/SMX.cpp"
    "smx-sdk-lite/SMXConfigPacket.cpp"
    "smx-sdk-lite/SMXDeviceConnection.cpp"
)

set(SMX_SDK_LITE_HPP
    "smx-sdk-lite/SMX.h"
    "smx-sdk-lite/SMXConfigPacket.h"
    "smx-sdk-lite/SMXDeviceConnection.h"
)

source_group("" FILES ${SMX_SDK_LITE_SRC} ${SMX_SDK_LITE_HPP})

# --- Begin hidapi detection logic (ported from smx-sdk-lite/CMakeLists.txt) ---
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
    # Fallback to vendored hidapi target
    set(HIDAPI_LIBRARIES hidapi)
    set(HIDAPI_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/hidapi/hidapi")
else()
    # When pkg-config finds hidapi, HIDAPI_INCLUDE_DIRS points to the hidapi subdirectory,
    # but we need the parent directory since code does #include <hidapi/hidapi.h>
    get_filename_component(HIDAPI_INCLUDE_DIRS_PARENT "${HIDAPI_INCLUDE_DIRS}" DIRECTORY)
    set(HIDAPI_INCLUDE_DIRS "${HIDAPI_INCLUDE_DIRS_PARENT}")
    set(HIDAPI_LIBRARIES ${HIDAPI_LIBRARIES})
endif()
# --- End hidapi detection logic ---

add_library("smx-sdk-lite" STATIC ${SMX_SDK_LITE_SRC} ${SMX_SDK_LITE_HPP})

set_property(TARGET "smx-sdk-lite" PROPERTY FOLDER "External Libraries")

# Set C++ standard and position independent code
set_target_properties("smx-sdk-lite" PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories("smx-sdk-lite" PUBLIC
    "smx-sdk-lite"
    ${HIDAPI_INCLUDE_DIRS}
)
target_link_libraries("smx-sdk-lite" PUBLIC ${HIDAPI_LIBRARIES} Threads::Threads)
target_compile_definitions("smx-sdk-lite" PRIVATE SMX_EXPORTS)

# Debug: print include directories
get_target_property(SMX_SDK_LITE_INCLUDES smx-sdk-lite INCLUDE_DIRECTORIES)
message(STATUS "smx-sdk-lite include dirs: ${SMX_SDK_LITE_INCLUDES}")








