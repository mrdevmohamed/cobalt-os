#ifndef COBALT_GAMES_H
#define COBALT_GAMES_H

#include <stdint.h>

/* MIT gameplay adaptations; upstream notices are in games.c and LICENSE-*. */
enum game_kind { GAME_2048, GAME_PONG };
enum game_key { GAME_UP, GAME_RIGHT, GAME_DOWN, GAME_LEFT, GAME_PAUSE };

struct game_canvas {
    void (*rect)(int x, int y, int w, int h, uint32_t color);
    void (*text)(int x, int y, const char *text, uint32_t color);
};

void games_start(enum game_kind kind, uint32_t seed, uint32_t now);
void games_key(enum game_key key, int down);
/* now is a wrapping 100 Hz counter; returns nonzero when redraw is needed. */
int games_tick(uint32_t now);
/* Draws and clears exactly the 704 x 368 client area at the given origin. */
void games_draw(int x, int y, const struct game_canvas *canvas);

#endif
