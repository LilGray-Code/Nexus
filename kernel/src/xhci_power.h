#ifndef NEXUS_XHCI_POWER_H
#define NEXUS_XHCI_POWER_H

#include <stdint.h>

#define NEXUS_XHCI_POWER_CMD_RUN (1U << 0)
#define NEXUS_XHCI_POWER_STATUS_HALTED (1U << 0)
#define NEXUS_XHCI_POWER_STATUS_CNR (1U << 11)

enum nexus_xhci_resume_action {
    NEXUS_XHCI_RESUME_RETAINED = 0,
    NEXUS_XHCI_RESUME_REINITIALIZE = 1
};

/* The controller can be trusted after resume only when it is running and
 * not halted or controller-not-ready. Any lost context requires rebuilding
 * DMA rings and device contexts before the next transfer is queued. */
static inline enum nexus_xhci_resume_action nexus_xhci_resume_decide(
    uint32_t command, uint32_t status) {
    if ((command & NEXUS_XHCI_POWER_CMD_RUN) == 0U ||
        (status & (NEXUS_XHCI_POWER_STATUS_HALTED |
                   NEXUS_XHCI_POWER_STATUS_CNR)) != 0U)
        return NEXUS_XHCI_RESUME_REINITIALIZE;
    return NEXUS_XHCI_RESUME_RETAINED;
}

#endif
