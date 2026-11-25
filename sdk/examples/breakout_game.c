#include "nexus/app.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define W 128U
#define H 96U
#define BRICK_COLUMNS 8U
#define BRICK_ROWS 3U
#define BRICK_WIDTH 14U
#define BRICK_HEIGHT 5U
#define BRICK_GAP 1U
#define BRICK_LEFT 4U
#define BRICK_TOP 22U
#define PADDLE_WIDTH 20U
#define PADDLE_HEIGHT 3U
#define PADDLE_Y 88U
#define BALL_SIZE 3U

static uint32_t frame[W * H];
static uint8_t bricks[BRICK_COLUMNS * BRICK_ROWS];

struct game {
    uint32_t paddle_x, score, lives;
    int32_t ball_x, ball_y, ball_vx, ball_vy;
    bool game_over;
};

static uint8_t glyph(char c, uint32_t row) {
    static const uint8_t numbers[10][5] = {
        {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},
        {5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},
        {7,5,7,5,7},{7,5,7,1,7}
    };
    if (row >= 5U) return 0U;
    if (c >= '0' && c <= '9') return numbers[(uint32_t)(c - '0')][row];
#define GLYPH(ch,a,b,c1,d,e) case ch: { static const uint8_t g[5]={a,b,c1,d,e}; return g[row]; }
    switch (c) {
        GLYPH('A',2,5,7,5,5) GLYPH('B',6,5,6,5,6)
        GLYPH('C',3,4,4,4,3) GLYPH('E',7,4,6,4,7)
        GLYPH('G',3,4,5,5,3) GLYPH('I',7,2,2,2,7)
        GLYPH('L',4,4,4,4,7) GLYPH('M',5,7,7,5,5)
        GLYPH('N',5,7,7,7,5) GLYPH('O',7,5,5,5,7)
        GLYPH('P',6,5,6,4,4) GLYPH('R',6,5,6,5,5)
        GLYPH('S',7,4,7,1,7) GLYPH('T',7,2,2,2,2)
        GLYPH('U',5,5,5,5,7) GLYPH('V',5,5,5,5,2)
        GLYPH('X',5,5,2,5,5)
        default: return 0U;
    }
#undef GLYPH
}

static void rect(uint32_t x, uint32_t y, uint32_t width,
                 uint32_t height, uint32_t color) {
    if (x >= W || y >= H) return;
    if (width > W - x) width = W - x;
    if (height > H - y) height = H - y;
    for (uint32_t r = 0U; r < height; ++r)
        for (uint32_t c = 0U; c < width; ++c)
            frame[(y + r) * W + x + c] = color;
}

static void text(uint32_t x, uint32_t y, const char *value,
                 uint32_t color) {
    for (size_t i = 0U; value[i] != '\0'; ++i)
        for (uint32_t r = 0U; r < 5U; ++r) {
            uint8_t bits = glyph(value[i], r);
            for (uint32_t c = 0U; c < 3U; ++c)
                if ((bits & (1U << (2U - c))) != 0U)
                    rect(x + (uint32_t)i * 4U + c, y + r, 1U, 1U, color);
        }
}

static void number(uint32_t x, uint32_t y, uint32_t value,
                   uint32_t digits, uint32_t color) {
    char result[9];
    if (digits > 8U) digits = 8U;
    for (uint32_t i = 0U; i < digits; ++i) {
        result[digits - i - 1U] = (char)('0' + value % 10U);
        value /= 10U;
    }
    result[digits] = '\0';
    text(x, y, result, color);
}

static void reset_bricks(void) {
    for (size_t i = 0U; i < sizeof(bricks); ++i) bricks[i] = 1U;
}

static void reset_ball(struct game *g) {
    g->ball_x = (int32_t)(W / 2U);
    g->ball_y = (int32_t)(PADDLE_Y - 7U);
    g->ball_vx = 1;
    g->ball_vy = -1;
}

