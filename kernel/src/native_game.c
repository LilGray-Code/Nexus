#include "native_game.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t next_random(struct nexus_snake_game *game) {
    uint32_t value = game->random_state;
    if (value == 0U) value = 0x6D2B79F5U;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->random_state = value;
    return value;
}

static bool direction_is_opposite(enum nexus_game_direction a,
                                  enum nexus_game_direction b) {
    return (((uint32_t)a + 2U) & 3U) == (uint32_t)b;
}

static bool spawn_food(struct nexus_snake_game *game) {
    if (game->length >= NEXUS_SNAKE_CELLS) return false;
    const uint16_t start = (uint16_t)(next_random(game) % NEXUS_SNAKE_CELLS);
    for (uint16_t offset = 0U; offset < NEXUS_SNAKE_CELLS; ++offset) {
        const uint16_t cell =
            (uint16_t)((start + offset) % NEXUS_SNAKE_CELLS);
        if (!nexus_snake_cell_is_snake(game, cell)) {
            game->food = cell;
            return true;
        }
    }
    return false;
}

void nexus_snake_init(struct nexus_snake_game *game, uint32_t seed) {
    if (game == NULL) return;
    for (size_t i = 0U; i < NEXUS_SNAKE_CELLS; ++i) game->body[i] = 0U;
    game->length = 3U;
    game->body[0] = (uint16_t)(6U * NEXUS_SNAKE_WIDTH + 10U);
    game->body[1] = (uint16_t)(6U * NEXUS_SNAKE_WIDTH + 9U);
    game->body[2] = (uint16_t)(6U * NEXUS_SNAKE_WIDTH + 8U);
    game->direction = NEXUS_GAME_RIGHT;
    game->queued_direction = NEXUS_GAME_RIGHT;
    game->state = NEXUS_GAME_READY;
    game->random_state = seed == 0U ? 0xA341316CU : seed;
    game->score = 0U;
    (void)spawn_food(game);
}

bool nexus_snake_start(struct nexus_snake_game *game) {
    if (game == NULL || game->state != NEXUS_GAME_READY) return false;
    game->state = NEXUS_GAME_RUNNING;
    return true;
}

bool nexus_snake_turn(struct nexus_snake_game *game,
                      enum nexus_game_direction direction) {
    if (game == NULL || direction > NEXUS_GAME_LEFT ||
        (game->state != NEXUS_GAME_READY &&
         game->state != NEXUS_GAME_RUNNING)) return false;
    if (direction == game->queued_direction ||
        direction_is_opposite(game->direction, direction)) return false;
    game->queued_direction = direction;
    return true;
}

bool nexus_snake_cell_is_snake(const struct nexus_snake_game *game,
                               uint16_t cell) {
    if (game == NULL || cell >= NEXUS_SNAKE_CELLS) return false;
    for (uint16_t i = 0U; i < game->length; ++i) {
        if (game->body[i] == cell) return true;
    }
    return false;
}

bool nexus_snake_cell_is_food(const struct nexus_snake_game *game,
                              uint16_t cell) {
    return game != NULL && cell < NEXUS_SNAKE_CELLS &&
           game->food == cell && game->state != NEXUS_GAME_WON;
}

bool nexus_snake_tick(struct nexus_snake_game *game) {
    if (game == NULL || game->state != NEXUS_GAME_RUNNING ||
        game->length == 0U || game->length > NEXUS_SNAKE_CELLS) return false;

    game->direction = game->queued_direction;
    const uint16_t head = game->body[0];
    uint16_t x = (uint16_t)(head % NEXUS_SNAKE_WIDTH);
    uint16_t y = (uint16_t)(head / NEXUS_SNAKE_WIDTH);
    if (game->direction == NEXUS_GAME_UP) {
        if (y == 0U) { game->state = NEXUS_GAME_OVER; return false; }
        --y;
    } else if (game->direction == NEXUS_GAME_RIGHT) {
        if (x + 1U >= NEXUS_SNAKE_WIDTH) {
            game->state = NEXUS_GAME_OVER; return false;
        }
        ++x;
    } else if (game->direction == NEXUS_GAME_DOWN) {
        if (y + 1U >= NEXUS_SNAKE_HEIGHT) {
            game->state = NEXUS_GAME_OVER; return false;
        }
        ++y;
    } else {
        if (x == 0U) { game->state = NEXUS_GAME_OVER; return false; }
        --x;
    }

    const uint16_t next = (uint16_t)(y * NEXUS_SNAKE_WIDTH + x);
    const bool eating = next == game->food;
    const uint16_t collision_limit =
        eating ? game->length : (uint16_t)(game->length - 1U);
    for (uint16_t i = 0U; i < collision_limit; ++i) {
        if (game->body[i] == next) {
            game->state = NEXUS_GAME_OVER;
            return false;
        }
    }

    if (eating && game->length < NEXUS_SNAKE_CELLS) ++game->length;
    for (uint16_t i = (uint16_t)(game->length - 1U); i > 0U; --i)
        game->body[i] = game->body[i - 1U];
    game->body[0] = next;

    if (eating) {
        ++game->score;
        if (game->length == NEXUS_SNAKE_CELLS || !spawn_food(game)) {
            game->state = NEXUS_GAME_WON;
            return true;
        }
    }
    return true;
}

void nexus_snake_toggle_pause(struct nexus_snake_game *game) {
    if (game == NULL) return;
    if (game->state == NEXUS_GAME_RUNNING) {
        game->state = NEXUS_GAME_PAUSED;
    } else if (game->state == NEXUS_GAME_PAUSED) {
        game->state = NEXUS_GAME_RUNNING;
    }
}
