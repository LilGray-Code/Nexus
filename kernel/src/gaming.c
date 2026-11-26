#include "gaming.h"

#include "ac97.h"
#include "cpu.h"
#include "gpu.h"
#include "graphics.h"
#include "sha256.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define SHADER_CACHE_MAGIC "NXSCACHE"
#define SHADER_CACHE_VERSION 1U
#define SHADER_CACHE_HEADER_SIZE 80U
#define SHADER_CACHE_PATH_MAX 31U
#define SHADER_CACHE_DIRECTORY "S"
#define SHADER_CACHE_MAX_FILE_BYTES \
    (SHADER_CACHE_HEADER_SIZE + NEXUS_GAMING_SHADER_CACHE_MAX_PAYLOAD)

static uint8_t shader_cache_scratch[SHADER_CACHE_MAX_FILE_BYTES];
static bool shader_cache_busy;

static bool text_is_terminated(const char *text, size_t capacity) {
    if (text == NULL || capacity == 0U) return false;
    for (size_t i = 0U; i < capacity; ++i)
        if (text[i] == '\0') return true;
    return false;
}

static bool text_is_safe_identifier(const char *text, size_t capacity) {
    if (!text_is_terminated(text, capacity) || text[0] == '\0') return false;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        const char c = text[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void nexus_gaming_query_system(
    struct nexus_gaming_system_capabilities *capabilities) {
    if (capabilities == NULL) return;
    *capabilities = (struct nexus_gaming_system_capabilities){0};

    /* Nexus currently loads bounded static native ELF applications. */
    capabilities->available |= NEXUS_GAME_CAP_NATIVE_STATIC_ELF;
    /* The USB HID parser and gamepad event queue are implemented. */
    capabilities->available |= NEXUS_GAME_CAP_USB_HID_GAMEPAD;

    if (graphics_width() != 0U && graphics_height() != 0U)
        capabilities->available |= NEXUS_GAME_CAP_DISPLAY_2D;
    if (ac97_is_ready())
        capabilities->available |= NEXUS_GAME_CAP_AUDIO_AC97_PCM;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_CMPXCHG16B))
        capabilities->available |= NEXUS_GAME_CAP_CPU_CMPXCHG16B;
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE3))
        capabilities->available |= NEXUS_GAME_CAP_CPU_SSE3;

    const struct nexus_gpu_info *primary = gpu_primary_info();
    if (primary != NULL) {
        capabilities->gpu_vendor_id = primary->vendor_id;
        capabilities->gpu_device_id = primary->device_id;
    }
    struct nexus_gpu_runtime_capabilities runtime = {0};
    gpu_get_runtime_capabilities(&runtime);
    if (runtime.hardware_2d_rendering)
        capabilities->available |= NEXUS_GAME_CAP_GPU_2D_RENDERING;
    if (runtime.hardware_3d_rendering)
        capabilities->available |= NEXUS_GAME_CAP_GPU_3D_RENDERING;
    if (runtime.opengl_available)
        capabilities->available |= NEXUS_GAME_CAP_OPENGL;
    if (runtime.vulkan_available)
        capabilities->available |= NEXUS_GAME_CAP_VULKAN;
    if (runtime.video_decode_available)
        capabilities->available |= NEXUS_GAME_CAP_VIDEO_DECODE;
    /*
     * Intentionally do not infer Linux ABI, glibc, dynamic linking, Steam,
     * Wine/Proton, sockets, OpenGL, Vulkan, DXVK or VKD3D from PCI inventory.
     * Those capability bits remain clear until the actual stack exists.
     */
}

static uint64_t graphics_api_requirements(
    enum nexus_game_graphics_api graphics_api) {
    switch (graphics_api) {
        case NEXUS_GAME_GRAPHICS_NONE:
            return 0U;
        case NEXUS_GAME_GRAPHICS_OPENGL:
            return NEXUS_GAME_CAP_OPENGL;
        case NEXUS_GAME_GRAPHICS_VULKAN:
            return NEXUS_GAME_CAP_VULKAN;
        case NEXUS_GAME_GRAPHICS_DIRECT3D_8_9:
        case NEXUS_GAME_GRAPHICS_DIRECT3D_10:
        case NEXUS_GAME_GRAPHICS_DIRECT3D_11:
            return NEXUS_GAME_CAP_DXVK | NEXUS_GAME_CAP_VULKAN |
                   NEXUS_GAME_CAP_GPU_3D_RENDERING;
        case NEXUS_GAME_GRAPHICS_DIRECT3D_12:
            return NEXUS_GAME_CAP_VKD3D_PROTON |
                   NEXUS_GAME_CAP_VULKAN |
                   NEXUS_GAME_CAP_VULKAN_1_3 |
                   NEXUS_GAME_CAP_GPU_3D_RENDERING;
        default:
            return UINT64_MAX;
    }
}

