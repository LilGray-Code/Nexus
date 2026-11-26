#include "gaming.h"
#include "ac97.h"
#include "cpu.h"
#include "gpu.h"
#include "graphics.h"
#include "vfs.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool audio_ready;
static uint64_t detected_cpu_features;
static uint64_t display_width;
static uint64_t display_height;
static struct nexus_gpu_info primary_gpu;
static struct nexus_gpu_runtime_capabilities gpu_runtime;
static bool nxfs_ready = true;
static char cache_path[NEXUS_VFS_NAME_MAX];
static uint8_t cache_bytes[NEXUS_GAMING_SHADER_CACHE_MAX_PAYLOAD + 80U];
static size_t cache_size;
static bool cache_present;

bool ac97_is_ready(void) { return audio_ready; }
bool nexus_cpu_has_feature(uint64_t feature) {
    return (detected_cpu_features & feature) == feature;
}
uint64_t graphics_width(void) { return display_width; }
uint64_t graphics_height(void) { return display_height; }
const struct nexus_gpu_info *gpu_primary_info(void) {
    return primary_gpu.present ? &primary_gpu : NULL;
}
void gpu_get_runtime_capabilities(
    struct nexus_gpu_runtime_capabilities *capabilities) {
    if (capabilities != NULL) *capabilities = gpu_runtime;
}

bool vfs_nxfs_writable(void) { return nxfs_ready; }
bool vfs_create_root_directory(const char *name) {
    return name != NULL && strcmp(name, "S") == 0 && nxfs_ready;
}
bool vfs_write_root_path(const char *path, const void *data, size_t length) {
    if (!nxfs_ready || path == NULL || data == NULL ||
        length > sizeof(cache_bytes) ||
        strlen(path) >= sizeof(cache_path)) return false;
    strcpy(cache_path, path);
    memcpy(cache_bytes, data, length);
    cache_size = length;
    cache_present = true;
    return true;
}
bool vfs_open_root_readonly_path(const char *path,
                                 struct nexus_vfs_file *file) {
    if (path == NULL || file == NULL || !cache_present ||
        strcmp(path, cache_path) != 0) return false;
    *file = (struct nexus_vfs_file){0};
    file->filesystem_entry = cache_bytes;
    file->open = true;
    file->nxfs_file = true;
    return true;
}
bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || !file->open || file->filesystem_entry != cache_bytes ||
        size == NULL) return false;
    *size = (uint32_t)cache_size;
    return true;
}
bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (file == NULL || !file->open || file->filesystem_entry != cache_bytes ||
        bytes_read == NULL || (requested != 0U && buffer == NULL) ||
        file->offset > cache_size || requested > cache_size - file->offset)
        return false;
    memcpy(buffer, cache_bytes + file->offset, requested);
    file->offset += (uint32_t)requested;
    *bytes_read = requested;
    return true;
}
void vfs_close(struct nexus_vfs_file *file) {
    if (file != NULL) *file = (struct nexus_vfs_file){0};
}
bool vfs_remove_root_path(const char *path) {
    if (!cache_present || path == NULL || strcmp(path, cache_path) != 0)
        return false;
    cache_present = false;
    cache_size = 0U;
    cache_path[0] = '\0';
    return true;
}

static void reset_fakes(void) {
    audio_ready = false;
    detected_cpu_features = 0U;
    display_width = 0U;
    display_height = 0U;
    primary_gpu = (struct nexus_gpu_info){0};
    gpu_runtime = (struct nexus_gpu_runtime_capabilities){0};
    nxfs_ready = true;
    cache_path[0] = '\0';
    cache_size = 0U;
    cache_present = false;
}

