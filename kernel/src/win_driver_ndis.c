#include "win_driver_ndis.h"

static bool valid_adapter(const struct nexus_ndis_adapter *adapter) {
    return adapter != NULL && adapter->generation != 0U;
}

static bool transition(struct nexus_ndis_adapter *adapter,
                       enum nexus_ndis_adapter_state expected,
                       enum nexus_ndis_adapter_state next) {
    if (!valid_adapter(adapter) || adapter->state != expected) {
        if (adapter != NULL) {
            adapter->failure = NEXUS_NDIS_FAILURE_BAD_STATE;
            adapter->last_status = 0U;
        }
        return false;
    }
    adapter->state = next;
    return true;
}

bool nexus_ndis_adapter_discover(struct nexus_ndis_adapter *adapter,
                                 uint32_t hardware_id) {
    if (adapter == NULL || hardware_id == 0U) return false;
    *adapter = (struct nexus_ndis_adapter){0};
    adapter->generation = 1U;
    adapter->hardware_id = hardware_id;
    adapter->state = NEXUS_NDIS_ADAPTER_DISCOVERED;
    return true;
}

bool nexus_ndis_adapter_validate_package(struct nexus_ndis_adapter *adapter,
                                         bool inf_matches, bool pe_valid,
                                         bool ndis6_candidate) {
    if (!transition(adapter, NEXUS_NDIS_ADAPTER_DISCOVERED,
                    NEXUS_NDIS_ADAPTER_PACKAGE_VALIDATED)) return false;
    if (!inf_matches || !pe_valid || !ndis6_candidate) {
        adapter->state = NEXUS_NDIS_ADAPTER_FAILED;
        adapter->failure = NEXUS_NDIS_FAILURE_PACKAGE;
        return false;
    }
    return true;
}

bool nexus_ndis_adapter_mark_image_mapped(struct nexus_ndis_adapter *adapter) {
    return transition(adapter, NEXUS_NDIS_ADAPTER_PACKAGE_VALIDATED,
                      NEXUS_NDIS_ADAPTER_IMAGE_MAPPED);
}

bool nexus_ndis_adapter_begin_initialize(struct nexus_ndis_adapter *adapter) {
    return transition(adapter, NEXUS_NDIS_ADAPTER_IMAGE_MAPPED,
                      NEXUS_NDIS_ADAPTER_INITIALIZING);
}

bool nexus_ndis_adapter_finish_initialize(struct nexus_ndis_adapter *adapter,
                                          uint32_t status) {
    if (!transition(adapter, NEXUS_NDIS_ADAPTER_INITIALIZING,
                    status == 0U ? NEXUS_NDIS_ADAPTER_RUNNING
                                 : NEXUS_NDIS_ADAPTER_FAILED)) return false;
    adapter->last_status = status;
    if (status != 0U) adapter->failure = NEXUS_NDIS_FAILURE_INITIALIZE;
    return status == 0U;
}

bool nexus_ndis_adapter_begin_halt(struct nexus_ndis_adapter *adapter) {
    return transition(adapter, NEXUS_NDIS_ADAPTER_RUNNING,
                      NEXUS_NDIS_ADAPTER_HALTING);
}

bool nexus_ndis_adapter_finish_halt(struct nexus_ndis_adapter *adapter,
                                    uint32_t status) {
    if (!transition(adapter, NEXUS_NDIS_ADAPTER_HALTING,
                    status == 0U ? NEXUS_NDIS_ADAPTER_STOPPED
                                 : NEXUS_NDIS_ADAPTER_FAILED)) return false;
    adapter->last_status = status;
    if (status != 0U) adapter->failure = NEXUS_NDIS_FAILURE_HALT;
    return status == 0U;
}

void nexus_ndis_adapter_fail(struct nexus_ndis_adapter *adapter,
                             enum nexus_ndis_failure failure,
                             uint32_t status) {
    if (adapter == NULL) return;
    adapter->state = NEXUS_NDIS_ADAPTER_FAILED;
    adapter->failure = failure == NEXUS_NDIS_FAILURE_NONE
        ? NEXUS_NDIS_FAILURE_BAD_STATE : failure;
    adapter->last_status = status;
}
