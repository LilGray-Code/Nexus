#ifndef NEXUS_GAMING_H
#define NEXUS_GAMING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_GAMING_CACHE_KEY_BYTES 32U
#define NEXUS_GAMING_SHADER_CACHE_MAX_PAYLOAD (256U * 1024U)

enum nexus_game_platform {
    NEXUS_GAME_PLATFORM_NATIVE_NEXUS = 1,
    NEXUS_GAME_PLATFORM_STEAM_LINUX,
    NEXUS_GAME_PLATFORM_PROTON,
    NEXUS_GAME_PLATFORM_WINE
};

enum nexus_game_graphics_api {
    NEXUS_GAME_GRAPHICS_NONE = 0,
    NEXUS_GAME_GRAPHICS_OPENGL,
    NEXUS_GAME_GRAPHICS_VULKAN,
    NEXUS_GAME_GRAPHICS_DIRECT3D_8_9,
    NEXUS_GAME_GRAPHICS_DIRECT3D_10,
    NEXUS_GAME_GRAPHICS_DIRECT3D_11,
    NEXUS_GAME_GRAPHICS_DIRECT3D_12
};

enum nexus_game_anticheat {
    NEXUS_GAME_ANTICHEAT_NONE = 0,
    NEXUS_GAME_ANTICHEAT_EASY,
    NEXUS_GAME_ANTICHEAT_BATTLEYE,
    NEXUS_GAME_ANTICHEAT_USERSPACE_OTHER,
    NEXUS_GAME_ANTICHEAT_KERNEL,
    NEXUS_GAME_ANTICHEAT_UNKNOWN
};

enum nexus_game_compatibility {
    NEXUS_GAME_COMPAT_UNTESTED = 0,
    NEXUS_GAME_COMPAT_VERIFIED,
    NEXUS_GAME_COMPAT_PLAYABLE_LIMITATIONS,
    NEXUS_GAME_COMPAT_UNSUPPORTED
};

enum nexus_game_assessment_status {
    NEXUS_GAME_ASSESS_INVALID = 0,
    NEXUS_GAME_ASSESS_READY_TO_TEST,
    NEXUS_GAME_ASSESS_VERIFIED,
    NEXUS_GAME_ASSESS_PLAYABLE_LIMITATIONS,
    NEXUS_GAME_ASSESS_MISSING_PREREQUISITES,
    NEXUS_GAME_ASSESS_BLOCKED_ANTICHEAT,
    NEXUS_GAME_ASSESS_NEEDS_RETEST,
    NEXUS_GAME_ASSESS_TESTED_UNSUPPORTED
};

/*
 * Capabilities describe implemented, usable interfaces, not detected
 * hardware. In particular, a VirtIO 2D scanout is not GPU 3D acceleration.
 */
#define NEXUS_GAME_CAP_NATIVE_STATIC_ELF       (1ULL << 0)
#define NEXUS_GAME_CAP_DISPLAY_2D               (1ULL << 1)
#define NEXUS_GAME_CAP_USB_HID_GAMEPAD          (1ULL << 2)
#define NEXUS_GAME_CAP_AUDIO_AC97_PCM           (1ULL << 3)
#define NEXUS_GAME_CAP_GPU_2D_RENDERING         (1ULL << 4)
#define NEXUS_GAME_CAP_GPU_3D_RENDERING         (1ULL << 5)
#define NEXUS_GAME_CAP_OPENGL                   (1ULL << 6)
#define NEXUS_GAME_CAP_VULKAN                   (1ULL << 7)
#define NEXUS_GAME_CAP_VULKAN_1_3               (1ULL << 8)
#define NEXUS_GAME_CAP_VULKAN_1_4               (1ULL << 9)
#define NEXUS_GAME_CAP_DXVK                     (1ULL << 10)
#define NEXUS_GAME_CAP_VKD3D_PROTON             (1ULL << 11)
#define NEXUS_GAME_CAP_LINUX_SYSCALL_ABI        (1ULL << 12)
#define NEXUS_GAME_CAP_POSIX_RUNTIME            (1ULL << 13)
#define NEXUS_GAME_CAP_GLIBC_32                 (1ULL << 14)
#define NEXUS_GAME_CAP_GLIBC_64                 (1ULL << 15)
#define NEXUS_GAME_CAP_DYNAMIC_ELF_LINKING      (1ULL << 16)
#define NEXUS_GAME_CAP_MULTILIB_GRAPHICS        (1ULL << 17)
#define NEXUS_GAME_CAP_DBUS_SESSION             (1ULL << 18)
#define NEXUS_GAME_CAP_X11_WSI                  (1ULL << 19)
#define NEXUS_GAME_CAP_AUDIO_SERVER             (1ULL << 20)
#define NEXUS_GAME_CAP_NETWORK_SOCKETS           (1ULL << 21)
#define NEXUS_GAME_CAP_STEAM_RUNTIME             (1ULL << 22)
#define NEXUS_GAME_CAP_STEAM_CLIENT              (1ULL << 23)
#define NEXUS_GAME_CAP_WINE_RUNTIME              (1ULL << 24)
#define NEXUS_GAME_CAP_PROTON_RUNTIME            (1ULL << 25)
#define NEXUS_GAME_CAP_USERSPACE_ANTICHEAT        (1ULL << 26)
#define NEXUS_GAME_CAP_EASY_ANTICHEAT_PROTON      (1ULL << 27)
#define NEXUS_GAME_CAP_BATTLEYE_PROTON            (1ULL << 28)
#define NEXUS_GAME_CAP_KERNEL_ANTICHEAT           (1ULL << 29)
#define NEXUS_GAME_CAP_CPU_CMPXCHG16B             (1ULL << 30)
#define NEXUS_GAME_CAP_CPU_SSE3                   (1ULL << 31)
#define NEXUS_GAME_CAP_GPU_FENCE_SYNC             (1ULL << 32)
#define NEXUS_GAME_CAP_USER_THREADS               (1ULL << 33)
#define NEXUS_GAME_CAP_USER_PROCESS_MULTITASKING  (1ULL << 34)
#define NEXUS_GAME_CAP_VIDEO_DECODE               (1ULL << 35)

