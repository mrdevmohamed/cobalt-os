/*
 * Bare-metal C adaptations of the gameplay in:
 * https://github.com/gabrielecirulli/2048/blob/master/js/game_manager.js
 * https://github.com/jakesgordon/javascript-pong/blob/master/pong.js
 *
 * 2048: Copyright (c) 2014 Gabriele Cirulli
 * Pong: Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016 Jake Gordon
 * and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Browser input, storage, animation, audio and assets are intentionally absent.
 * 2048 keeps playing after winning; tiles cap at 1048576, score saturates.
 * Pong uses 100 Hz Q8 integer physics, moving-paddle spin, swept collisions,
 * delayed imperfect reflected-intercept AI, and first-to-seven scoring.
 */
#include "games.h"
#include <stdio.h>
#include <string.h>

enum { GAME_WIDTH = 704, GAME_HEIGHT = 368, TILE_MAX = 1048576 };
enum { GAME_BG = 0x0b1321, GAME_INK = 0xe5edf8, GAME_MUTED = 0x93a9c5,
       GAME_ACCENT = 0x62d5c4, GAME_PANEL = 0x1c2b43 };
enum { FP = 256, COURT_LEFT = 16, COURT_RIGHT = 688,
       COURT_TOP = 68, COURT_BOTTOM = 316, PADDLE_LEFT = 28,
       PADDLE_RIGHT = 666, PADDLE_W = 10, PADDLE_H = 52, BALL_R = 4 };

static enum game_kind active_game;
static uint32_t random_state, last_tick;
static int paused, dirty, rebase_clock;
static struct {
    uint32_t cells[4][4];
    uint32_t score;
    int won, over;
} tiles;
static struct {
    int player_y, ai_y;
    int ball_x, ball_y, ball_dx, ball_dy;
    int up, down;
    int score[2], winner, serve;
    int ai_clock, ai_target;
} pong;

/* Private deterministic PRNG: starting a game does not reseed the libc user. */
static uint32_t game_random(void) {
    uint32_t x = random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    random_state = x;
    return x;
}

static int clamp(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static void tiles_spawn(void) {
    unsigned empty[16], count = 0;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            if (!tiles.cells[row][col]) empty[count++] = row * 4 + col;
    if (count) {
        /* Cirulli's addRandomTile: 90% two, 10% four, on a free cell. */
        uint32_t value = game_random() % 10 < 9 ? 2 : 4;
        unsigned cell = empty[game_random() % count];
        tiles.cells[cell / 4][cell % 4] = value;
    }
}

static int tiles_moves_available(void) {
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned col = 0; col < 4; ++col) {
            uint32_t value = tiles.cells[row][col];
            if (!value) return 1;
            if (value < TILE_MAX &&
                ((row < 3 && tiles.cells[row + 1][col] == value) ||
                 (col < 3 && tiles.cells[row][col + 1] == value))) return 1;
        }
    }
    return 0;
}

/* Traverse from the destination edge, like buildTraversals in game_manager.js.
 * Compress first, consume each equal pair once, then write the complete line.
 * This is the fixed-array equivalent of upstream's mergedFrom protection. */
static int tiles_slide(enum game_key key) {
    int changed = 0;
    for (unsigned line = 0; line < 4; ++line) {
        uint32_t input[4], output[4] = {0, 0, 0, 0};
        unsigned rows[4], cols[4], count = 0, out = 0;
        for (unsigned i = 0; i < 4; ++i) {
            rows[i] = key == GAME_LEFT || key == GAME_RIGHT ? line :
                      (key == GAME_UP ? i : 3 - i);
            cols[i] = key == GAME_UP || key == GAME_DOWN ? line :
                      (key == GAME_LEFT ? i : 3 - i);
            uint32_t value = tiles.cells[rows[i]][cols[i]];
            if (value) input[count++] = value;
        }
        for (unsigned i = 0; i < count; ++i) {
            uint32_t value = input[i];
            if (i + 1 < count && input[i + 1] == value && value < TILE_MAX) {
                value *= 2;
                ++i;
                if (UINT32_MAX - tiles.score < value) tiles.score = UINT32_MAX;
                else tiles.score += value;
                if (value == 2048) tiles.won = 1;
            }
            output[out++] = value;
        }
        for (unsigned i = 0; i < 4; ++i) {
            if (tiles.cells[rows[i]][cols[i]] != output[i]) changed = 1;
            tiles.cells[rows[i]][cols[i]] = output[i];
        }
    }
    return changed;
}

