#include "random.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    uint8_t bytes[64];
    for (size_t i = 0U; i < sizeof(bytes); ++i) bytes[i] = 0xA5U;

    assert(nexus_rng_fill(NULL, 0U));
    assert(!nexus_rng_fill(NULL, 1U));

    const bool available = nexus_rng_available();
    const bool filled = nexus_rng_fill(bytes, sizeof(bytes));
    if (filled) {
        assert(available);
        bool nonzero = false;
        for (size_t i = 0U; i < sizeof(bytes); ++i)
            nonzero = nonzero || bytes[i] != 0U;
        assert(nonzero);

        uint8_t second[64];
        assert(nexus_rng_fill(second, sizeof(second)));
        bool different = false;
        for (size_t i = 0U; i < sizeof(bytes); ++i)
            different = different || bytes[i] != second[i];
        assert(different);
    } else {
        for (size_t i = 0U; i < sizeof(bytes); ++i)
            assert(bytes[i] == 0U);
    }

    uint32_t word = 0xA5A5A5A5U;
    const bool word_filled = nexus_rng_u32(&word);
    if (!word_filled) assert(word == 0U);
    else assert(available);
    assert(!nexus_rng_u32(NULL));
    return 0;
}
