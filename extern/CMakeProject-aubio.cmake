set(AUBIO_DIR "aubio/aubio")

set(AUBIO_SRC "${AUBIO_DIR}/fvec.c"
              "${AUBIO_DIR}/cvec.c"
              "${AUBIO_DIR}/lvec.c"
              "${AUBIO_DIR}/fmat.c"
              "${AUBIO_DIR}/mathutils.c"
              "${AUBIO_DIR}/musicutils.c"
              "${AUBIO_DIR}/onset/onset.c"
              "${AUBIO_DIR}/onset/peakpicker.c"
              "${AUBIO_DIR}/spectral/awhitening.c"
              "${AUBIO_DIR}/spectral/fft.c"
              "${AUBIO_DIR}/spectral/ooura_fft8g.c"
              "${AUBIO_DIR}/spectral/phasevoc.c"
              "${AUBIO_DIR}/spectral/specdesc.c"
              "${AUBIO_DIR}/spectral/statistics.c"
              "${AUBIO_DIR}/temporal/biquad.c"
              "${AUBIO_DIR}/temporal/filter.c"
              "${AUBIO_DIR}/utils/hist.c"
              "${AUBIO_DIR}/utils/log.c"
              "${AUBIO_DIR}/utils/scale.c")

set(AUBIO_HPP "${AUBIO_DIR}/aubio.h"
              "${AUBIO_DIR}/aubio_priv.h"
              "${AUBIO_DIR}/config.h"
              "${AUBIO_DIR}/types.h")

source_group("" FILES ${AUBIO_SRC} ${AUBIO_HPP})

add_library("aubio" STATIC ${AUBIO_SRC} ${AUBIO_HPP})

set_property(TARGET "aubio" PROPERTY FOLDER "External Libraries")

disable_project_warnings("aubio")

target_compile_definitions("aubio" PRIVATE HAVE_CONFIG_H)

if(MSVC)
  target_compile_definitions("aubio" PRIVATE _CRT_SECURE_NO_WARNINGS)
endif(MSVC)

if(NOT WIN32)
  target_link_libraries("aubio" m)
endif()

# "aubio" resolves <aubio/aubio.h> for consumers; the inner directory resolves
# aubio's own quoted includes, such as "spectral/fft.h".
target_include_directories("aubio" PUBLIC "aubio")
target_include_directories("aubio" PRIVATE "${AUBIO_DIR}")
