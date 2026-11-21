#ifndef NEXUS_INSTALLER_CORE_H
#define NEXUS_INSTALLER_CORE_H

#include "partition_plan.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_INSTALLER_ID_MAX 64U
#define NEXUS_INSTALLER_MODEL_MAX 64U

enum nexus_installer_state {
    NEXUS_INSTALLER_IDLE = 0,
    NEXUS_INSTALLER_TARGET_SELECTED,
    NEXUS_INSTALLER_PLAN_READY,
    NEXUS_INSTALLER_TARGET_CONFIRMED,
    NEXUS_INSTALLER_DESTRUCTIVE_CONFIRMED,
    NEXUS_INSTALLER_APPLYING,
    NEXUS_INSTALLER_RECOVERY_REQUIRED,
    NEXUS_INSTALLER_COMPLETE,
    NEXUS_INSTALLER_CANCELLED
};

struct nexus_installer_target {
    char stable_id[NEXUS_INSTALLER_ID_MAX];
    char model[NEXUS_INSTALLER_MODEL_MAX];
    uint64_t block_count;
    uint32_t block_size;
    bool is_live_media;
    bool is_mounted;
};

struct nexus_installer_context {
    enum nexus_installer_state state;
    struct nexus_installer_target target;
    struct nexus_partition_plan plan;
    bool target_revalidated;
};

enum nexus_installer_error {
    NEXUS_INSTALLER_ERROR_NONE = 0,
    NEXUS_INSTALLER_ERROR_STATE,
    NEXUS_INSTALLER_ERROR_TARGET,
    NEXUS_INSTALLER_ERROR_PLAN,
    NEXUS_INSTALLER_ERROR_CONFIRMATION,
    NEXUS_INSTALLER_ERROR_CHANGED_TARGET
};

void nexus_installer_init(struct nexus_installer_context *context);
enum nexus_installer_error nexus_installer_select_target(
    struct nexus_installer_context *context,
    const struct nexus_installer_target *target);
enum nexus_installer_error nexus_installer_create_plan(
    struct nexus_installer_context *context,
    enum nexus_firmware_mode firmware);
enum nexus_installer_error nexus_installer_confirm_target(
    struct nexus_installer_context *context, const char *typed_stable_id);
enum nexus_installer_error nexus_installer_confirm_destructive(
    struct nexus_installer_context *context, const char *typed_phrase);
enum nexus_installer_error nexus_installer_revalidate_target(
    struct nexus_installer_context *context,
    const struct nexus_installer_target *current_target);
bool nexus_installer_can_apply(const struct nexus_installer_context *context);
enum nexus_installer_error nexus_installer_begin_apply(
    struct nexus_installer_context *context);
enum nexus_installer_error nexus_installer_finish_apply(
    struct nexus_installer_context *context, bool success);
void nexus_installer_cancel(struct nexus_installer_context *context);

#endif