static void test_system_capabilities_are_truthful(void) {
    reset_fakes();
    struct nexus_gaming_system_capabilities caps;
    nexus_gaming_query_system(&caps);
    assert((caps.available & NEXUS_GAME_CAP_NATIVE_STATIC_ELF) != 0U);
    assert((caps.available & NEXUS_GAME_CAP_USB_HID_GAMEPAD) != 0U);
    assert((caps.available & NEXUS_GAME_CAP_DISPLAY_2D) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_AUDIO_AC97_PCM) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_LINUX_SYSCALL_ABI) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_DYNAMIC_ELF_LINKING) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_STEAM_CLIENT) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_PROTON_RUNTIME) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_VULKAN) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_OPENGL) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_GPU_3D_RENDERING) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_CPU_CMPXCHG16B) == 0U);
    assert((caps.available & NEXUS_GAME_CAP_CPU_SSE3) == 0U);

    detected_cpu_features =
        NEXUS_CPU_FEATURE_CMPXCHG16B | NEXUS_CPU_FEATURE_SSE3;
    nexus_gaming_query_system(&caps);
    assert((caps.available & NEXUS_GAME_CAP_CPU_CMPXCHG16B) != 0U);
    assert((caps.available & NEXUS_GAME_CAP_CPU_SSE3) != 0U);

    audio_ready = true;
    display_width = 1920U;
    display_height = 1080U;
    nexus_gaming_query_system(&caps);
    assert((caps.available & NEXUS_GAME_CAP_AUDIO_AC97_PCM) != 0U);
    assert((caps.available & NEXUS_GAME_CAP_DISPLAY_2D) != 0U);
}

static void test_platform_requirements_and_directx_mapping(void) {
    const uint64_t steam =
        nexus_gaming_platform_requirements(
            NEXUS_GAME_PLATFORM_STEAM_LINUX,
            NEXUS_GAME_GRAPHICS_OPENGL);
    assert((steam & NEXUS_GAME_CAP_GLIBC_32) != 0U);
    assert((steam & NEXUS_GAME_CAP_GLIBC_64) != 0U);
    assert((steam & NEXUS_GAME_CAP_DYNAMIC_ELF_LINKING) != 0U);
    assert((steam & NEXUS_GAME_CAP_DBUS_SESSION) != 0U);
    assert((steam & NEXUS_GAME_CAP_X11_WSI) != 0U);
    assert((steam & NEXUS_GAME_CAP_AUDIO_SERVER) != 0U);

    const uint64_t dx11 = nexus_gaming_platform_requirements(
        NEXUS_GAME_PLATFORM_PROTON,
        NEXUS_GAME_GRAPHICS_DIRECT3D_11);
    assert((dx11 & NEXUS_GAME_CAP_DXVK) != 0U);
    assert((dx11 & NEXUS_GAME_CAP_VULKAN) != 0U);
    assert((dx11 & NEXUS_GAME_CAP_VULKAN_1_4) != 0U);
    assert((dx11 & NEXUS_GAME_CAP_GPU_3D_RENDERING) != 0U);

    const uint64_t dx12 = nexus_gaming_platform_requirements(
        NEXUS_GAME_PLATFORM_PROTON,
        NEXUS_GAME_GRAPHICS_DIRECT3D_12);
    assert((dx12 & NEXUS_GAME_CAP_VKD3D_PROTON) != 0U);
    assert((dx12 & NEXUS_GAME_CAP_VULKAN_1_3) != 0U);

    assert((nexus_gaming_platform_requirements(
        NEXUS_GAME_PLATFORM_PROTON,
        NEXUS_GAME_GRAPHICS_NONE) & NEXUS_GAME_CAP_OPENGL) != 0U);
    assert(nexus_gaming_platform_requirements(
        NEXUS_GAME_PLATFORM_NATIVE_NEXUS,
        NEXUS_GAME_GRAPHICS_DIRECT3D_11) == UINT64_MAX);

    struct nexus_game_descriptor game = {
        .platform = NEXUS_GAME_PLATFORM_NATIVE_NEXUS,
        .graphics_api = NEXUS_GAME_GRAPHICS_DIRECT3D_11,
        .anticheat = NEXUS_GAME_ANTICHEAT_NONE,
        .compatibility = NEXUS_GAME_COMPAT_UNTESTED
    };
    struct nexus_gaming_system_capabilities empty_system = {0};
    struct nexus_game_assessment invalid_assessment;
    assert(nexus_gaming_assess(&game, &empty_system, &invalid_assessment));
    assert(invalid_assessment.status == NEXUS_GAME_ASSESS_INVALID);

    game.graphics_api = NEXUS_GAME_GRAPHICS_NONE;
    game.compatibility = NEXUS_GAME_COMPAT_UNSUPPORTED;
    assert(nexus_gaming_assess(&game, &empty_system, &invalid_assessment));
    assert(invalid_assessment.status == NEXUS_GAME_ASSESS_TESTED_UNSUPPORTED);
}

