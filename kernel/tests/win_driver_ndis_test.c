#include "win_driver_ndis.h"

#include <assert.h>
#include <stdio.h>

static void test_successful_lifecycle(void) {
    struct nexus_ndis_adapter adapter;
    assert(nexus_ndis_adapter_discover(&adapter, 0x12348086U));
    assert(adapter.state == NEXUS_NDIS_ADAPTER_DISCOVERED);
    assert(nexus_ndis_adapter_validate_package(&adapter, true, true, true));
    assert(nexus_ndis_adapter_mark_image_mapped(&adapter));
    assert(nexus_ndis_adapter_begin_initialize(&adapter));
    assert(nexus_ndis_adapter_finish_initialize(&adapter, 0U));
    assert(adapter.state == NEXUS_NDIS_ADAPTER_RUNNING);
    assert(nexus_ndis_adapter_begin_halt(&adapter));
    assert(nexus_ndis_adapter_finish_halt(&adapter, 0U));
    assert(adapter.state == NEXUS_NDIS_ADAPTER_STOPPED);
}

static void test_rejects_unvalidated_or_unsupported_package(void) {
    struct nexus_ndis_adapter adapter;
    assert(nexus_ndis_adapter_discover(&adapter, 0x100E8086U));
    assert(!nexus_ndis_adapter_validate_package(&adapter, true, true, false));
    assert(adapter.state == NEXUS_NDIS_ADAPTER_FAILED);
    assert(adapter.failure == NEXUS_NDIS_FAILURE_PACKAGE);

    assert(nexus_ndis_adapter_discover(&adapter, 0x100E8086U));
    assert(!nexus_ndis_adapter_mark_image_mapped(&adapter));
    assert(adapter.failure == NEXUS_NDIS_FAILURE_BAD_STATE);
}

static void test_failure_and_recovery_boundary(void) {
    struct nexus_ndis_adapter adapter;
    assert(nexus_ndis_adapter_discover(&adapter, 0x12348086U));
    assert(nexus_ndis_adapter_validate_package(&adapter, true, true, true));
    assert(nexus_ndis_adapter_mark_image_mapped(&adapter));
    assert(nexus_ndis_adapter_begin_initialize(&adapter));
    assert(!nexus_ndis_adapter_finish_initialize(&adapter, 0xC0000001U));
    assert(adapter.state == NEXUS_NDIS_ADAPTER_FAILED);
    assert(adapter.failure == NEXUS_NDIS_FAILURE_INITIALIZE);
    assert(adapter.last_status == 0xC0000001U);
    assert(!nexus_ndis_adapter_begin_halt(&adapter));
}

int main(void) {
    test_successful_lifecycle();
    test_rejects_unvalidated_or_unsupported_package();
    test_failure_and_recovery_boundary();
    puts("Nexus NDIS lifecycle tests passed");
    return 0;
}
