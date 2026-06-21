#ifndef COMPILER_H
#define COMPILER_H

#if defined(__GNUC__)
#define PRINTF(a, b) __attribute__((format(__printf__, a, b)))
#define CONST_FUNCTION __attribute__((const))
#else
#define PRINTF(a, b)
#define CONST_FUNCTION
#endif

#endif
