#include "installer_core.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static struct nexus_installer_target good_target(void) {
    return (struct nexus_installer_target){
        .stable_id = "nvme:serial:NEXUS-TEST-0001",
        .model = "Nexus Mock NVMe",
        .block_count = 64ULL * 1024ULL * 1024ULL * 1024ULL / 512ULL,
        .block_size = 512U,
        .is_live_media = false,
        .is_mounted = false
    };
}

static void test_full_confirmation_state_machine(void) {
    struct nexus_installer_context context;
    struct nexus_installer_target target = good_target();
    nexus_installer_init(&context);
    assert(context.state == NEXUS_INSTALLER_IDLE);
    assert(nexus_installer_select_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_create_plan(&context, NEXUS_FIRMWARE_UEFI) ==
           NEXUS_INSTALLER_ERROR_NONE);
    /* The selected target identity is already captured by the UI; only one
     * final typed ERASE confirmation is required from the user. */
    assert(nexus_installer_confirm_destructive(&context, "ERASE NOW") ==
           NEXUS_INSTALLER_ERROR_CONFIRMATION);
    assert(nexus_installer_confirm_destructive(&context, "ERASE") ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(!nexus_installer_can_apply(&context));
    assert(nexus_installer_revalidate_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_can_apply(&context));
    assert(nexus_installer_begin_apply(&context) == NEXUS_INSTALLER_ERROR_NONE);
    assert(context.state == NEXUS_INSTALLER_APPLYING);
    assert(nexus_installer_finish_apply(&context, true) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(context.state == NEXUS_INSTALLER_COMPLETE);
}

static void test_refuses_live_mounted_or_unsupported_targets(void) {
    struct nexus_installer_context context;
    struct nexus_installer_target target = good_target();
    nexus_installer_init(&context);
    target.is_live_media = true;
    assert(nexus_installer_select_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_TARGET);
    target = good_target();
    target.is_mounted = true;
    assert(nexus_installer_select_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_TARGET);
    target = good_target();
    target.block_size = 768U;
    assert(nexus_installer_select_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_TARGET);
    assert(context.state == NEXUS_INSTALLER_IDLE);
}

static void test_changed_target_requires_recovery(void) {
    struct nexus_installer_context context;
    struct nexus_installer_target target = good_target();
    nexus_installer_init(&context);
    assert(nexus_installer_select_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_create_plan(&context, NEXUS_FIRMWARE_LEGACY_BIOS) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_confirm_destructive(&context, "ERASE") ==
           NEXUS_INSTALLER_ERROR_NONE);
    target.block_count -= 1U;
    assert(nexus_installer_revalidate_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_CHANGED_TARGET);
    assert(context.state == NEXUS_INSTALLER_FAILED);
    assert(!nexus_installer_can_apply(&context));
}

static void test_cancel_and_failed_apply(void) {
    struct nexus_installer_context context;
    struct nexus_installer_target target = good_target();
    nexus_installer_init(&context);
    assert(nexus_installer_select_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_NONE);
    nexus_installer_cancel(&context);
    assert(context.state == NEXUS_INSTALLER_CANCELLED);
    nexus_installer_init(&context);
    assert(nexus_installer_select_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_create_plan(&context, NEXUS_FIRMWARE_UEFI) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_confirm_target(&context,
               "nvme:serial:NEXUS-TEST-0001") == NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_confirm_destructive(&context, "ERASE") ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_revalidate_target(&context, &target) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_begin_apply(&context) == NEXUS_INSTALLER_ERROR_NONE);
    assert(nexus_installer_finish_apply(&context, false) ==
           NEXUS_INSTALLER_ERROR_NONE);
    assert(context.state == NEXUS_INSTALLER_FAILED);
}

int main(void) {
    test_full_confirmation_state_machine();
    test_refuses_live_mounted_or_unsupported_targets();
    test_changed_target_requires_recovery();
    test_cancel_and_failed_apply();
    puts("NEXUS INSTALLER CORE STATE MACHINE TESTS PASSED");
    return 0;
}
