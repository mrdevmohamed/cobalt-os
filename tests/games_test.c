/* Deterministic white-box tests: compile this file, not a separate games.c.
 * The included gameplay adaptations carry their upstream MIT notices.
 * A nonzero exit identifies the failing test group; no host OS API is needed.
 */
#include "../games/games.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static unsigned occupied(void) {
    unsigned count = 0;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            if (tiles.cells[row][col]) ++count;
    return count;
}

static int test_merges(void) {
    games_start(GAME_2048, 1, 0);
    memset(&tiles, 0, sizeof(tiles));
    for (unsigned i = 0; i < 4; ++i) tiles.cells[0][i] = 2;
    CHECK(tiles_slide(GAME_LEFT));
    CHECK(tiles.cells[0][0] == 4 && tiles.cells[0][1] == 4);
    CHECK(!tiles.cells[0][2] && !tiles.cells[0][3]);
    CHECK(tiles.score == 8 && occupied() == 2);

    memset(&tiles, 0, sizeof(tiles));
    for (unsigned i = 0; i < 4; ++i) tiles.cells[0][i] = 2;
    CHECK(tiles_slide(GAME_RIGHT));
    CHECK(!tiles.cells[0][0] && !tiles.cells[0][1]);
    CHECK(tiles.cells[0][2] == 4 && tiles.cells[0][3] == 4);
    CHECK(tiles.score == 8);

    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][2] = 2;
    tiles.cells[2][2] = 2;
    tiles.cells[3][2] = 4;
    CHECK(tiles_slide(GAME_UP));
    CHECK(tiles.cells[0][2] == 4 && tiles.cells[1][2] == 4);
    CHECK(!tiles.cells[2][2] && !tiles.cells[3][2]);
    CHECK(tiles.score == 4);

    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][1] = 2;
    tiles.cells[2][1] = 2;
    tiles.cells[3][1] = 4;
    CHECK(tiles_slide(GAME_DOWN));
    CHECK(!tiles.cells[0][1] && !tiles.cells[1][1]);
    CHECK(tiles.cells[2][1] == 4 && tiles.cells[3][1] == 4);
    CHECK(tiles.score == 4);

    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][0] = tiles.cells[0][1] = 2;
    tiles.cells[0][2] = 4;
    CHECK(tiles_slide(GAME_LEFT));
    CHECK(tiles.cells[0][0] == 4 && tiles.cells[0][1] == 4);
    CHECK(tiles.score == 4); /* A newly created four cannot merge again. */
    CHECK(tiles_slide(GAME_LEFT));
    CHECK(tiles.cells[0][0] == 8 && !tiles.cells[0][1]);
    CHECK(tiles.score == 12);

    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][0] = tiles.cells[0][1] = 4;
    tiles.cells[0][2] = tiles.cells[0][3] = 8;
    CHECK(tiles_slide(GAME_LEFT));
    CHECK(tiles.cells[0][0] == 8 && tiles.cells[0][1] == 16);
    CHECK(tiles.score == 24);
    return 0;
}

static int test_spawn_noop_and_input(void) {
    uint32_t saved[4][4];
    games_start(GAME_2048, 1234, 0);
    CHECK(!paused && occupied() == 2);
    memcpy(saved, tiles.cells, sizeof(saved));
    games_start(GAME_2048, 1234, 0);
    CHECK(!memcmp(saved, tiles.cells, sizeof(saved)));
    games_start(GAME_2048, 0, 0);
    CHECK(occupied() == 2 && random_state != 0);

    memset(&tiles, 0, sizeof(tiles));
    random_state = 1; /* First xorshift value is 270369: value roll nine. */
    tiles_spawn();
    CHECK(occupied() == 1);
    unsigned fours = 0;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            if (tiles.cells[row][col] == 4) ++fours;
    CHECK(fours == 1);
    memset(&tiles, 0, sizeof(tiles));
    random_state = 2; /* First xorshift value is 540738: value roll eight. */
    tiles_spawn();
    unsigned twos = 0;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            if (tiles.cells[row][col] == 2) ++twos;
    CHECK(twos == 1 && occupied() == 1);

    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][0] = 2;
    (void)games_tick(0);
    uint32_t before = random_state;
    games_key(GAME_RIGHT, 0);
    CHECK(tiles.cells[0][0] == 2 && occupied() == 1);
    games_key(GAME_LEFT, 1);
    CHECK(random_state == before && !tiles.score);
    CHECK(occupied() == 1 && games_tick(0) == 0);
    games_key(GAME_RIGHT, 1);
    CHECK(tiles.cells[0][3] == 2 && occupied() == 2);
    CHECK(random_state != before && games_tick(0));
    CHECK(!games_tick(0));

    games_key(GAME_PAUSE, 1);
    memcpy(saved, tiles.cells, sizeof(saved));
    games_key(GAME_DOWN, 1);
    CHECK(paused && !memcmp(saved, tiles.cells, sizeof(saved)));
    games_key(GAME_PAUSE, 0);
    CHECK(paused);
    games_key(GAME_PAUSE, 1);
    CHECK(!paused);
    return 0;
}

