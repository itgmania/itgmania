set(INSTALL_DIR ${CMAKE_BINARY_DIR}/libjpeg-turbo-install)
set(INCLUDE_DIR ${INSTALL_DIR}/include)
set(LIB_DIR ${INSTALL_DIR}/lib)

if(WIN32)
  set(LIB_PATH ${LIB_DIR}/turbojpeg-static.lib)
else()
  set(LIB_PATH ${LIB_DIR}/libturbojpeg.a)
endif()

if(APPLE)
  LIST(APPEND ARCH_FLAGS
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
    -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
  )
endif()

include(ExternalProject)
ExternalProject_Add(
  libjpeg_turbo_project

  SOURCE_DIR ${SM_EXTERN_DIR}/libjpeg-turbo
  INSTALL_DIR ${INSTALL_DIR}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}
             -DCMAKE_INSTALL_LIBDIR=${LIB_DIR}
             -DENABLE_SHARED=OFF
             -DENABLE_STATIC=ON
             ${ARCH_FLAGS}
  BUILD_IN_SOURCE OFF
  CONFIGURE_HANDLED_BY_BUILD ON
  BUILD_BYPRODUCTS "${LIB_PATH}"  # Needed for Ninja generator. See BUILD_BYPRODUCTS docs for details.
)

# This needs to be created immediately, otherwise top-level project generation will fail.
# It will be populated by the install step of the external project.
file(MAKE_DIRECTORY ${INCLUDE_DIR})

add_library(libjpeg-turbo STATIC IMPORTED GLOBAL)
add_dependencies(libjpeg-turbo libjpeg_turbo_project)
target_include_directories(libjpeg-turbo INTERFACE "${INCLUDE_DIR}")
set_property(TARGET libjpeg-turbo PROPERTY IMPORTED_LOCATION "${LIB_PATH}")
