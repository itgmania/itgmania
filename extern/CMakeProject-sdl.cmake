# Build and statically link libusb project

# libusb is currently used by Linux, but we need to test it under windows too
# and it should work
if(NOT LINUX)
  return()
endif()

set(SDL_SOURCE_DIR "${SM_EXTERN_DIR}/SDL")

include(ExternalProject)
ExternalProject_Add(
  SDL3

  SOURCE_DIR "${SDL_SOURCE_DIR}"
  INSTALL_COMMAND ""
)

ExternalProject_Get_Property(SDL3 SOURCE_DIR BINARY_DIR)
set(SDL_INCLUDE_DIR "${SOURCE_DIR}/include" CACHE INTERNAL "SDL3 include")
# Use SDL3::SDL3 for linking