struct nexus_gaming_system_capabilities {
    uint64_t available;
    uint16_t gpu_vendor_id;
    uint16_t gpu_device_id;
    uint32_t gpu_driver_version;
    uint32_t vulkan_api_version; /* Vulkan packed version, e.g. VK_MAKE_API_VERSION */
    uint32_t opengl_major;
    uint32_t opengl_minor;
    uint64_t dedicated_video_memory_bytes;
};

struct nexus_game_descriptor {
    enum nexus_game_platform platform;
    enum nexus_game_graphics_api graphics_api;
    enum nexus_game_anticheat anticheat;
    enum nexus_game_compatibility compatibility;
    uint64_t additional_required_capabilities;
    uint64_t tested_capabilities;
    uint16_t tested_gpu_vendor_id;
    uint16_t tested_gpu_device_id;
    uint32_t tested_gpu_driver_version;
};

struct nexus_game_assessment {
    enum nexus_game_assessment_status status;
    uint64_t required_capabilities;
    uint64_t missing_capabilities;
};

struct nexus_game_shader_cache_identity {
    char game_id[32];
    char game_build_id[32];
    uint32_t graphics_api;
    uint32_t translator_version;
    uint32_t shader_compiler_version;
    uint16_t gpu_vendor_id;
    uint16_t gpu_device_id;
    uint32_t gpu_driver_version;
    uint8_t shader_source_hash[32];
};

/* Reports interfaces actually implemented by the current kernel build. */
void nexus_gaming_query_system(
    struct nexus_gaming_system_capabilities *capabilities);
uint64_t nexus_gaming_platform_requirements(
    enum nexus_game_platform platform,
    enum nexus_game_graphics_api graphics_api);
bool nexus_gaming_assess(
    const struct nexus_game_descriptor *game,
    const struct nexus_gaming_system_capabilities *system,
    struct nexus_game_assessment *assessment);
const char *nexus_game_assessment_name(
    enum nexus_game_assessment_status status);

/*
 * Shader cache identity includes game build, API translator/compiler version
 * and exact GPU/driver identity. Stale binaries must never cross those keys.
 * The persistent cache is NXFS-only and stores bounded individual blobs.
 */
bool nexus_gaming_shader_cache_key(
    const struct nexus_game_shader_cache_identity *identity,
    uint8_t key[NEXUS_GAMING_CACHE_KEY_BYTES]);
bool nexus_gaming_shader_cache_store(
    const struct nexus_game_shader_cache_identity *identity,
    const void *payload, size_t payload_length);
bool nexus_gaming_shader_cache_load(
    const struct nexus_game_shader_cache_identity *identity,
    void *payload, size_t payload_capacity, size_t *payload_length);
bool nexus_gaming_shader_cache_invalidate(
    const struct nexus_game_shader_cache_identity *identity);

#endif
