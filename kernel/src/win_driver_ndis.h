#ifndef NEXUS_NDIS_H
#define NEXUS_NDIS_H

#include <stdbool.h>
#include <stdint.h>

/* Lifecycle contract for a future isolated NDIS miniport host.
 * These states do not load or execute Windows driver images.
 */
enum nexus_ndis_adapter_state {
    NEXUS_NDIS_ADAPTER_EMPTY = 0,
    NEXUS_NDIS_ADAPTER_DISCOVERED,
    NEXUS_NDIS_ADAPTER_PACKAGE_VALIDATED,
    NEXUS_NDIS_ADAPTER_IMAGE_MAPPED,
    NEXUS_NDIS_ADAPTER_INITIALIZING,
    NEXUS_NDIS_ADAPTER_RUNNING,
    NEXUS_NDIS_ADAPTER_HALTING,
    NEXUS_NDIS_ADAPTER_STOPPED,
    NEXUS_NDIS_ADAPTER_FAILED
};

enum nexus_ndis_failure {
    NEXUS_NDIS_FAILURE_NONE = 0,
    NEXUS_NDIS_FAILURE_BAD_STATE,
    NEXUS_NDIS_FAILURE_PACKAGE,
    NEXUS_NDIS_FAILURE_IMAGE,
    NEXUS_NDIS_FAILURE_INITIALIZE,
    NEXUS_NDIS_FAILURE_HALT
};

struct nexus_ndis_adapter {
    enum nexus_ndis_adapter_state state;
    enum nexus_ndis_failure failure;
    uint32_t generation;
    uint32_t hardware_id;
    uint32_t last_status;
};

bool nexus_ndis_adapter_discover(struct nexus_ndis_adapter *adapter,
                                 uint32_t hardware_id);
bool nexus_ndis_adapter_validate_package(struct nexus_ndis_adapter *adapter,
                                         bool inf_matches, bool pe_valid,
                                         bool ndis6_candidate);
bool nexus_ndis_adapter_mark_image_mapped(struct nexus_ndis_adapter *adapter);
bool nexus_ndis_adapter_begin_initialize(struct nexus_ndis_adapter *adapter);
bool nexus_ndis_adapter_finish_initialize(struct nexus_ndis_adapter *adapter,
                                          uint32_t status);
bool nexus_ndis_adapter_begin_halt(struct nexus_ndis_adapter *adapter);
bool nexus_ndis_adapter_finish_halt(struct nexus_ndis_adapter *adapter,
                                    uint32_t status);
void nexus_ndis_adapter_fail(struct nexus_ndis_adapter *adapter,
                             enum nexus_ndis_failure failure,
                             uint32_t status);

#endif