static void tiles_move(enum game_key key) {
    if (tiles.over) return;
    int changed = tiles_slide(key);
    if (changed) tiles_spawn();
    int over = !tiles_moves_available();
    if (changed || over != tiles.over) dirty = 1;
    tiles.over = over;
}

static void pong_serve(int direction) {
    pong.ball_x = (COURT_LEFT + COURT_RIGHT) * FP / 2;
    pong.ball_y = (COURT_TOP + COURT_BOTTOM) * FP / 2;
    pong.ball_dx = direction * (5 * FP / 2);
    int vertical_direction = game_random() & 1 ? 1 : -1;
    pong.ball_dy = vertical_direction * (FP + (int)(game_random() % FP));
    pong.serve = 75;
    pong.ai_clock = 0;
}

/* Gordon's Paddle.predict, reduced to a reflected intercept with bounded error
 * and 0.4 second reaction time, rather than a perfect frame-by-frame tracker. */
static void pong_predict(void) {
    int target = (COURT_TOP + COURT_BOTTOM) * FP / 2;
    if (pong.ball_dx > 0) {
        int distance = (PADDLE_RIGHT - BALL_R) * FP - pong.ball_x;
        target = pong.ball_y + pong.ball_dy * distance / pong.ball_dx;
        int low = (COURT_TOP + BALL_R) * FP;
        int span = (COURT_BOTTOM - COURT_TOP - 2 * BALL_R) * FP;
        int reflected = (target - low) % (2 * span);
        if (reflected < 0) reflected += 2 * span;
        if (reflected > span) reflected = 2 * span - reflected;
        target = low + reflected;
        int error = clamp(40 + (pong.score[1] - pong.score[0]) * 4, 20, 64);
        target += ((int)(game_random() % (unsigned)(2 * error + 1)) - error) * FP;
    }
    pong.ai_target = clamp(target - PADDLE_H * FP / 2,
                           COURT_TOP * FP, (COURT_BOTTOM - PADDLE_H) * FP);
    pong.ai_clock = 40;
}

static void pong_goal(int player) {
    if (++pong.score[player] == 7) {
        pong.winner = player + 1;
    } else {
        /* As upstream, serve toward the side which conceded the point. */
        pong_serve(player == 0 ? 1 : -1);
    }
}

static void pong_spin(int paddle_y, int motion, int hit_y) {
    int offset = hit_y - (paddle_y + PADDLE_H * FP / 2);
    pong.ball_dy = clamp(pong.ball_dy + offset / 12 + motion * 64,
                         -5 * FP, 5 * FP);
    if (pong.ball_dy > -FP / 2 && pong.ball_dy < FP / 2)
        pong.ball_dy = offset < 0 ? -FP / 2 : FP / 2;
    int speed = pong.ball_dx < 0 ? -pong.ball_dx : pong.ball_dx;
    speed = clamp(speed + FP / 8, 0, 6 * FP);
    pong.ball_dx = pong.ball_dx < 0 ? speed : -speed;
    pong.ai_clock = 0;
}

static void pong_ball_step(int player_motion, int ai_motion) {
    int old_x = pong.ball_x, old_y = pong.ball_y;
    int next_x = old_x + pong.ball_dx, next_y = old_y + pong.ball_dy;
    int low = (COURT_TOP + BALL_R) * FP;
    int high = (COURT_BOTTOM - BALL_R) * FP;
    if (next_y < low) {
        next_y = 2 * low - next_y;
        pong.ball_dy = -pong.ball_dy;
    } else if (next_y > high) {
        next_y = 2 * high - next_y;
        pong.ball_dy = -pong.ball_dy;
    }

    int leftward = pong.ball_dx < 0;
    int paddle_x = leftward ? PADDLE_LEFT : PADDLE_RIGHT;
    int paddle_y = leftward ? pong.player_y : pong.ai_y;
    int face = (leftward ? paddle_x + PADDLE_W + BALL_R : paddle_x - BALL_R) * FP;
    int min_y = paddle_y - BALL_R * FP;
    int max_y = paddle_y + (PADDLE_H + BALL_R) * FP;
    if ((leftward && old_x >= face && next_x <= face) ||
        (!leftward && old_x <= face && next_x >= face)) {
        /* Swept face intercept: a fast ball cannot tunnel through a paddle. */
        int hit_y = old_y + (next_y - old_y) * (face - old_x) / (next_x - old_x);
        if (hit_y >= min_y && hit_y <= max_y) {
            next_x = 2 * face - next_x;
            pong_spin(paddle_y, leftward ? player_motion : ai_motion, hit_y);
        }
    } else if (next_x >= (paddle_x - BALL_R) * FP &&
               next_x <= (paddle_x + PADDLE_W + BALL_R) * FP) {
        /* Upstream ballIntercept also catches grazes on paddle top/bottom. */
        if (old_y <= min_y && next_y >= min_y && pong.ball_dy > 0) {
            next_y = 2 * min_y - next_y;
            pong.ball_dy = -pong.ball_dy;
        } else if (old_y >= max_y && next_y <= max_y && pong.ball_dy < 0) {
            next_y = 2 * max_y - next_y;
            pong.ball_dy = -pong.ball_dy;
        }
    }
    /* A paddle beside a wall must not reflect a grazing ball through the wall. */
    pong.ball_y = clamp(next_y, low, high);
    pong.ball_x = next_x;
    if (next_x > (COURT_RIGHT + BALL_R) * FP) pong_goal(0);
    else if (next_x < (COURT_LEFT - BALL_R) * FP) pong_goal(1);
}