uint64_t nexus_gaming_platform_requirements(
    enum nexus_game_platform platform,
    enum nexus_game_graphics_api graphics_api) {
    const uint64_t steam_requirements =
        NEXUS_GAME_CAP_LINUX_SYSCALL_ABI |
        NEXUS_GAME_CAP_POSIX_RUNTIME |
        NEXUS_GAME_CAP_GLIBC_32 |
        NEXUS_GAME_CAP_GLIBC_64 |
        NEXUS_GAME_CAP_DYNAMIC_ELF_LINKING |
        NEXUS_GAME_CAP_MULTILIB_GRAPHICS |
        NEXUS_GAME_CAP_DBUS_SESSION |
        NEXUS_GAME_CAP_X11_WSI |
        NEXUS_GAME_CAP_AUDIO_SERVER |
        NEXUS_GAME_CAP_NETWORK_SOCKETS |
        NEXUS_GAME_CAP_STEAM_RUNTIME |
        NEXUS_GAME_CAP_CPU_CMPXCHG16B |
        NEXUS_GAME_CAP_CPU_SSE3;
    const uint64_t graphics = graphics_api_requirements(graphics_api);
    if (graphics == UINT64_MAX) return UINT64_MAX;

    switch (platform) {
        case NEXUS_GAME_PLATFORM_NATIVE_NEXUS:
            if (graphics_api >= NEXUS_GAME_GRAPHICS_DIRECT3D_8_9)
                return UINT64_MAX;
            return NEXUS_GAME_CAP_NATIVE_STATIC_ELF | graphics;
        case NEXUS_GAME_PLATFORM_STEAM_LINUX:
            if (graphics_api >= NEXUS_GAME_GRAPHICS_DIRECT3D_8_9)
                return UINT64_MAX;
            return steam_requirements |
                   NEXUS_GAME_CAP_STEAM_CLIENT |
                   NEXUS_GAME_CAP_OPENGL | graphics;
        case NEXUS_GAME_PLATFORM_PROTON:
            return steam_requirements |
                   NEXUS_GAME_CAP_STEAM_CLIENT |
                   NEXUS_GAME_CAP_WINE_RUNTIME |
                   NEXUS_GAME_CAP_PROTON_RUNTIME |
                   NEXUS_GAME_CAP_VULKAN |
                   NEXUS_GAME_CAP_GPU_3D_RENDERING |
                   NEXUS_GAME_CAP_GPU_FENCE_SYNC | graphics;
        case NEXUS_GAME_PLATFORM_WINE:
            return NEXUS_GAME_CAP_LINUX_SYSCALL_ABI |
                   NEXUS_GAME_CAP_POSIX_RUNTIME |
                   NEXUS_GAME_CAP_GLIBC_64 |
                   NEXUS_GAME_CAP_DYNAMIC_ELF_LINKING |
                   NEXUS_GAME_CAP_WINE_RUNTIME | graphics;
        default:
            return UINT64_MAX;
    }
}

static uint64_t anticheat_requirements(enum nexus_game_anticheat anticheat,
                                       enum nexus_game_platform platform) {
    switch (anticheat) {
        case NEXUS_GAME_ANTICHEAT_NONE: return 0U;
        case NEXUS_GAME_ANTICHEAT_EASY:
            return platform == NEXUS_GAME_PLATFORM_PROTON
                ? NEXUS_GAME_CAP_EASY_ANTICHEAT_PROTON
                : NEXUS_GAME_CAP_USERSPACE_ANTICHEAT;
        case NEXUS_GAME_ANTICHEAT_BATTLEYE:
            return platform == NEXUS_GAME_PLATFORM_PROTON
                ? NEXUS_GAME_CAP_BATTLEYE_PROTON
                : NEXUS_GAME_CAP_USERSPACE_ANTICHEAT;
        case NEXUS_GAME_ANTICHEAT_USERSPACE_OTHER:
            return NEXUS_GAME_CAP_USERSPACE_ANTICHEAT;
        case NEXUS_GAME_ANTICHEAT_KERNEL:
            return NEXUS_GAME_CAP_KERNEL_ANTICHEAT;
        case NEXUS_GAME_ANTICHEAT_UNKNOWN:
        default:
            return UINT64_MAX;
    }
}