static void test_assessment_never_confuses_missing_support_with_verified(void) {
    struct nexus_game_descriptor game = {
        .platform = NEXUS_GAME_PLATFORM_NATIVE_NEXUS,
        .graphics_api = NEXUS_GAME_GRAPHICS_NONE,
        .anticheat = NEXUS_GAME_ANTICHEAT_NONE,
        .compatibility = NEXUS_GAME_COMPAT_UNTESTED
    };
    struct nexus_gaming_system_capabilities system = {
        .available = NEXUS_GAME_CAP_NATIVE_STATIC_ELF |
                     NEXUS_GAME_CAP_DISPLAY_2D
    };
    struct nexus_game_assessment assessment;
    assert(nexus_gaming_assess(&game, &system, &assessment));
    assert(assessment.status == NEXUS_GAME_ASSESS_READY_TO_TEST);

    game.compatibility = NEXUS_GAME_COMPAT_VERIFIED;
    game.tested_capabilities = assessment.required_capabilities;
    game.tested_gpu_vendor_id = 0x1234U;
    game.tested_gpu_device_id = 0x5678U;
    game.tested_gpu_driver_version = 4U;
    system.gpu_vendor_id = 0x1234U;
    system.gpu_device_id = 0x5678U;
    system.gpu_driver_version = 4U;
    assert(nexus_gaming_assess(&game, &system, &assessment));
    assert(assessment.status == NEXUS_GAME_ASSESS_VERIFIED);

    system.gpu_driver_version = 5U;
    assert(nexus_gaming_assess(&game, &system, &assessment));
    assert(assessment.status == NEXUS_GAME_ASSESS_NEEDS_RETEST);

    game.compatibility = NEXUS_GAME_COMPAT_UNTESTED;
    game.platform = NEXUS_GAME_PLATFORM_STEAM_LINUX;
    game.graphics_api = NEXUS_GAME_GRAPHICS_OPENGL;
    assert(nexus_gaming_assess(&game, &system, &assessment));
    assert(assessment.status == NEXUS_GAME_ASSESS_MISSING_PREREQUISITES);
    assert((assessment.missing_capabilities &
            NEXUS_GAME_CAP_GLIBC_32) != 0U);
    assert((assessment.missing_capabilities &
            NEXUS_GAME_CAP_X11_WSI) != 0U);
}

static void test_anticheat_is_reported_as_a_hard_prerequisite(void) {
    struct nexus_game_descriptor game = {
        .platform = NEXUS_GAME_PLATFORM_PROTON,
        .graphics_api = NEXUS_GAME_GRAPHICS_DIRECT3D_11,
        .anticheat = NEXUS_GAME_ANTICHEAT_KERNEL,
        .compatibility = NEXUS_GAME_COMPAT_UNTESTED
    };
    struct nexus_gaming_system_capabilities system = {0};
    struct nexus_game_assessment assessment;
    assert(nexus_gaming_assess(&game, &system, &assessment));
    assert(assessment.status == NEXUS_GAME_ASSESS_BLOCKED_ANTICHEAT);

    game.anticheat = NEXUS_GAME_ANTICHEAT_UNKNOWN;
    assert(nexus_gaming_assess(&game, &system, &assessment));
    assert(assessment.status == NEXUS_GAME_ASSESS_BLOCKED_ANTICHEAT);
}