static void reset_game(struct game *g) {
    g->paddle_x = (W - PADDLE_WIDTH) / 2U;
    g->score = 0U;
    g->lives = 3U;
    g->game_over = false;
    reset_bricks();
    reset_ball(g);
}

static void draw(const struct game *g) {
    for (uint32_t y = 0U; y < H; ++y) {
        uint32_t color = y < 18U ? 0x00121D31U : 0x00091422U;
        for (uint32_t x = 0U; x < W; ++x)
            frame[y * W + x] = color;
    }
    text(3U, 2U, "NEXUS BREAKOUT", 0x00E6EDF7U);
    text(3U, 11U, "SCORE", 0x008EA7C7U);
    number(25U, 11U, g->score, 4U, 0x00F2C14EU);
    text(82U, 11U, "LIVES", 0x008EA7C7U);
    number(105U, 11U, g->lives, 1U, 0x00F2C14EU);

    const uint32_t colors[BRICK_ROWS] = {
        0x00E06C75U, 0x00E09F3EU, 0x002BB5A8U
    };
    for (uint32_t row = 0U; row < BRICK_ROWS; ++row)
        for (uint32_t column = 0U; column < BRICK_COLUMNS; ++column) {
            if (bricks[row * BRICK_COLUMNS + column] == 0U) continue;
            uint32_t x = BRICK_LEFT + column * (BRICK_WIDTH + BRICK_GAP);
            uint32_t y = BRICK_TOP + row * (BRICK_HEIGHT + 3U);
            rect(x, y, BRICK_WIDTH, BRICK_HEIGHT, colors[row]);
            rect(x + 1U, y + 1U, BRICK_WIDTH - 2U, 1U, 0x00F5D4B7U);
        }

    rect(1U, 19U, W - 2U, 1U, 0x003C5573U);
    rect(1U, H - 2U, W - 2U, 1U, 0x003C5573U);
    rect(g->paddle_x, PADDLE_Y, PADDLE_WIDTH, PADDLE_HEIGHT, 0x004DA3FFU);
    rect((uint32_t)g->ball_x, (uint32_t)g->ball_y,
         BALL_SIZE, BALL_SIZE, 0x00FFF2CCU);
    if (g->game_over) {
        rect(19U, 37U, 90U, 24U, 0x00202F46U);
        rect(19U, 37U, 90U, 1U, 0x00E06C75U);
        text(45U, 42U, "GAME OVER", 0x00F4F7FBU);
        text(53U, 51U, "ENTER", 0x00F2C14EU);
    }
}

static void hit_brick(struct game *g) {
    for (uint32_t row = 0U; row < BRICK_ROWS; ++row)
        for (uint32_t column = 0U; column < BRICK_COLUMNS; ++column) {
            size_t index = row * BRICK_COLUMNS + column;
            if (bricks[index] == 0U) continue;
            int32_t x = (int32_t)(BRICK_LEFT +
                column * (BRICK_WIDTH + BRICK_GAP));
            int32_t y = (int32_t)(BRICK_TOP + row * (BRICK_HEIGHT + 3U));
            if (g->ball_x + (int32_t)BALL_SIZE > x &&
                g->ball_x < x + (int32_t)BRICK_WIDTH &&
                g->ball_y + (int32_t)BALL_SIZE > y &&
                g->ball_y < y + (int32_t)BRICK_HEIGHT) {
                bricks[index] = 0U;
                g->ball_vy = -g->ball_vy;
                g->score += 10U;
                return;
            }
        }
}

