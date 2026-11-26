#include "display.h"
#include "graphics.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

struct fake_backend {
    uint64_t current_width;
    uint64_t current_height;
    uint64_t fail_width;
    uint64_t fail_verify_width;
    bool fail_restore;
    uint32_t primary_monitor_id;
    bool fail_primary;
    unsigned test_calls;
    unsigned set_calls;
    unsigned verify_calls;
    unsigned restore_calls;
};

static bool fake_test(void *context, const struct nexus_display_mode *mode) {
    struct fake_backend *backend = context;
    ++backend->test_calls;
    return mode != NULL && mode->width != 0U && mode->height != 0U;
}

static bool fake_set(void *context, const struct nexus_display_mode *mode) {
    struct fake_backend *backend = context;
    ++backend->set_calls;
    if (mode == NULL) return false;
    if (mode->width == backend->fail_width) return false;
    backend->current_width = mode->width;
    backend->current_height = mode->height;
    return true;
}

static bool fake_restore(void *context, const struct nexus_display_mode *mode) {
    struct fake_backend *backend = context;
    ++backend->restore_calls;
    if (mode == NULL || backend->fail_restore) return false;
    backend->current_width = mode->width;
    backend->current_height = mode->height;
    return true;
}

static bool fake_verify(void *context,
                        const struct nexus_display_mode *mode) {
    struct fake_backend *backend = context;
    ++backend->verify_calls;
    return mode != NULL && mode->width != backend->fail_verify_width &&
           mode->width == backend->current_width &&
           mode->height == backend->current_height;
}

static bool fake_primary(void *context, uint32_t monitor_id) {
    struct fake_backend *backend = context;
    if (backend->fail_primary) return false;
    backend->primary_monitor_id = monitor_id;
    return true;
}

static bool fake_hdr(void *context, const struct nexus_display_mode *mode) {
    (void)context;
    return mode != NULL && mode->bits_per_pixel >= 30U;
}

static void set_checksum(uint8_t *block) {
    uint8_t sum = 0U;
    block[127] = 0U;
    for (size_t i = 0U; i < 127U; ++i) sum = (uint8_t)(sum + block[i]);
    block[127] = (uint8_t)(0U - sum);
}

static void make_1080p60_dtd(uint8_t *dtd) {
    memset(dtd, 0, 18U);
    dtd[0] = 0x02U;
    dtd[1] = 0x3AU; /* 148.50 MHz */
    dtd[2] = 0x80U; /* 1920 active */
    dtd[3] = 0x18U; /* 280 blank */
    dtd[4] = 0x71U;
    dtd[5] = 0x38U; /* 1080 active */
    dtd[6] = 0x2DU; /* 45 blank */
    dtd[7] = 0x40U;
    dtd[17] = 0x1AU;
}