static struct nexus_game_shader_cache_identity make_cache_identity(void) {
    struct nexus_game_shader_cache_identity identity = {
        .game_id = "GAME_001",
        .game_build_id = "BUILD_01",
        .graphics_api = NEXUS_GAME_GRAPHICS_VULKAN,
        .translator_version = 2U,
        .shader_compiler_version = 3U,
        .gpu_vendor_id = 0x1002U,
        .gpu_device_id = 0x73BFU,
        .gpu_driver_version = 42U
    };
    for (size_t i = 0U; i < sizeof(identity.shader_source_hash); ++i)
        identity.shader_source_hash[i] = (uint8_t)i;
    return identity;
}

static void test_shader_cache_key_invalidation_inputs(void) {
    struct nexus_game_shader_cache_identity identity = make_cache_identity();
    uint8_t first[NEXUS_GAMING_CACHE_KEY_BYTES];
    uint8_t second[NEXUS_GAMING_CACHE_KEY_BYTES];
    assert(nexus_gaming_shader_cache_key(&identity, first));
    identity.gpu_driver_version++;
    assert(nexus_gaming_shader_cache_key(&identity, second));
    assert(memcmp(first, second, sizeof(first)) != 0);
    identity.gpu_driver_version--;
    identity.game_build_id[7] = '2';
    assert(nexus_gaming_shader_cache_key(&identity, second));
    assert(memcmp(first, second, sizeof(first)) != 0);
    identity.game_build_id[7] = '1';
    identity.game_id[4] = '/';
    assert(!nexus_gaming_shader_cache_key(&identity, second));
}

static void test_shader_cache_round_trip_and_corruption(void) {
    reset_fakes();
    struct nexus_game_shader_cache_identity identity = make_cache_identity();
    const uint8_t source_payload[] = {0U, 1U, 2U, 0xFEU, 0xFFU, 0x7AU};
    uint8_t loaded[sizeof(source_payload)] = {0};
    size_t payload_length = 0U;
    assert(nexus_gaming_shader_cache_store(
        &identity, source_payload, sizeof(source_payload)));
    assert(cache_present);
    assert(strncmp(cache_path, "S/", 2U) == 0);
    assert(strlen(cache_path) < NEXUS_VFS_NAME_MAX);
    assert(nexus_gaming_shader_cache_load(
        &identity, loaded, sizeof(loaded), &payload_length));
    assert(payload_length == sizeof(source_payload));
    assert(memcmp(loaded, source_payload, sizeof(loaded)) == 0);

    cache_bytes[cache_size - 1U] ^= 0x80U;
    assert(!nexus_gaming_shader_cache_load(
        &identity, loaded, sizeof(loaded), &payload_length));
    assert(!cache_present);
}

static void test_shader_cache_requires_persistent_nxfs(void) {
    reset_fakes();
    nxfs_ready = false;
    struct nexus_game_shader_cache_identity identity = make_cache_identity();
    const uint8_t payload[] = {1U, 2U, 3U};
    assert(!nexus_gaming_shader_cache_store(
        &identity, payload, sizeof(payload)));
}

int main(void) {
    test_system_capabilities_are_truthful();
    test_platform_requirements_and_directx_mapping();
    test_assessment_never_confuses_missing_support_with_verified();
    test_anticheat_is_reported_as_a_hard_prerequisite();
    test_shader_cache_key_invalidation_inputs();
    test_shader_cache_round_trip_and_corruption();
    test_shader_cache_requires_persistent_nxfs();
    puts("NEXUS GAMING CAPABILITY AND SHADER CACHE TESTS PASSED");
    return 0;
}
