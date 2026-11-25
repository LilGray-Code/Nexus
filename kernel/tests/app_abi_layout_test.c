#include "nexus/app.h"

#include <stddef.h>

_Static_assert(NEXUS_APP_ABI_VERSION == 1U, "app ABI major version");
_Static_assert(sizeof(struct nexus_app_input_event) == 12U,
               "input event layout is stable");
_Static_assert(offsetof(struct nexus_app_input_event, scancode) == 2U,
               "scancode offset is stable");
_Static_assert(offsetof(struct nexus_app_input_event, mouse_dx) == 6U,
               "mouse delta offset is stable");
_Static_assert(sizeof(struct nexus_app_gamepad_event) == 32U,
               "gamepad event layout is stable");
_Static_assert(offsetof(struct nexus_app_gamepad_event, axes) == 16U,
               "gamepad axis offset is stable");
_Static_assert(sizeof(struct nexus_app_display_info) == 40U,
               "display info layout is stable");
_Static_assert(offsetof(struct nexus_app_display_info, stride_bytes) == 24U,
               "display stride offset is stable");
_Static_assert(sizeof(struct nexus_app_blit_request) == 32U,
               "blit request layout is stable");
_Static_assert(offsetof(struct nexus_app_blit_request, pixels) == 24U,
               "pixel pointer offset is stable");

int main(void) {
    return NEXUS_APP_SYS_FRAMEBUFFER_BLIT == 11ULL &&
           NEXUS_APP_SYS_SLEEP_TICKS == 12ULL &&
           NEXUS_APP_SYS_GAMEPAD_POLL == 13ULL &&
           NEXUS_APP_SYS_AUDIO_WRITE == 14ULL &&
           NEXUS_APP_AUDIO_SAMPLE_RATE_HZ == 48000U &&
           NEXUS_APP_AUDIO_MAX_FRAMES == 1024U ? 0 : 1;
}