int main(void) {
    uint8_t edid[256] = {0};
    static const uint8_t header[8] = {
        0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U
    };
    memcpy(edid, header, sizeof(header));
    edid[18] = 1U;
    edid[19] = 4U;
    edid[126] = 1U;

    /* Monitor name descriptor. */
    edid[54] = 0U;
    edid[55] = 0U;
    edid[56] = 0U;
    edid[57] = 0xFCU;
    memcpy(edid + 59, "NEXUS PANEL", 11U);
    edid[70] = '\n';
    /* Base EDID preferred detailed timing: 1920x1080 at 60 Hz. */
    make_1080p60_dtd(edid + 72U);
    set_checksum(edid);

    /* CTA extension with HDR Static Metadata data block. */
    edid[128] = 0x02U;
    edid[129] = 0x03U;
    edid[130] = 8U;
    edid[131] = 0U;
    edid[132] = 0xE3U; /* Extended tag, three following bytes. */
    edid[133] = 0x06U; /* HDR Static Metadata. */
    edid[134] = 0x0CU; /* PQ and HLG EOTFs. */
    edid[135] = 0x01U; /* Static metadata descriptor type 1. */
    set_checksum(edid + 128U);

    struct limine_video_mode mode_1080 = {
        .pitch = 1920U * 4U,
        .width = 1920U,
        .height = 1080U,
        .bpp = 32U
    };
    struct limine_video_mode mode_720 = {
        .pitch = 1280U * 4U,
        .width = 1280U,
        .height = 720U,
        .bpp = 32U
    };
    struct limine_video_mode *modes[] = {&mode_1080, &mode_720};
    uint32_t framebuffer[1024U * 768U] = {0};
    struct limine_framebuffer fb = {
        .address = framebuffer,
        .width = 1024U,
        .height = 768U,
        .pitch = 1024U * 4U,
        .bpp = 32U,
        .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8U,
        .green_mask_size = 8U,
        .blue_mask_size = 8U,
        .edid_size = sizeof(edid),
        .edid = edid,
        .mode_count = 2U,
        .modes = modes
    };

    assert(nexus_display_init(&fb));
    const struct nexus_display_info *info = nexus_display_get_info();
    assert(info != NULL && info->initialized);
    assert(info->edid_valid);
    assert(info->edid_extensions_complete);
    assert(info->monitor_name_valid);
    assert(strcmp(info->monitor_name, "NEXUS PANEL") == 0);
    assert(info->timing_count == 1U);
    assert(info->timings[0].width == 1920U);
    assert(info->timings[0].height == 1080U);
    assert(info->timings[0].refresh_hz == 60U);
    assert(nexus_display_hdr_monitor_supported());
    assert(!nexus_display_hdr_output_available());
    assert(info->firmware_mode_count == 2U);
    assert(info->mode_count == 3U); /* Two firmware modes plus active boot mode. */
    assert(info->modes[0].refresh_hz == 60U);
    assert(info->modes[0].edid_timing);
    assert(!info->mode_set_available);

    struct nexus_graphics_surface surface = {
        .address = framebuffer, .width = 1024U, .height = 768U,
        .pitch = 1024U * 4U, .bits_per_pixel = 32U,
        .red_mask_size = 8U, .red_mask_shift = 16U,
        .green_mask_size = 8U, .green_mask_shift = 8U,
        .blue_mask_size = 8U, .blue_mask_shift = 0U
    };
    assert(graphics_init(&surface));
    assert(graphics_width() == 1024U && graphics_height() == 768U);
    assert(nexus_display_set_rotation(NEXUS_DISPLAY_ROTATE_90));
    assert(graphics_width() == 768U && graphics_height() == 1024U);
    assert(nexus_display_set_scale_percent(150U));
    assert(graphics_width() == 512U && graphics_height() == 682U);
    assert(nexus_display_set_color_temperature(3000U));
    assert(nexus_display_set_brightness_percent(75U));
    assert(nexus_display_set_scale_percent(100U));
    assert(nexus_display_set_rotation(NEXUS_DISPLAY_ROTATE_0));
    assert(graphics_width() == 1024U && graphics_height() == 768U);
    assert(!nexus_display_set_scale_percent(20U));
    assert(!nexus_display_set_color_temperature(0U));

    assert(nexus_display_add_monitor(1U, true, 1024, 0,
                                    1920U, 1080U, edid, sizeof(edid)));
    assert(info->monitor_count == 2U);
    assert(nexus_display_monitor_at(1U) != NULL);
    assert(nexus_display_monitor_at(1U)->edid_valid);
    assert(nexus_display_monitor_at(1U)->dpi_x != 0U);
    assert(!nexus_display_set_primary_monitor(1U));
    assert(!nexus_display_set_primary_monitor(99U));
    assert(!nexus_display_topology_available());
    assert(!nexus_display_set_topology(NEXUS_DISPLAY_TOPOLOGY_MIRRORED));

    struct fake_backend fake = {
        .current_width = 1024U,
        .current_height = 768U,
        .fail_width = 1280U
    };
    const struct nexus_display_mode_backend backend = {
        .test_mode = fake_test,
        .set_mode = fake_set,
        .restore_mode = fake_restore,
        .verify_mode = fake_verify,
        .supports_hdr = fake_hdr,
        .set_primary_monitor = fake_primary
    };
    assert(nexus_display_register_mode_backend(&backend, &fake));
    assert(nexus_display_hdr_output_available());
    assert(nexus_display_set_primary_monitor(1U));
    assert(fake.primary_monitor_id == 1U);
    assert(nexus_display_monitor_at(0U)->primary == false);
    assert(nexus_display_monitor_at(1U)->primary);
    fake.fail_primary = true;
    assert(!nexus_display_set_primary_monitor(0U));
    assert(nexus_display_get_info()->primary_monitor_id == 1U);
    fake.fail_primary = false;
    assert(nexus_display_apply_mode(0U));
    info = nexus_display_get_info();
    assert(info->current_width == 1920U);
    assert(info->current_height == 1080U);
    assert(fake.current_width == 1920U);
    assert(fake.verify_calls == 1U);

    /* A set failure must restore and verify the prior mode. */
    assert(!nexus_display_apply_mode(1U));
    info = nexus_display_get_info();
    assert(info->current_width == 1920U);
    assert(fake.current_width == 1920U);
    assert(fake.restore_calls == 1U);
    assert(fake.verify_calls == 2U);
    assert(info->last_rollback_succeeded);
    assert(info->last_mode_change_failed);
    assert(info->last_failure_stage == NEXUS_DISPLAY_FAILURE_SET);
    assert(info->mode_set_available);

    /* A mode that fails post-set verification must also roll back. */
    fake.fail_width = 0U;
    fake.fail_verify_width = 1280U;
    assert(!nexus_display_apply_mode(1U));
    info = nexus_display_get_info();
    assert(info->current_width == 1920U);
    assert(fake.current_width == 1920U);
    assert(fake.restore_calls == 2U);
    assert(info->last_failure_stage == NEXUS_DISPLAY_FAILURE_VERIFY);
    assert(info->last_rollback_succeeded);

    /* An unverified rollback disables future mode changes. */
    fake.fail_verify_width = 0U;
    fake.fail_width = 1280U;
    fake.fail_restore = true;
    assert(!nexus_display_apply_mode(1U));
    info = nexus_display_get_info();
    assert(!info->last_rollback_succeeded);
    assert(info->last_failure_stage == NEXUS_DISPLAY_FAILURE_ROLLBACK);
    assert(!info->mode_set_available);
    assert(!nexus_display_apply_mode(0U));

    uint8_t bad_edid[128];
    memcpy(bad_edid, edid, sizeof(bad_edid));
    bad_edid[20] ^= 1U;
    fb.edid = bad_edid;
    fb.edid_size = sizeof(bad_edid);
    assert(nexus_display_init(&fb));
    info = nexus_display_get_info();
    assert(!info->edid_valid);
    assert(!nexus_display_hdr_monitor_supported());
    assert(!info->mode_set_available);

    fb.edid = NULL;
    fb.edid_size = 0U;
    fb.mode_count = 0U;
    fb.modes = NULL;
    assert(nexus_display_init(&fb));
    info = nexus_display_get_info();
    assert(info->mode_count == 1U);
    assert(info->modes[0].current_boot_mode);
    assert(info->current_refresh_hz == 0U);
    return 0;
}