bool nexus_gaming_assess(
    const struct nexus_game_descriptor *game,
    const struct nexus_gaming_system_capabilities *system,
    struct nexus_game_assessment *assessment) {
    if (assessment == NULL) return false;
    *assessment = (struct nexus_game_assessment){
        .status = NEXUS_GAME_ASSESS_INVALID
    };
    if (game == NULL || system == NULL ||
        game->platform < NEXUS_GAME_PLATFORM_NATIVE_NEXUS ||
        game->platform > NEXUS_GAME_PLATFORM_WINE ||
        game->graphics_api > NEXUS_GAME_GRAPHICS_DIRECT3D_12 ||
        game->anticheat > NEXUS_GAME_ANTICHEAT_UNKNOWN ||
        game->compatibility > NEXUS_GAME_COMPAT_UNSUPPORTED)
        return false;

    uint64_t required = nexus_gaming_platform_requirements(
        game->platform, game->graphics_api);
    const uint64_t anti_cheat =
        anticheat_requirements(game->anticheat, game->platform);
    if (required == UINT64_MAX || anti_cheat == UINT64_MAX ||
        (anti_cheat != 0U &&
         (anti_cheat & ~system->available) != 0U)) {
        assessment->status = NEXUS_GAME_ASSESS_BLOCKED_ANTICHEAT;
        return true;
    }
    required |= game->additional_required_capabilities | anti_cheat;
    assessment->required_capabilities = required;
    assessment->missing_capabilities = required & ~system->available;

    if (assessment->missing_capabilities != 0U) {
        assessment->status = NEXUS_GAME_ASSESS_MISSING_PREREQUISITES;
        return true;
    }

    if (game->compatibility == NEXUS_GAME_COMPAT_UNSUPPORTED) {
        assessment->status = NEXUS_GAME_ASSESS_TESTED_UNSUPPORTED;
        return true;
    }

    if (game->compatibility == NEXUS_GAME_COMPAT_UNTESTED) {
        assessment->status = NEXUS_GAME_ASSESS_READY_TO_TEST;
        return true;
    }

    if ((game->tested_capabilities & required) != required ||
        game->tested_gpu_vendor_id != system->gpu_vendor_id ||
        game->tested_gpu_device_id != system->gpu_device_id ||
        game->tested_gpu_driver_version != system->gpu_driver_version) {
        assessment->status = NEXUS_GAME_ASSESS_NEEDS_RETEST;
        return true;
    }

    assessment->status =
        game->compatibility == NEXUS_GAME_COMPAT_VERIFIED
            ? NEXUS_GAME_ASSESS_VERIFIED
            : NEXUS_GAME_ASSESS_PLAYABLE_LIMITATIONS;
    return true;
}

const char *nexus_game_assessment_name(
    enum nexus_game_assessment_status status) {
    switch (status) {
        case NEXUS_GAME_ASSESS_READY_TO_TEST: return "READY TO TEST";
        case NEXUS_GAME_ASSESS_VERIFIED: return "VERIFIED ON THIS CONFIGURATION";
        case NEXUS_GAME_ASSESS_PLAYABLE_LIMITATIONS:
            return "PLAYABLE WITH REPORTED LIMITATIONS";
        case NEXUS_GAME_ASSESS_MISSING_PREREQUISITES:
            return "MISSING REQUIRED RUNTIME OR DRIVER";
        case NEXUS_GAME_ASSESS_BLOCKED_ANTICHEAT:
            return "ANTI-CHEAT SUPPORT UNAVAILABLE OR UNKNOWN";
        case NEXUS_GAME_ASSESS_NEEDS_RETEST:
            return "RETEST REQUIRED FOR THIS HARDWARE/DRIVER";
        case NEXUS_GAME_ASSESS_TESTED_UNSUPPORTED:
            return "TESTED AND UNSUPPORTED";
        case NEXUS_GAME_ASSESS_INVALID:
        default:
            return "INVALID GAME PROFILE";
    }
}

