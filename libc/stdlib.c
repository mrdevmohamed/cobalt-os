#include <stdint.h>
#include <stdlib.h>

static uint32_t rand_state = 1;

void srand(unsigned int seed)
{
    rand_state = seed;
}

int rand(void)
{
    /* A 32-bit linear congruential generator. Discard the lowest bit. */
    rand_state = rand_state * UINT32_C(1664525) + UINT32_C(1013904223);
    return (int)(rand_state >> 1);
}

int abs(int value)
{
    /* As in standard C, the behavior is undefined for INT_MIN. */
    return value < 0 ? -value : value;
}
