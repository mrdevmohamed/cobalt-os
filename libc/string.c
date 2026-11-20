#include <stdint.h>
#include <string.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    uintptr_t d_addr = (uintptr_t)d;
    uintptr_t s_addr = (uintptr_t)s;

    if (d == s)
        return dest;

    /* On the flat address space, only an overlap to the right needs a
       backward copy. Subtract after ordering to avoid address wraparound. */
    if (d_addr > s_addr && d_addr - s_addr < n) {
        while (n != 0) {
            --n;
            d[n] = s[n];
        }
    } else {
        for (size_t i = 0; i < n; ++i)
            d[i] = s[i];
    }
    return dest;
}

void *memset(void *dest, int c, size_t n)
{
    unsigned char *d = dest;

    for (size_t i = 0; i < n; ++i)
        d[i] = (unsigned char)c;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a;
    const unsigned char *q = b;

    for (size_t i = 0; i < n; ++i) {
        if (p[i] != q[i])
            return p[i] < q[i] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t length = 0;

    while (s[length] != '\0')
        ++length;
    return length;
}

int strcmp(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (*p == *q) {
        if (*p == '\0')
            return 0;
        ++p;
        ++q;
    }
    return *p < *q ? -1 : 1;
}

int strncmp(const char *a, const char *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    for (size_t i = 0; i < n; ++i) {
        if (p[i] != q[i])
            return p[i] < q[i] ? -1 : 1;
        if (p[i] == '\0')
            break;
    }
    return 0;
}

char *strcpy(char *restrict dest, const char *restrict src)
{
    char *d = dest;

    while ((*d++ = *src++) != '\0')
        ;
    return dest;
}

char *strchr(const char *s, int c)
{
    const char needle = (char)c;

    while (*s != needle) {
        if (*s == '\0')
            return NULL;
        ++s;
    }
    return (char *)s;
}