bool nexus_gaming_shader_cache_key(
    const struct nexus_game_shader_cache_identity *identity,
    uint8_t key[NEXUS_GAMING_CACHE_KEY_BYTES]) {
    if (identity == NULL || key == NULL ||
        !text_is_safe_identifier(identity->game_id,
                                 sizeof(identity->game_id)) ||
        !text_is_safe_identifier(identity->game_build_id,
                                 sizeof(identity->game_build_id)) ||
        identity->graphics_api > NEXUS_GAME_GRAPHICS_DIRECT3D_12)
        return false;

    static const uint8_t domain[] = "NEXUS-GAME-SHADER-CACHE/1";
    struct nexus_sha256_context sha;
    nexus_sha256_init(&sha);
    nexus_sha256_update(&sha, domain, sizeof(domain));
    nexus_sha256_update(&sha, identity->game_id,
                        sizeof(identity->game_id));
    nexus_sha256_update(&sha, identity->game_build_id,
                        sizeof(identity->game_build_id));

    uint8_t numeric[24];
    write_le32(numeric, identity->graphics_api);
    write_le32(numeric + 4U, identity->translator_version);
    write_le32(numeric + 8U, identity->shader_compiler_version);
    write_le16(numeric + 12U, identity->gpu_vendor_id);
    write_le16(numeric + 14U, identity->gpu_device_id);
    write_le32(numeric + 16U, identity->gpu_driver_version);
    write_le32(numeric + 20U, 0U);
    nexus_sha256_update(&sha, numeric, sizeof(numeric));
    nexus_sha256_update(&sha, identity->shader_source_hash,
                        sizeof(identity->shader_source_hash));
    nexus_sha256_final(&sha, key);
    return true;
}

static bool shader_cache_path(
    const uint8_t key[NEXUS_GAMING_CACHE_KEY_BYTES],
    char path[SHADER_CACHE_PATH_MAX]) {
    static const char hex[] = "0123456789abcdef";
    if (key == NULL || path == NULL) return false;
    path[0] = 'S';
    path[1] = '/';
    for (size_t i = 0U; i < 12U; ++i) {
        path[2U + i * 2U] = hex[key[i] >> 4];
        path[3U + i * 2U] = hex[key[i] & 0x0FU];
    }
    path[26] = '.';
    path[27] = 'S';
    path[28] = 'C';
    path[29] = '\0';
    return true;
}

static bool shader_cache_header_valid(
    const uint8_t *bytes, size_t length,
    const uint8_t expected_key[NEXUS_GAMING_CACHE_KEY_BYTES],
    uint32_t *payload_length) {
    if (bytes == NULL || expected_key == NULL ||
        length < SHADER_CACHE_HEADER_SIZE ||
        payload_length == NULL) return false;
    static const uint8_t magic[8] = {
        'N', 'X', 'S', 'C', 'A', 'C', 'H', 'E'
    };
    for (size_t i = 0U; i < sizeof(magic); ++i)
        if (bytes[i] != magic[i]) return false;
    if (read_le32(bytes + 8U) != SHADER_CACHE_VERSION) return false;
    const uint32_t size = read_le32(bytes + 12U);
    if (size > NEXUS_GAMING_SHADER_CACHE_MAX_PAYLOAD ||
        length != SHADER_CACHE_HEADER_SIZE + size) return false;
    for (size_t i = 0U; i < NEXUS_GAMING_CACHE_KEY_BYTES; ++i)
        if (bytes[16U + i] != expected_key[i]) return false;
    uint8_t digest[NEXUS_SHA256_SIZE];
    nexus_sha256(bytes + SHADER_CACHE_HEADER_SIZE, size, digest);
    for (size_t i = 0U; i < sizeof(digest); ++i)
        if (bytes[48U + i] != digest[i]) return false;
    *payload_length = size;
    return true;
}