static int pong_step(void) {
    if (pong.winner) return 0;
    int player_before = pong.player_y, ai_before = pong.ai_y;
    int ball_x_before = pong.ball_x, ball_y_before = pong.ball_y;
    int score_before = pong.score[0] + pong.score[1];
    int was_serving = pong.serve != 0;
    int motion = pong.down - pong.up;
    pong.player_y = clamp(pong.player_y + motion * 3 * FP,
                           COURT_TOP * FP, (COURT_BOTTOM - PADDLE_H) * FP);
    if (!pong.ai_clock) pong_predict();
    --pong.ai_clock;
    int ai_delta = clamp(pong.ai_target - pong.ai_y, -2 * FP, 2 * FP);
    pong.ai_y += ai_delta;
    if (pong.serve) --pong.serve;
    else pong_ball_step(motion, (ai_delta > 0) - (ai_delta < 0));
    return pong.player_y != player_before || pong.ai_y != ai_before ||
           pong.ball_x != ball_x_before || pong.ball_y != ball_y_before ||
           pong.score[0] + pong.score[1] != score_before ||
           (pong.serve != 0) != was_serving;
}

void games_start(enum game_kind kind, uint32_t seed, uint32_t now) {
    active_game = kind;
    random_state = seed ? seed : 0x6d2b79f5u;
    last_tick = now;
    paused = rebase_clock = 0;
    dirty = 1;
    if (kind == GAME_2048) {
        memset(&tiles, 0, sizeof(tiles));
        tiles_spawn();
        tiles_spawn();
    } else {
        memset(&pong, 0, sizeof(pong));
        pong.player_y = pong.ai_y = (COURT_TOP + COURT_BOTTOM - PADDLE_H) * FP / 2;
        pong_serve(game_random() & 1 ? 1 : -1);
    }
}

void games_key(enum game_key key, int down) {
    if (key == GAME_PAUSE) {
        if (down) {
            paused = !paused;
            pong.up = pong.down = 0;
            rebase_clock = dirty = 1;
        }
        return;
    }
    if (key < GAME_UP || key > GAME_LEFT) return;
    if (active_game == GAME_PONG) {
        if (key == GAME_UP) pong.up = down && !paused;
        if (key == GAME_DOWN) pong.down = down && !paused;
    } else if (down && !paused) {
        tiles_move(key);
    }
}

int games_tick(uint32_t now) {
    /* Unsigned subtraction is correct across uint32_t tick rollover. */
    uint32_t elapsed = now - last_tick;
    last_tick = now;
    if (rebase_clock) {
        elapsed = 0;
        rebase_clock = 0;
    }
    if (!paused && active_game == GAME_PONG) {
        /* Drop excess backlog after a stall rather than freezing the shell. */
        if (elapsed > 25) elapsed = 25;
        while (elapsed--) if (pong_step()) dirty = 1;
    }
    int result = dirty;
    dirty = 0;
    return result;
}

static uint32_t tile_color(uint32_t value) {
    static const uint32_t colors[] = {
        0x283c53, 0xcdd7dd, 0xb6d3ce, 0x83c6b5, 0x52b19e,
        0x339985, 0x267e76, 0x417fa3, 0x526bb1, 0x745ab0,
        0x9d59a2, 0xcb638e
    };
    unsigned exponent = 0;
    while (value > 1) { value >>= 1; ++exponent; }
    if (exponent >= sizeof(colors) / sizeof(colors[0])) exponent = 11;
    return colors[exponent];
}

