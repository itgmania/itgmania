set(LIBSAMPLERATE_DIR "libsamplerate")

# Attempt to detect version string
file(STRINGS "${LIBSAMPLERATE_DIR}/CMakeLists.txt" LIBSAMPLERATE_PROJECT_LINE
     REGEX "^[ \t]*project[ \t]*\\([ \t]*libsamplerate[ \t]+VERSION[ \t]+[0-9]+(\\.[0-9]+)*"
     LIMIT_COUNT 1)
string(REGEX MATCH "VERSION[ \t]+([0-9]+(\\.[0-9]+)*)" _ "${LIBSAMPLERATE_PROJECT_LINE}")
set(LIBSAMPLERATE_VERSION "${CMAKE_MATCH_1}")
if(NOT LIBSAMPLERATE_VERSION)
  message(WARNING "Could not read the libsamplerate version from ${LIBSAMPLERATE_DIR}/CMakeLists.txt")
  set(LIBSAMPLERATE_VERSION "0.2.2")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${LIBSAMPLERATE_DIR}/CMakeLists.txt")

set(LIBSAMPLERATE_SRC "${LIBSAMPLERATE_DIR}/src/samplerate.c"
                      "${LIBSAMPLERATE_DIR}/src/src_linear.c"
                      "${LIBSAMPLERATE_DIR}/src/src_sinc.c"
                      "${LIBSAMPLERATE_DIR}/src/src_zoh.c")

set(LIBSAMPLERATE_HPP "${LIBSAMPLERATE_DIR}/include/samplerate.h"
                      "${LIBSAMPLERATE_DIR}/src/common.h"
                      "${LIBSAMPLERATE_DIR}/src/fastest_coeffs.h"
                      "${LIBSAMPLERATE_DIR}/src/high_qual_coeffs.h"
                      "${LIBSAMPLERATE_DIR}/src/mid_qual_coeffs.h")

source_group("" FILES ${LIBSAMPLERATE_SRC} ${LIBSAMPLERATE_HPP})

add_library("libsamplerate" STATIC ${LIBSAMPLERATE_SRC} ${LIBSAMPLERATE_HPP})

set_property(TARGET "libsamplerate" PROPERTY FOLDER "External Libraries")

disable_project_warnings("libsamplerate")

# ITGmania defaults to using the fast sinc converter SRC_SINC_FASTEST.
# We don't build the others here to keep the binary size down.
# So, if you want to build with a higher quality resampler, be sure to
# enable it here as well as RageSoundReader_Resample_Good.cpp.
# See extern/libsamplerate/src/CMakeLists.txt for those options.
target_compile_definitions("libsamplerate" PRIVATE PACKAGE="libsamplerate"
                                                   VERSION="${LIBSAMPLERATE_VERSION}"
                                                   HAVE_STDBOOL_H
                                                   ENABLE_SINC_FAST_CONVERTER
                                                   CPU_CLIPS_POSITIVE=0
                                                   CPU_CLIPS_NEGATIVE=0)

if(MSVC)
  target_compile_definitions("libsamplerate" PRIVATE _CRT_SECURE_NO_WARNINGS)
endif(MSVC)

if(NOT WIN32)
  target_link_libraries("libsamplerate" m)
endif()

target_include_directories("libsamplerate" PUBLIC "${LIBSAMPLERATE_DIR}/include")