static int test_gameover_win_and_limits(void) {
    games_start(GAME_2048, 1, 0);
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            tiles.cells[row][col] = (row + col) % 2 ? 2 : 4;
    CHECK(!tiles_moves_available());
    uint32_t before = random_state;
    games_key(GAME_LEFT, 1);
    CHECK(tiles.over && random_state == before && !tiles.score);
    games_key(GAME_UP, 1);
    CHECK(tiles.over && random_state == before);
    tiles.cells[0][0] = tiles.cells[0][1];
    CHECK(tiles_moves_available());
    tiles.cells[0][0] = 0;
    CHECK(tiles_moves_available());

    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][0] = tiles.cells[0][1] = 1024;
    games_key(GAME_LEFT, 1);
    CHECK(tiles.won && !tiles.over && tiles.score == 2048);
    CHECK(tiles.cells[0][0] == 2048 && occupied() == 2);
    memset(tiles.cells, 0, sizeof(tiles.cells));
    tiles.cells[0][0] = tiles.cells[0][1] = 2048;
    games_key(GAME_LEFT, 1);
    CHECK(tiles.won && tiles.cells[0][0] == 4096 && tiles.score == 6144);

    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][0] = tiles.cells[0][1] = 2;
    tiles.score = UINT32_MAX - 1;
    CHECK(tiles_slide(GAME_LEFT));
    CHECK(tiles.score == UINT32_MAX);
    memset(&tiles, 0, sizeof(tiles));
    tiles.cells[0][0] = tiles.cells[0][1] = TILE_MAX / 2;
    CHECK(tiles_slide(GAME_LEFT));
    CHECK(tiles.cells[0][0] == TILE_MAX && tiles.score == TILE_MAX);
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col) tiles.cells[row][col] = TILE_MAX;
    CHECK(!tiles_slide(GAME_LEFT));
    CHECK(!tiles_moves_available());
    before = random_state;
    tiles_spawn();
    CHECK(random_state == before && occupied() == 16);
    return 0;
}

static void pong_fixture(void) {
    games_start(GAME_PONG, 42, 0);
    pong.serve = 0;
    pong.ai_clock = 100;
    pong.ai_target = pong.ai_y;
}

static int test_pong_collisions(void) {
    pong_fixture();
    int face = (PADDLE_LEFT + PADDLE_W + BALL_R) * FP;
    pong.ball_x = face + FP;
    pong.ball_y = pong.player_y + PADDLE_H * FP / 2;
    pong.ball_dx = -4 * FP;
    pong.ball_dy = FP;
    pong_ball_step(0, 0);
    CHECK(pong.ball_dx > 4 * FP && pong.ball_x > face);
    CHECK(!pong.score[0] && !pong.score[1]);
    CHECK(pong.ball_dy != 0);

    pong_fixture();
    face = (PADDLE_RIGHT - BALL_R) * FP;
    pong.ball_x = face - FP;
    pong.ball_y = pong.ai_y + PADDLE_H * FP / 2;
    pong.ball_dx = 4 * FP;
    pong.ball_dy = -FP;
    pong_ball_step(0, 0);
    CHECK(pong.ball_dx < -4 * FP && pong.ball_x < face);

    pong_fixture();
    int top = (COURT_TOP + BALL_R) * FP;
    pong.ball_y = top + FP;
    pong.ball_dx = 3 * FP;
    pong.ball_dy = -3 * FP;
    pong_ball_step(0, 0);
    CHECK(pong.ball_y == top + 2 * FP && pong.ball_dy == 3 * FP);
    int bottom = (COURT_BOTTOM - BALL_R) * FP;
    pong.ball_y = bottom - FP;
    pong.ball_dy = 3 * FP;
    pong_ball_step(0, 0);
    CHECK(pong.ball_y == bottom - 2 * FP && pong.ball_dy == -3 * FP);

    pong_fixture();
    pong.player_y = 120 * FP;
    pong.ball_x = (PADDLE_LEFT + 5) * FP;
    pong.ball_y = pong.player_y - (BALL_R + 1) * FP;
    pong.ball_dx = -FP;
    pong.ball_dy = 3 * FP;
    pong_ball_step(0, 0);
    CHECK(pong.ball_dy == -3 * FP);
    CHECK(pong.ball_y == pong.player_y - (BALL_R + 2) * FP);

    pong_fixture();
    pong.player_y = COURT_TOP * FP;
    pong.ball_x = (PADDLE_LEFT + PADDLE_W + BALL_R + 1) * FP;
    pong.ball_y = (COURT_BOTTOM - BALL_R - 10) * FP;
    pong.ball_dx = -4 * FP;
    pong.ball_dy = -FP;
    pong_ball_step(0, 0);
    CHECK(pong.ball_dx == -4 * FP); /* Outside paddle: no phantom bounce. */
    return 0;
}

