#include "boot_mode.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(!nexus_boot_mode_is_installer(NULL));
    assert(!nexus_boot_mode_is_installer(""));
    assert(!nexus_boot_mode_is_installer("nexus.mode=desktop"));
    assert(!nexus_boot_mode_is_installer("x-nexus.mode=installer"));
    assert(!nexus_boot_mode_is_installer("nexus.mode=installer-extra"));
    assert(nexus_boot_mode_is_installer("nexus.mode=installer"));
    assert(nexus_boot_mode_is_installer(
        "quiet nexus.mode=installer loglevel=3"));
    assert(nexus_boot_mode_is_installer(
        "\tnexus.mode=desktop\t nexus.mode=installer "));
    puts("NEXUS BOOT MODE PARSER TESTS PASSED");
    return 0;
}
