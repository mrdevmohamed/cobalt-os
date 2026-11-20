#include <stdio.h>
#include <string.h>

/* GCC/Clang provide these limits without pulling in host libc headers. */
#define FORMAT_INT_MAX __INT_MAX__

struct output {
    char *next;
    size_t room; /* Excludes the terminating NUL. */
    int length;
};

static size_t output_count(struct output *out, size_t length)
{
    size_t stored = length < out->room ? length : out->room;
    out->room -= stored;
    if (out->length >= 0) {
        if (length > (size_t)(FORMAT_INT_MAX - out->length))
            out->length = -1;
        else
            out->length += (int)length;
    }
    return stored;
}

static void output_bytes(struct output *out, const char *bytes, size_t length)
{
    size_t stored = output_count(out, length);
    if (stored != 0) {
        memcpy(out->next, bytes, stored);
        out->next += stored;
    }
}

static void output_pad(struct output *out, char byte, size_t length)
{
    size_t stored = output_count(out, length);
    if (stored != 0) {
        memset(out->next, byte, stored);
        out->next += stored;
    }
}

static void output_text(struct output *out, const char *text, size_t length,
                        size_t width)
{
    if (width > length)
        output_pad(out, ' ', width - length);
    output_bytes(out, text, length);
}

static void output_number(struct output *out, unsigned int value,
                          unsigned int base, const char *alphabet,
                          int negative, int zero_pad, size_t width)
{
    char digits[sizeof(unsigned int) * __CHAR_BIT__];
    char *end = digits + sizeof digits;
    char *start = end;

    do {
        *--start = alphabet[value % base];
        value /= base;
    } while (value != 0);

    size_t length = (size_t)(end - start);
    size_t occupied = length + (size_t)negative;
    size_t padding = width > occupied ? width - occupied : 0;
    if (!zero_pad)
        output_pad(out, ' ', padding);
    if (negative)
        output_bytes(out, "-", 1);
    if (zero_pad)
        output_pad(out, '0', padding);
    output_bytes(out, start, length);
}

/* Supported directives are %[0][width]{%,s,c,d,i,u,x,X}. Width is a
   decimal minimum field width; the zero flag is ignored for %, s, and c.
   Width on %% is supported here as an extension. Like ordinary printf, %s
   requires a pointer to a NUL-terminated string. Any unsupported directive
   (including a trailing %, another flag, '*', precision, or a length
   modifier) or width above INT_MAX stops at that directive without consuming
   its argument or emitting it. The stored prefix is NUL-terminated when
   capacity is nonzero and the return value is -1. A supported format whose
   total output exceeds INT_MAX still writes the bounded output and returns
   -1. No errno is set. */
int vsnprintf(char *restrict buffer, size_t capacity,
              const char *restrict format, va_list args)
{
    struct output out = {buffer, capacity != 0 ? capacity - 1 : 0, 0};
    const char *lower = "0123456789abcdef";
    const char *upper = "0123456789ABCDEF";
    va_list ap;
    va_copy(ap, args);

    while (*format != '\0') {
        if (*format != '%') {
            const char *start = format;
            do {
                ++format;
            } while (*format != '\0' && *format != '%');
            output_bytes(&out, start, (size_t)(format - start));
            continue;
        }

        ++format;
        int zero_pad = *format == '0';
        if (zero_pad)
            ++format;

        size_t width = 0;
        while (*format >= '0' && *format <= '9') {
            unsigned int digit = (unsigned int)(*format - '0');
            if (width > ((size_t)FORMAT_INT_MAX - digit) / 10) {
                out.length = -1;
                goto done;
            }
            width = width * 10 + digit;
            ++format;
        }

        switch (*format) {
        case '%':
            output_text(&out, "%", 1, width);
            break;
        case 's': {
            const char *text = va_arg(ap, const char *);
            output_text(&out, text, strlen(text), width);
            break;
        }
        case 'c': {
            unsigned char byte = (unsigned char)va_arg(ap, int);
            output_text(&out, (const char *)&byte, 1, width);
            break;
        }
        case 'd':
        case 'i': {
            int value = va_arg(ap, int);
            int negative = value < 0;
            unsigned int magnitude = (unsigned int)value;
            if (negative)
                magnitude = 0u - magnitude;
            output_number(&out, magnitude, 10, lower, negative, zero_pad, width);
            break;
        }
        case 'u':
            output_number(&out, va_arg(ap, unsigned int), 10, lower, 0,
                          zero_pad, width);
            break;
        case 'x':
        case 'X':
            output_number(&out, va_arg(ap, unsigned int), 16,
                          *format == 'X' ? upper : lower, 0, zero_pad, width);
            break;
        default:
            out.length = -1;
            goto done;
        }
        ++format;
    }

done:
    va_end(ap);
    if (capacity != 0)
        *out.next = '\0';
    return out.length;
}

int snprintf(char *restrict buffer, size_t capacity,
             const char *restrict format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, capacity, format, args);
    va_end(args);
    return length;
}
