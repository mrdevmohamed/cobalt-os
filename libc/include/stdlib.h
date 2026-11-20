#ifndef COBALT_LIBC_STDLIB_H
#define COBALT_LIBC_STDLIB_H

#define RAND_MAX 2147483647

int rand(void);
void srand(unsigned int seed);
int abs(int value);

#endif
