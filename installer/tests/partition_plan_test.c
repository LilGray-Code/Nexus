#include "partition_plan.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_uefi_plan(void) {
    struct nexus_partition_plan plan;
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       512ULL, 512U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    assert(plan.partition_count == 3U);
    assert(plan.partitions[0].kind == NEXUS_PARTITION_ESP);
    assert(plan.partitions[0].last_lba - plan.partitions[0].first_lba + 1U ==
           (512ULL * 1024ULL * 1024ULL) / 512ULL);
    assert(plan.partitions[1].kind == NEXUS_PARTITION_SYSTEM);
    assert(plan.partitions[1].last_lba - plan.partitions[1].first_lba + 1U ==
           (8192ULL * 1024ULL * 1024ULL) / 512ULL);
    assert(plan.partitions[2].kind == NEXUS_PARTITION_DATA);
    for (size_t i = 0; i < plan.partition_count; ++i) {
        assert(plan.partitions[i].first_lba <= plan.partitions[i].last_lba);
        assert(plan.partitions[i].first_lba % 2048U == 0U);
        if (i != 0U)
            assert(plan.partitions[i - 1U].last_lba <
                   plan.partitions[i].first_lba);
    }
    assert(plan.partitions[2].last_lba <= plan.last_usable_lba);
}

static void test_bios_plan(void) {
    struct nexus_partition_plan plan;
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       512ULL, 512U,
                                       NEXUS_FIRMWARE_LEGACY_BIOS, &plan));
    assert(plan.partition_count == 3U);
    assert(plan.partitions[0].kind == NEXUS_PARTITION_BIOS_BOOT);
    assert(plan.partitions[0].last_lba - plan.partitions[0].first_lba + 1U ==
           (2ULL * 1024ULL * 1024ULL) / 512ULL);
    assert(plan.partitions[1].kind == NEXUS_PARTITION_SYSTEM);
    assert(plan.partitions[2].kind == NEXUS_PARTITION_DATA);
}

static void test_dual_firmware_plan(void) {
    struct nexus_partition_plan plan;
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
        512ULL,512U,NEXUS_FIRMWARE_DUAL,&plan));
    assert(plan.partition_count==4U);
    assert(plan.partitions[0].kind==NEXUS_PARTITION_ESP);
    assert(plan.partitions[1].kind==NEXUS_PARTITION_BIOS_BOOT);
    assert(plan.partitions[2].kind==NEXUS_PARTITION_SYSTEM);
    assert(plan.partitions[3].kind==NEXUS_PARTITION_DATA);
    for(size_t i=1U;i<plan.partition_count;i++)
        assert(plan.partitions[i-1U].last_lba<plan.partitions[i].first_lba);
}

static void test_4kn_plan(void) {
    struct nexus_partition_plan plan;
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       4096ULL, 4096U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    assert(plan.partitions[0].first_lba % 256U == 0U);
    assert(plan.partitions[0].last_lba - plan.partitions[0].first_lba + 1U ==
           (512ULL * 1024ULL * 1024ULL) / 4096ULL);
}

static void test_rejects_unsafe_inputs(void) {
    struct nexus_partition_plan plan;
    assert(!nexus_partition_plan_create(0U, 512U,
                                         NEXUS_FIRMWARE_UEFI, &plan));
    assert(!nexus_partition_plan_create(UINT64_MAX, 768U,
                                         NEXUS_FIRMWARE_UEFI, &plan));
    assert(!nexus_partition_plan_create(128U, 512U,
                                         NEXUS_FIRMWARE_UEFI, &plan));
    assert(!nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                        512ULL, 512U, 0, &plan));
    assert(plan.partition_count == 0U);
}

int main(void) {
    test_uefi_plan();
    test_bios_plan();
    test_dual_firmware_plan();
    test_4kn_plan();
    test_rejects_unsafe_inputs();
    puts("NEXUS INSTALLER PARTITION PLANNER TESTS PASSED");
    return 0;
}
