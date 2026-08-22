/*
  Minimal configuration for ITGmania's vendored aubio subset.

  Upstream aubio generates this file with waf. Only the pieces required by the
  onset detector are enabled here: no external FFT/BLAS backend is declared, so
  aubio falls back to its bundled OOURA FFT, matching the default vcpkg build
  that ArrowVortex links against.
*/

#ifndef AUBIO_ITGMANIA_CONFIG_H
#define AUBIO_ITGMANIA_CONFIG_H

#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_MATH_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDARG_H 1

/* MSVC has no support for the GNU "args..." fallback in aubio_priv.h. */
#define HAVE_C99_VARARGS_MACROS 1

#endif
