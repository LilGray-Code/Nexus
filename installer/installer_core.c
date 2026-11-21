#include "installer_core.h"

#include <stddef.h>
#include <stdint.h>

static size_t bounded_length(const char *value, size_t capacity) {
    if (value == NULL) return capacity;
    size_t length = 0U;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

static bool exact_string(const char *a, const char *b, size_t capacity) {
    if (a == NULL || b == NULL) return false;
    const size_t alen = bounded_length(a, capacity);
    const size_t blen = bounded_length(b, capacity);
    if (alen == 0U || alen >= capacity || alen != blen) return false;
    for (size_t i = 0U; i < alen; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

static bool valid_target(const struct nexus_installer_target *target) {
    return target != NULL &&
        bounded_length(target->stable_id, sizeof(target->stable_id)) > 0U &&
        bounded_length(target->stable_id, sizeof(target->stable_id)) <
            sizeof(target->stable_id) &&
        bounded_length(target->model, sizeof(target->model)) > 0U &&
        bounded_length(target->model, sizeof(target->model)) <
            sizeof(target->model) &&
        target->block_count > 0U &&
        (target->block_size == 512U || target->block_size == 4096U) &&
        !target->is_live_media && !target->is_mounted;
}

void nexus_installer_init(struct nexus_installer_context *context) {
    if (context == NULL) return;
    *context = (struct nexus_installer_context){0};
    context->state = NEXUS_INSTALLER_IDLE;
}

enum nexus_installer_error nexus_installer_select_target(
    struct nexus_installer_context *context,
    const struct nexus_installer_target *target) {
    if (context == NULL || context->state != NEXUS_INSTALLER_IDLE)
        return NEXUS_INSTALLER_ERROR_STATE;
    if (!valid_target(target)) return NEXUS_INSTALLER_ERROR_TARGET;
    context->target = *target;
    context->state = NEXUS_INSTALLER_TARGET_SELECTED;
    context->target_revalidated = false;
    return NEXUS_INSTALLER_ERROR_NONE;
}

enum nexus_installer_error nexus_installer_create_plan(
    struct nexus_installer_context *context,
    enum nexus_firmware_mode firmware) {
    if (context == NULL ||
        context->state != NEXUS_INSTALLER_TARGET_SELECTED)
        return NEXUS_INSTALLER_ERROR_STATE;
    if (!nexus_partition_plan_create(context->target.block_count,
                                     context->target.block_size,
                                     firmware, &context->plan))
        return NEXUS_INSTALLER_ERROR_PLAN;
    context->state = NEXUS_INSTALLER_PLAN_READY;
    return NEXUS_INSTALLER_ERROR_NONE;
}

enum nexus_installer_error nexus_installer_confirm_target(
    struct nexus_installer_context *context, const char *typed_stable_id) {
    if (context == NULL || context->state != NEXUS_INSTALLER_PLAN_READY)
        return NEXUS_INSTALLER_ERROR_STATE;
    if (!exact_string(context->target.stable_id, typed_stable_id,
                      sizeof(context->target.stable_id)))
        return NEXUS_INSTALLER_ERROR_CONFIRMATION;
    context->state = NEXUS_INSTALLER_TARGET_CONFIRMED;
    return NEXUS_INSTALLER_ERROR_NONE;
}

enum nexus_installer_error nexus_installer_confirm_destructive(
    struct nexus_installer_context *context, const char *typed_phrase) {
    if (context == NULL ||
        context->state != NEXUS_INSTALLER_TARGET_CONFIRMED)
        return NEXUS_INSTALLER_ERROR_STATE;
    if (!exact_string("ERASE", typed_phrase, 8U))
        return NEXUS_INSTALLER_ERROR_CONFIRMATION;
    context->state = NEXUS_INSTALLER_DESTRUCTIVE_CONFIRMED;
    return NEXUS_INSTALLER_ERROR_NONE;
}

enum nexus_installer_error nexus_installer_revalidate_target(
    struct nexus_installer_context *context,
    const struct nexus_installer_target *current_target) {
    if (context == NULL ||
        context->state != NEXUS_INSTALLER_DESTRUCTIVE_CONFIRMED)
        return NEXUS_INSTALLER_ERROR_STATE;
    if (!valid_target(current_target) ||
        !exact_string(context->target.stable_id, current_target->stable_id,
                      sizeof(context->target.stable_id)) ||
        !exact_string(context->target.model, current_target->model,
                      sizeof(context->target.model)) ||
        context->target.block_count != current_target->block_count ||
        context->target.block_size != current_target->block_size) {
        context->target_revalidated = false;
        context->state = NEXUS_INSTALLER_RECOVERY_REQUIRED;
        return NEXUS_INSTALLER_ERROR_CHANGED_TARGET;
    }
    context->target_revalidated = true;
    return NEXUS_INSTALLER_ERROR_NONE;
}

bool nexus_installer_can_apply(const struct nexus_installer_context *context) {
    return context != NULL &&
        context->state == NEXUS_INSTALLER_DESTRUCTIVE_CONFIRMED &&
        context->target_revalidated &&
        context->plan.disk_block_count == context->target.block_count &&
        context->plan.logical_block_size == context->target.block_size;
}

enum nexus_installer_error nexus_installer_begin_apply(
    struct nexus_installer_context *context) {
    if (!nexus_installer_can_apply(context))
        return NEXUS_INSTALLER_ERROR_STATE;
    context->state = NEXUS_INSTALLER_APPLYING;
    return NEXUS_INSTALLER_ERROR_NONE;
}

enum nexus_installer_error nexus_installer_finish_apply(
    struct nexus_installer_context *context, bool success) {
    if (context == NULL || context->state != NEXUS_INSTALLER_APPLYING)
        return NEXUS_INSTALLER_ERROR_STATE;
    context->state = success ? NEXUS_INSTALLER_COMPLETE
                             : NEXUS_INSTALLER_RECOVERY_REQUIRED;
    return NEXUS_INSTALLER_ERROR_NONE;
}

void nexus_installer_cancel(struct nexus_installer_context *context) {
    if (context == NULL) return;
    *context = (struct nexus_installer_context){0};
    context->state = NEXUS_INSTALLER_CANCELLED;
}