bool nexus_gaming_shader_cache_store(
    const struct nexus_game_shader_cache_identity *identity,
    const void *payload, size_t payload_length) {
    if (shader_cache_busy || payload == NULL || payload_length == 0U ||
        payload_length > NEXUS_GAMING_SHADER_CACHE_MAX_PAYLOAD ||
        !vfs_nxfs_writable()) return false;
    uint8_t key[NEXUS_GAMING_CACHE_KEY_BYTES];
    char path[SHADER_CACHE_PATH_MAX];
    if (!nexus_gaming_shader_cache_key(identity, key) ||
        !shader_cache_path(key, path)) return false;

    shader_cache_busy = true;
    bool result = false;
    if (!vfs_create_root_directory(SHADER_CACHE_DIRECTORY)) goto done;

    /* A short filename is used, but the full 256-bit key is checked to
     * prevent a truncated-name collision from replacing another cache entry.
     */
    struct nexus_vfs_file existing;
    if (vfs_open_root_readonly_path(path, &existing)) {
        uint32_t existing_size = 0U;
        uint8_t existing_header[SHADER_CACHE_HEADER_SIZE];
        size_t received = 0U;
        bool same_key = false;
        if (vfs_file_size(&existing, &existing_size) &&
            existing_size >= SHADER_CACHE_HEADER_SIZE &&
            vfs_read(&existing, existing_header,
                     sizeof(existing_header), &received) &&
            received == sizeof(existing_header)) {
            same_key = true;
            for (size_t i = 0U; i < NEXUS_GAMING_CACHE_KEY_BYTES; ++i)
                if (existing_header[16U + i] != key[i]) same_key = false;
        }
        vfs_close(&existing);
        if (!same_key) goto done;
    }

    for (size_t i = 0U; i < sizeof(SHADER_CACHE_MAGIC) - 1U; ++i)
        shader_cache_scratch[i] = (uint8_t)SHADER_CACHE_MAGIC[i];
    write_le32(shader_cache_scratch + 8U, SHADER_CACHE_VERSION);
    write_le32(shader_cache_scratch + 12U, (uint32_t)payload_length);
    for (size_t i = 0U; i < sizeof(key); ++i)
        shader_cache_scratch[16U + i] = key[i];
    nexus_sha256(payload, payload_length,
                 shader_cache_scratch + 48U);
    const uint8_t *input = (const uint8_t *)payload;
    for (size_t i = 0U; i < payload_length; ++i)
        shader_cache_scratch[SHADER_CACHE_HEADER_SIZE + i] = input[i];

    result = vfs_write_root_path(
        path, shader_cache_scratch,
        SHADER_CACHE_HEADER_SIZE + payload_length);

done:
    shader_cache_busy = false;
    return result;
}

bool nexus_gaming_shader_cache_load(
    const struct nexus_game_shader_cache_identity *identity,
    void *payload, size_t payload_capacity, size_t *payload_length) {
    if (payload_length == NULL || shader_cache_busy) return false;
    *payload_length = 0U;
    uint8_t key[NEXUS_GAMING_CACHE_KEY_BYTES];
    char path[SHADER_CACHE_PATH_MAX];
    if (!nexus_gaming_shader_cache_key(identity, key) ||
        !shader_cache_path(key, path)) return false;

    shader_cache_busy = true;
    bool result = false;
    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly_path(path, &file)) goto done;
    uint32_t file_size = 0U;
    size_t received = 0U;
    if (!vfs_file_size(&file, &file_size) ||
        file_size < SHADER_CACHE_HEADER_SIZE ||
        file_size > sizeof(shader_cache_scratch) ||
        !vfs_read(&file, shader_cache_scratch, file_size, &received) ||
        received != file_size) {
        vfs_close(&file);
        goto done;
    }
    vfs_close(&file);

    uint32_t required = 0U;
    if (!shader_cache_header_valid(shader_cache_scratch, file_size,
                                   key, &required)) {
        (void)vfs_remove_root_path(path);
        goto done;
    }
    *payload_length = required;
    if (required > payload_capacity || (required != 0U && payload == NULL))
        goto done;
    uint8_t *output = (uint8_t *)payload;
    for (size_t i = 0U; i < required; ++i)
        output[i] = shader_cache_scratch[SHADER_CACHE_HEADER_SIZE + i];
    result = true;

done:
    shader_cache_busy = false;
    return result;
}

bool nexus_gaming_shader_cache_invalidate(
    const struct nexus_game_shader_cache_identity *identity) {
    uint8_t key[NEXUS_GAMING_CACHE_KEY_BYTES];
    char path[SHADER_CACHE_PATH_MAX];
    if (!nexus_gaming_shader_cache_key(identity, key) ||
        !shader_cache_path(key, path)) return false;
    return vfs_remove_root_path(path);
}