static int test_pong_score_serve_and_controls(void) {
    pong_fixture();
    pong.ball_x = (COURT_LEFT - BALL_R + 1) * FP;
    pong.ball_dx = -3 * FP;
    pong_ball_step(0, 0);
    CHECK(pong.score[1] == 1 && !pong.score[0]);
    CHECK(pong.serve == 75 && pong.ball_dx < 0 && !pong.winner);
    CHECK(pong.ball_x == (COURT_LEFT + COURT_RIGHT) * FP / 2);
    int ball_before = pong.ball_x;
    for (int i = 0; i < 75; ++i) (void)pong_step();
    CHECK(!pong.serve && pong.ball_x == ball_before);
    CHECK(pong_step() && pong.ball_x < ball_before);

    pong_fixture();
    pong.ball_x = (COURT_RIGHT + BALL_R - 1) * FP;
    pong.ball_dx = 3 * FP;
    pong_ball_step(0, 0);
    CHECK(pong.score[0] == 1 && !pong.score[1]);
    CHECK(pong.serve == 75 && pong.ball_dx > 0);

    pong_fixture();
    pong.score[0] = 6;
    pong.ball_x = (COURT_RIGHT + BALL_R - 1) * FP;
    pong.ball_dx = 3 * FP;
    pong_ball_step(0, 0);
    CHECK(pong.score[0] == 7 && pong.winner == 1);
    ball_before = pong.ball_x;
    CHECK(!pong_step() && pong.ball_x == ball_before);
    pong_fixture();
    pong.score[1] = 6;
    pong.ball_x = (COURT_LEFT - BALL_R + 1) * FP;
    pong.ball_dx = -3 * FP;
    pong_ball_step(0, 0);
    CHECK(pong.score[1] == 7 && pong.winner == 2);

    pong_fixture();
    int player_before = pong.player_y;
    games_key(GAME_UP, 1);
    (void)pong_step();
    CHECK(pong.player_y == player_before - 3 * FP);
    games_key(GAME_DOWN, 1);
    player_before = pong.player_y;
    (void)pong_step();
    CHECK(pong.player_y == player_before); /* Opposite held keys cancel. */
    games_key(GAME_UP, 0);
    (void)pong_step();
    CHECK(pong.player_y == player_before + 3 * FP);
    games_key(GAME_DOWN, 0);
    player_before = pong.player_y;
    (void)pong_step();
    CHECK(pong.player_y == player_before);
    pong.player_y = COURT_TOP * FP;
    games_key(GAME_UP, 1);
    (void)pong_step();
    CHECK(pong.player_y == COURT_TOP * FP);
    pong.player_y = (COURT_BOTTOM - PADDLE_H) * FP;
    games_key(GAME_UP, 0);
    games_key(GAME_DOWN, 1);
    (void)pong_step();
    CHECK(pong.player_y == (COURT_BOTTOM - PADDLE_H) * FP);

    pong_fixture();
    pong.ai_clock = 0;
    pong.ball_dx = 6 * FP;
    pong.ball_dy = 5 * FP;
    pong_predict();
    CHECK(pong.ai_target >= COURT_TOP * FP);
    CHECK(pong.ai_target <= (COURT_BOTTOM - PADDLE_H) * FP);
    int ai_before = pong.ai_y;
    (void)pong_step();
    CHECK(pong.ai_y - ai_before <= 2 * FP && ai_before - pong.ai_y <= 2 * FP);
    return 0;
}

