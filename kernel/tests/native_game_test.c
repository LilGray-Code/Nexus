#include "native_game.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    struct nexus_snake_game game;
    struct nexus_snake_game same;
    nexus_snake_init(&game, 1234U);
    nexus_snake_init(&same, 1234U);
    assert(game.state == NEXUS_GAME_READY);
    assert(game.length == 3U);
    assert(game.food == same.food);
    assert(game.food < NEXUS_SNAKE_CELLS);
    assert(nexus_snake_cell_is_snake(&game, game.body[0]));
    assert(nexus_snake_cell_is_food(&game, game.food));
    assert(!nexus_snake_start(NULL));
    assert(nexus_snake_start(&game));
    assert(!nexus_snake_start(&game));

    /* Reject direct reversal and accept a perpendicular turn. */
    assert(!nexus_snake_turn(&game, NEXUS_GAME_LEFT));
    assert(nexus_snake_turn(&game, NEXUS_GAME_DOWN));
    assert(nexus_snake_tick(&game));
    assert(game.body[0] == 7U * NEXUS_SNAKE_WIDTH + 10U);

    nexus_snake_toggle_pause(&game);
    assert(game.state == NEXUS_GAME_PAUSED);
    assert(!nexus_snake_tick(&game));
    nexus_snake_toggle_pause(&game);
    assert(game.state == NEXUS_GAME_RUNNING);

    /* A wall collision ends the game without leaving the bounded board. */
    nexus_snake_init(&game, 1U);
    game.state = NEXUS_GAME_RUNNING;
    game.direction = NEXUS_GAME_UP;
    game.queued_direction = NEXUS_GAME_UP;
    game.body[0] = 0U;
    assert(!nexus_snake_tick(&game));
    assert(game.state == NEXUS_GAME_OVER);
    assert(!nexus_snake_turn(&game, NEXUS_GAME_RIGHT));

    puts("Nexus native Snake game tests passed");
    return 0;
}
