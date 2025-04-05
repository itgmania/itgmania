# Build and statically link libusb project

# libusb is currently used by Linux, but we need to test it under windows too
# and it should work
if(NOT LINUX)
  return()
endif()

set(LIBUSB_SOURCE_DIR "${SM_EXTERN_DIR}/libusb")

include(ExternalProject)
ExternalProject_Add(
  libusb

  SOURCE_DIR "${LIBUSB_SOURCE_DIR}"
  INSTALL_COMMAND ""
)

ExternalProject_Get_Property(libusb SOURCE_DIR BINARY_DIR)
set(LIBUSB_INCLUDE_DIR "${SOURCE_DIR}/libusb/libusb" CACHE INTERNAL "libusb include")
set(LIBUSB_LIBRARY "${BINARY_DIR}/libusb-1.0.a" CACHE INTERNAL "libusb library")