static int test_pause_rollover_and_backlog(void) {
    uint32_t start = UINT32_MAX - 2;
    games_start(GAME_PONG, 7, start);
    pong.serve = 0;
    pong.ai_clock = 100;
    pong.ai_target = pong.ai_y;
    CHECK(games_tick(start));
    CHECK(!games_tick(start));
    games_key(GAME_UP, 1);
    int player_before = pong.player_y;
    CHECK(games_tick(1)); /* Four ticks, including the zero tick. */
    CHECK(pong.player_y == player_before - 12 * FP);
    games_key(GAME_UP, 0);
    player_before = pong.player_y;
    (void)games_tick(2);
    CHECK(pong.player_y == player_before);

    games_key(GAME_UP, 1);
    games_key(GAME_PAUSE, 1);
    CHECK(paused && !pong.up && !pong.down);
    int ball_before = pong.ball_x;
    CHECK(games_tick(1000));
    CHECK(!games_tick(1500));
    CHECK(pong.player_y == player_before && pong.ball_x == ball_before);
    games_key(GAME_UP, 1);
    CHECK(!pong.up);
    games_key(GAME_PAUSE, 0);
    CHECK(paused);
    games_key(GAME_PAUSE, 1);
    CHECK(!paused && games_tick(9000));
    CHECK(pong.ball_x == ball_before); /* Paused time is never caught up. */
    games_key(GAME_UP, 1);
    CHECK(games_tick(9001));
    CHECK(pong.player_y == player_before - 3 * FP);

    games_start(GAME_PONG, 8, 0);
    CHECK(!paused && !pong.up && !pong.down && !pong.winner);
    games_key(GAME_UP, 1);
    player_before = pong.player_y;
    (void)games_tick(10000);
    CHECK(pong.player_y == player_before - 25 * 3 * FP);
    CHECK(pong.serve == 50); /* Work is bounded even after a long stall. */
    return 0;
}

static int draw_bad, draw_count, clear_count, paused_text;
static const int draw_x = 23, draw_y = 37;

static void check_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0 || x < draw_x || y < draw_y ||
        x + w > draw_x + GAME_WIDTH || y + h > draw_y + GAME_HEIGHT) draw_bad = 1;
    if (!draw_count && x == draw_x && y == draw_y &&
        w == GAME_WIDTH && h == GAME_HEIGHT && color == GAME_BG) ++clear_count;
    ++draw_count;
}

static void check_text(int x, int y, const char *value, uint32_t color) {
    (void)color;
    if (x < draw_x || y < draw_y ||
        x + (int)strlen(value) * 8 > draw_x + GAME_WIDTH ||
        y + 16 > draw_y + GAME_HEIGHT) draw_bad = 1;
    if (!strcmp(value, "PAUSED")) ++paused_text;
    ++draw_count;
}

static int check_draw(void) {
    const struct game_canvas canvas = {check_rect, check_text};
    draw_bad = draw_count = clear_count = paused_text = 0;
    games_draw(draw_x, draw_y, &canvas);
    CHECK(!draw_bad && draw_count > 10 && clear_count == 1);
    CHECK((paused_text != 0) == (paused != 0));
    return 0;
}

static int test_drawing_bounds(void) {
    games_start(GAME_2048, 1, 0);
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col) tiles.cells[row][col] = TILE_MAX;
    tiles.score = UINT32_MAX;
    tiles.won = tiles.over = 1;
    CHECK(!check_draw());
    games_key(GAME_PAUSE, 1);
    CHECK(!check_draw());
    games_start(GAME_PONG, 1, 0);
    CHECK(!check_draw());
    pong.ball_x = (COURT_RIGHT + BALL_R) * FP;
    pong.ball_y = (COURT_TOP + BALL_R) * FP;
    CHECK(!check_draw());
    pong.ball_x = (COURT_LEFT - BALL_R) * FP;
    pong.ball_y = (COURT_BOTTOM - BALL_R) * FP;
    CHECK(!check_draw());
    games_key(GAME_PAUSE, 1);
    CHECK(!check_draw());
    games_key(GAME_PAUSE, 1);
    pong.score[0] = 7;
    pong.winner = 1;
    CHECK(!check_draw());
    return 0;
}

int main(void) {
    if (test_merges()) return 1;
    if (test_spawn_noop_and_input()) return 2;
    if (test_gameover_win_and_limits()) return 3;
    if (test_pong_collisions()) return 4;
    if (test_pong_score_serve_and_controls()) return 5;
    if (test_pause_rollover_and_backlog()) return 6;
    if (test_drawing_bounds()) return 7;
    return 0;
}
