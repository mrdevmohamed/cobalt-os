#ifndef COBALT_LIBC_STDIO_H
#define COBALT_LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>

int snprintf(char *restrict buffer, size_t capacity,
             const char *restrict format, ...);
int vsnprintf(char *restrict buffer, size_t capacity,
              const char *restrict format, va_list args);

#endif