static void update(struct game *g, int32_t move) {
    if (g->game_over) return;
    int32_t paddle = (int32_t)g->paddle_x + move;
    if (paddle < 2) paddle = 2;
    if (paddle > (int32_t)(W - PADDLE_WIDTH - 2U))
        paddle = (int32_t)(W - PADDLE_WIDTH - 2U);
    g->paddle_x = (uint32_t)paddle;

    g->ball_x += g->ball_vx;
    g->ball_y += g->ball_vy;
    if (g->ball_x <= 1) { g->ball_x = 1; g->ball_vx = 1; }
    else if (g->ball_x >= (int32_t)(W - BALL_SIZE - 1U)) {
        g->ball_x = (int32_t)(W - BALL_SIZE - 1U);
        g->ball_vx = -1;
    }
    if (g->ball_y <= 20) { g->ball_y = 20; g->ball_vy = 1; }

    hit_brick(g);
    if (g->ball_vy > 0 &&
        g->ball_y + (int32_t)BALL_SIZE >= (int32_t)PADDLE_Y &&
        g->ball_y <= (int32_t)(PADDLE_Y + PADDLE_HEIGHT) &&
        g->ball_x + (int32_t)BALL_SIZE >= (int32_t)g->paddle_x &&
        g->ball_x <= (int32_t)(g->paddle_x + PADDLE_WIDTH)) {
        g->ball_y = (int32_t)(PADDLE_Y - BALL_SIZE);
        g->ball_vy = -1;
        int32_t offset = g->ball_x + 1 -
            ((int32_t)g->paddle_x + (int32_t)(PADDLE_WIDTH / 2U));
        if (offset < -3) g->ball_vx = -1;
        else if (offset > 3) g->ball_vx = 1;
    }

    if (g->ball_y >= (int32_t)(H - BALL_SIZE - 1U)) {
        if (g->lives > 0U) --g->lives;
        if (g->lives == 0U) g->game_over = true;
        else reset_ball(g);
    }

    bool remaining = false;
    for (size_t i = 0U; i < sizeof(bricks); ++i)
        remaining = remaining || bricks[i] != 0U;
    if (!remaining) { g->score += 100U; reset_bricks(); reset_ball(g); }
}

void _start(void) {
    if (nexus_app_abi_version() != NEXUS_APP_ABI_VERSION)
        nexus_app_exit(2U);
    struct nexus_app_display_info display = {0};
    if (nexus_app_get_display(&display) != 0 || display.width < W ||
        display.height < H || display.pixel_format != NEXUS_APP_PIXEL_XRGB8888)
        nexus_app_exit(3U);

    struct game g = {0};
    reset_game(&g);
    bool left = false, right = false, gamepad = false;
    int16_t axis = 0;
    uint32_t ox = (uint32_t)(display.width - W) / 2U;
    uint32_t oy = (uint32_t)(display.height - H) / 2U;

    for (;;) {
        struct nexus_app_input_event input;
        while (nexus_app_input_poll(&input) > 0) {
            if (input.type != NEXUS_APP_INPUT_KEYBOARD) continue;
            uint32_t key = input.scancode & 0x7FU;
            bool up = (input.scancode & NEXUS_APP_KEY_RELEASE) != 0U;
            if (key == 0x01U && !up) nexus_app_exit(0U);
            if (key == 0x4BU) left = !up;
            if (key == 0x4DU) right = !up;
            if (key == 0x1CU && !up && g.game_over) reset_game(&g);
        }
        struct nexus_app_gamepad_event pad;
        while (nexus_app_gamepad_poll(&pad) > 0) {
            gamepad = (pad.flags & NEXUS_APP_GAMEPAD_DISCONNECTED) == 0U;
            axis = gamepad && pad.axis_count > 0U ? pad.axes[0] : 0;
        }
        int32_t move = (left ? -3 : 0) + (right ? 3 : 0);
        if (gamepad) {
            if (axis < -7000) move = -3;
            else if (axis > 7000) move = 3;
        }
        update(&g, move);
        draw(&g);
        if (nexus_app_blit(ox, oy, W, H, frame, W) != 0)
            nexus_app_exit(4U);
        (void)nexus_app_sleep_ticks(1U);
    }
}
