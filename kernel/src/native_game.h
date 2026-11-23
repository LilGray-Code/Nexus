#ifndef NEXUS_NATIVE_GAME_H
#define NEXUS_NATIVE_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_SNAKE_WIDTH 20U
#define NEXUS_SNAKE_HEIGHT 12U
#define NEXUS_SNAKE_CELLS (NEXUS_SNAKE_WIDTH * NEXUS_SNAKE_HEIGHT)

enum nexus_game_direction {
    NEXUS_GAME_UP = 0,
    NEXUS_GAME_RIGHT,
    NEXUS_GAME_DOWN,
    NEXUS_GAME_LEFT
};

enum nexus_game_state {
    NEXUS_GAME_READY = 0,
    NEXUS_GAME_RUNNING,
    NEXUS_GAME_PAUSED,
    NEXUS_GAME_WON,
    NEXUS_GAME_OVER
};

struct nexus_snake_game {
    uint16_t body[NEXUS_SNAKE_CELLS];
    uint16_t length;
    uint16_t food;
    enum nexus_game_direction direction;
    enum nexus_game_direction queued_direction;
    enum nexus_game_state state;
    uint32_t random_state;
    uint32_t score;
};

void nexus_snake_init(struct nexus_snake_game *game, uint32_t seed);
bool nexus_snake_start(struct nexus_snake_game *game);
bool nexus_snake_turn(struct nexus_snake_game *game,
                      enum nexus_game_direction direction);
bool nexus_snake_tick(struct nexus_snake_game *game);
void nexus_snake_toggle_pause(struct nexus_snake_game *game);
bool nexus_snake_cell_is_snake(const struct nexus_snake_game *game,
                               uint16_t cell);
bool nexus_snake_cell_is_food(const struct nexus_snake_game *game,
                              uint16_t cell);

#endif
