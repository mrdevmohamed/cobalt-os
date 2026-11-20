#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_memory(void)
{
    const unsigned char source[] = {0x00, 0xff, 0x80, 0x01};
    unsigned char dest[5];
    assert(memset(dest, 0x1a5, sizeof dest) == dest);
    for (size_t i = 0; i < sizeof dest; ++i)
        assert(dest[i] == 0xa5);
    assert(memcpy(dest, source, sizeof source) == dest);
    assert(memcmp(dest, source, sizeof source) == 0);
    assert(dest[4] == 0xa5);
    assert(memcmp(source + 1, source + 2, 1) > 0);
    assert(memcmp(source + 2, source + 1, 1) < 0);
    assert(memcmp(source, dest + 1, 0) == 0);

    char right[] = "abcdef";
    assert(memmove(right + 2, right, 4) == right + 2);
    assert(memcmp(right, "ababcd", sizeof right) == 0);

    char left[] = "abcdef";
    assert(memmove(left, left + 2, 4) == left);
    assert(memcmp(left, "cdefef", sizeof left) == 0);
    assert(memmove(left, left, sizeof left) == left);
    assert(memmove(left, left + 2, 0) == left);
    assert(memcmp(left, "cdefef", sizeof left) == 0);

    assert(memmove(dest, source, sizeof source) == dest);
    assert(memcmp(dest, source, sizeof source) == 0);
}

static void test_strings(void)
{
    char copy[] = "!!!!!";
    assert(strcpy(copy, "aba") == copy);
    assert(strlen(copy) == 3);
    assert(strlen("") == 0);
    assert(copy[3] == '\0' && copy[4] == '!');
    assert(strchr(copy, 'a') == copy);
    assert(strchr(copy, 'b') == copy + 1);
    assert(strchr(copy, '\0') == copy + 3);
    assert(strchr(copy, '!') == NULL);

    assert(strcmp(copy, "aba") == 0);
    assert(strcmp("ab", "aba") < 0);
    assert(strcmp("aba", "ab") > 0);
    assert(strcmp("\xff", "\x7f") > 0);
    assert(strncmp("abx", "aby", 2) == 0);
    assert(strncmp("abx", "aby", 3) < 0);
    assert(strncmp("x", "y", 0) == 0);
    assert(strncmp("a\0x", "a\0y", 3) == 0);
    assert(strncmp("\xff", "\x7f", 1) > 0);
}

static void test_utilities(void)
{
    assert(abs(-37) == 37 && abs(0) == 0 && abs(37) == 37);
    assert(abs(-INT_MAX) == INT_MAX);

    int initial = rand();
    srand(1);
    assert(rand() == initial);

    int sequence[32];
    srand(0);
    for (size_t i = 0; i < sizeof sequence / sizeof sequence[0]; ++i) {
        sequence[i] = rand();
        assert((unsigned int)sequence[i] <= (unsigned int)RAND_MAX);
    }
    assert(sequence[0] != sequence[1]);
    srand(0);
    for (size_t i = 0; i < sizeof sequence / sizeof sequence[0]; ++i)
        assert(rand() == sequence[i]);
}

static int format_v(char *buffer, size_t capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, capacity, format, args);
    va_end(args);
    return length;
}

static void test_formatting(void)
{
    char buffer[64];
    const char expected[] = "mix:%:hi:!:17:-6:42:ab:AB";
    assert(snprintf(buffer, sizeof buffer, "mix:%%:%s:%c:%d:%i:%u:%x:%X",
                    "hi", '!', 17, -6, 42u, 0xabu, 0xabu) ==
           (int)(sizeof expected - 1));
    assert(strcmp(buffer, expected) == 0);

    const char padded[] = "-000042|   -7|0000AB|123|  ok|  !";
    assert(format_v(buffer, sizeof buffer, "%07d|%5i|%06X|%2u|%4s|%3c",
                    -42, -7, 0xabu, 123u, "ok", '!') ==
           (int)(sizeof padded - 1));
    assert(strcmp(buffer, padded) == 0);

    assert(snprintf(buffer, sizeof buffer, "%d %u", INT_MIN, UINT_MAX) == 22);
    assert(strcmp(buffer, "-2147483648 4294967295") == 0);
    assert(snprintf(buffer, sizeof buffer, "%d %u %x", 0, 0u, 0u) == 5);
    assert(strcmp(buffer, "0 0 0") == 0);

    char small[] = {'!', '!', '!', '!', '!', '!'};
    assert(snprintf(small, 4, "%s:%d", "hello", 23) == 8);
    assert(memcmp(small, "hel\0!!", sizeof small) == 0);
    assert(format_v(small, 4, "%08d", -42) == 8);
    assert(memcmp(small, "-00\0!!", sizeof small) == 0);
    assert(snprintf(small, 1, "xyz") == 3);
    assert(small[0] == '\0' && small[1] == '0');
    assert(snprintf(small, 0, "xyz") == 3 && small[1] == '0');
    assert(snprintf(NULL, 0, "%s:%05d", "hi", -42) == 8);
    assert(snprintf(NULL, 0, "%s", "") == 0);
    assert(snprintf(buffer, sizeof buffer, "%s", "") == 0 && buffer[0] == '\0');

    assert(snprintf(buffer, sizeof buffer, "a%cb", 0) == 3);
    assert(memcmp(buffer, "a\0b", 4) == 0);
    assert(format_v(buffer, sizeof buffer, "[%03s][%2%]", "a") == 9);
    assert(strcmp(buffer, "[  a][ %]") == 0);

    const char *bad_formats[] = {"ok%", "ok%f", "ok%ld", "ok%+d", "ok%.1s",
                                 "ok%*u", "ok%99999999999999999999d"};
    for (size_t i = 0; i < sizeof bad_formats / sizeof bad_formats[0]; ++i) {
        assert(format_v(buffer, sizeof buffer, bad_formats[i]) == -1);
        assert(strcmp(buffer, "ok") == 0);
    }
    assert(format_v(NULL, 0, "%2147483647u", 1u) == INT_MAX);
    assert(format_v(small, sizeof small, "%2147483647u!", 1u) == -1);
    assert(strcmp(small, "     ") == 0);
}

int main(void)
{
    test_memory();
    test_strings();
    test_utilities();
    test_formatting();
    return 0;
}