static void tiles_draw(int x, int y, const struct game_canvas *canvas) {
    char number[32];
    canvas->text(x + 16, y + 12, "2048", GAME_ACCENT);
    canvas->rect(x + 16, y + 44, 304, 304, GAME_PANEL);
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned col = 0; col < 4; ++col) {
            uint32_t value = tiles.cells[row][col];
            int tx = x + 24 + (int)col * 74, ty = y + 52 + (int)row * 74;
            canvas->rect(tx, ty, 66, 66, tile_color(value));
            if (value) {
                snprintf(number, sizeof(number), "%u", (unsigned)value);
                int width = (int)strlen(number) * 8;
                canvas->text(tx + (66 - width) / 2, ty + 25, number,
                             value <= 4 ? GAME_BG : GAME_INK);
            }
        }
    }
    snprintf(number, sizeof(number), "Score: %u", (unsigned)tiles.score);
    canvas->text(x + 340, y + 52, number, GAME_INK);
    canvas->text(x + 340, y + 92, paused ? "PAUSED" :
                 (tiles.over ? "GAME OVER - R to restart" :
                  (tiles.won ? "2048 reached! Keep going." : "Join tiles to reach 2048.")),
                 GAME_ACCENT);
    if (tiles.over && tiles.won)
        canvas->text(x + 340, y + 116, "2048 reached!", GAME_ACCENT);
    canvas->text(x + 340, y + 156, "Arrows / WASD: slide", GAME_INK);
    canvas->text(x + 340, y + 180, "P: pause / resume", GAME_INK);
    canvas->text(x + 340, y + 204, "R: restart    Esc: shell", GAME_INK);
    canvas->text(x + 340, y + 248, "Equal tiles merge once per move.", GAME_MUTED);
    canvas->text(x + 340, y + 272, "Tile limit: 1048576", GAME_MUTED);
    canvas->text(x + 340, y + 312, "Based on Gabriele Cirulli's 2048", GAME_MUTED);
    canvas->text(x + 340, y + 336, "MIT license", GAME_MUTED);
}

static void pong_draw(int x, int y, const struct game_canvas *canvas) {
    char score[40];
    canvas->text(x + 16, y + 12, "PONG - first to 7", GAME_ACCENT);
    snprintf(score, sizeof(score), "YOU %u  :  %u CPU",
             (unsigned)pong.score[0], (unsigned)pong.score[1]);
    canvas->text(x + 280, y + 12, score, GAME_INK);
    const char *status = paused ? "PAUSED" :
                         (pong.winner == 1 ? "YOU WIN! R to restart" :
                          (pong.winner == 2 ? "CPU WINS - R to retry" :
                           (pong.serve ? "Serving..." : "Moving paddles add spin")));
    canvas->text(x + 16, y + 40, status, GAME_MUTED);
    canvas->rect(x + COURT_LEFT, y + COURT_TOP - 4,
                 COURT_RIGHT - COURT_LEFT, 4, GAME_MUTED);
    canvas->rect(x + COURT_LEFT, y + COURT_BOTTOM,
                 COURT_RIGHT - COURT_LEFT, 4, GAME_MUTED);
    for (int dy = COURT_TOP + 4; dy + 8 <= COURT_BOTTOM; dy += 16)
        canvas->rect(x + 350, y + dy, 4, 8, GAME_PANEL);
    canvas->rect(x + PADDLE_LEFT, y + pong.player_y / FP,
                 PADDLE_W, PADDLE_H, GAME_ACCENT);
    canvas->rect(x + PADDLE_RIGHT, y + pong.ai_y / FP,
                 PADDLE_W, PADDLE_H, GAME_INK);
    if (!pong.winner)
        canvas->rect(x + pong.ball_x / FP - BALL_R, y + pong.ball_y / FP - BALL_R,
                     BALL_R * 2, BALL_R * 2, GAME_INK);
    canvas->text(x + 16, y + 328, "Up/Down or W/S: move   P: pause   R: restart   Esc: shell", GAME_INK);
    canvas->text(x + 16, y + 350, "Based on Jake Gordon's Javascript Pong - MIT license", GAME_MUTED);
}

void games_draw(int x, int y, const struct game_canvas *canvas) {
    canvas->rect(x, y, GAME_WIDTH, GAME_HEIGHT, GAME_BG);
    if (active_game == GAME_2048) tiles_draw(x, y, canvas);
    else pong_draw(x, y, canvas);
}
