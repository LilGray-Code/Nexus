#include "xhci_power.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(nexus_xhci_resume_decide(NEXUS_XHCI_POWER_CMD_RUN, 0U) ==
           NEXUS_XHCI_RESUME_RETAINED);
    assert(nexus_xhci_resume_decide(0U, NEXUS_XHCI_POWER_STATUS_HALTED) ==
           NEXUS_XHCI_RESUME_REINITIALIZE);
    assert(nexus_xhci_resume_decide(0U, 0U) ==
           NEXUS_XHCI_RESUME_REINITIALIZE);
    assert(nexus_xhci_resume_decide(NEXUS_XHCI_POWER_CMD_RUN,
           NEXUS_XHCI_POWER_STATUS_HALTED) ==
           NEXUS_XHCI_RESUME_REINITIALIZE);
    assert(nexus_xhci_resume_decide(NEXUS_XHCI_POWER_CMD_RUN,
           NEXUS_XHCI_POWER_STATUS_CNR) ==
           NEXUS_XHCI_RESUME_REINITIALIZE);
    puts("NEXUS XHCI RESUME STATE DECISION TESTS PASSED");
    return 0;
}
