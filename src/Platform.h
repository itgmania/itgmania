#ifndef PLATFORM_H
#define PLATFORM_H

/* Ensure we have a function that acts like a size limited sprintf. */
#if defined(_WIN32)
#define snprintf _snprintf
#endif

/* Ensure we have a function that acts like a case insensitive string
 * comparison. */
#if defined(_WIN32)
#define strcasecmp _stricmp
#endif

/* Ensure we have a function that can create a directory on the file system. */
#if defined(_WIN32)
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#endif
