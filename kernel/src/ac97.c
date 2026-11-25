#include "ac97.h"

#include "memory.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define AC97_MIXER_RESET             0x00U
#define AC97_MIXER_MASTER_VOLUME     0x02U
#define AC97_MIXER_PCM_VOLUME        0x18U
#define AC97_BUSMASTER_GLOBAL_CTRL   0x2CU
#define AC97_BUSMASTER_GLOBAL_STATUS 0x30U
#define AC97_PCM_OUT_BASE            0x10U
#define AC97_PCM_OUT_BDBAR           0x00U
#define AC97_PCM_OUT_CIV              0x04U
#define AC97_PCM_OUT_LVI              0x05U
#define AC97_PCM_OUT_STATUS           0x06U
#define AC97_PCM_OUT_PICB             0x08U
#define AC97_PCM_OUT_PIV              0x0AU
#define AC97_PCM_OUT_CONTROL          0x0BU

#define AC97_GLOBAL_COLD_RESET        0x00000002U
#define AC97_CODEC_PRIMARY_READY      0x00000001U
#define AC97_STATUS_DCH               0x0001U
#define AC97_STATUS_ERROR             0x0010U
#define AC97_STATUS_W1C               0x001CU
#define AC97_CONTROL_RUN              0x01U
#define AC97_CONTROL_RESET             0x02U
#define AC97_DMA_LIMIT                0x100000000ULL
#define AC97_REGISTER_WINDOW          0x40U
#define AC97_RESET_SPIN_LIMIT         1000000U

static uint16_t mixer_base;
static uint16_t busmaster_base;
static uint64_t bdl_physical;
static uint64_t audio_physical;
static struct nexus_ac97_bdl_entry *bdl;
static int16_t *audio_buffer;
static bool initialized;
static bool playback_active;
static bool playback_started;
static bool playback_failed;

static inline void io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t io_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t io_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static bool io_window_valid(const struct nexus_pci_bar *bar) {
    return bar != NULL && bar->present && bar->assigned &&
        bar->io_space && bar->base != 0U &&
        bar->base <= (uint64_t)(UINT16_MAX - AC97_REGISTER_WINDOW);
}

static bool wait_codec_ready(void) {
    for (uint32_t i = 0U; i < AC97_RESET_SPIN_LIMIT; ++i) {
        if ((io_in32((uint16_t)(busmaster_base +
                AC97_BUSMASTER_GLOBAL_STATUS)) &
             AC97_CODEC_PRIMARY_READY) != 0U)
            return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool reset_pcm_output(void) {
    const uint16_t control_port = (uint16_t)(
        busmaster_base + AC97_PCM_OUT_BASE + AC97_PCM_OUT_CONTROL);
    io_out8(control_port, AC97_CONTROL_RESET);
    for (uint32_t i = 0U; i < AC97_RESET_SPIN_LIMIT; ++i) {
        if ((io_in8(control_port) & AC97_CONTROL_RESET) == 0U)
            return true;
        __asm__ volatile ("pause");
    }
    return false;
}

bool ac97_init(void) {
    initialized = false;
    playback_active = false;
    playback_started = false;
    playback_failed = false;
    mixer_base = 0U;
    busmaster_base = 0U;
    bdl = NULL;
    audio_buffer = NULL;
    bdl_physical = 0U;
    audio_physical = 0U;

    const struct nexus_pci_device *device = NULL;
    uint32_t start = 0U;
    while ((device = pci_find_class(0x04U, 0x01U, start, &start)) != NULL) {
        struct nexus_pci_bar mixer_bar;
        struct nexus_pci_bar busmaster_bar;
        if (pci_decode_bar(device, 0U, &mixer_bar) &&
            pci_decode_bar(device, 1U, &busmaster_bar) &&
            io_window_valid(&mixer_bar) && io_window_valid(&busmaster_bar)) {
            mixer_base = (uint16_t)mixer_bar.base;
            busmaster_base = (uint16_t)busmaster_bar.base;
            break;
        }
        ++start;
    }

    if (mixer_base == 0U || busmaster_base == 0U) {
        serial_write("AC97: no compatible PCI AC'97 I/O controller found\r\n");
        return false;
    }
    if (!pci_enable_io_bus_master(device)) {
        serial_write("AC97: PCI I/O and bus-master enable failed\r\n");
        return false;
    }

    io_out32((uint16_t)(busmaster_base + AC97_BUSMASTER_GLOBAL_CTRL),
             AC97_GLOBAL_COLD_RESET);
    if (!wait_codec_ready()) {
        serial_write("AC97: primary codec did not become ready\r\n");
        return false;
    }

    /* Reset codec mixer registers and select conservative PCM output volume. */
    io_out16((uint16_t)(mixer_base + AC97_MIXER_RESET), 0U);
    io_out16((uint16_t)(mixer_base + AC97_MIXER_MASTER_VOLUME), 0U);
    io_out16((uint16_t)(mixer_base + AC97_MIXER_PCM_VOLUME), 0U);

    if (!memory_alloc_dma_page(AC97_DMA_LIMIT, &bdl_physical) ||
        !memory_alloc_dma_page(AC97_DMA_LIMIT, &audio_physical)) {
        if (bdl_physical != 0U) (void)memory_free_page(bdl_physical);
        if (audio_physical != 0U) (void)memory_free_page(audio_physical);
        bdl_physical = 0U;
        audio_physical = 0U;
        serial_write("AC97: unable to allocate low DMA pages\r\n");
        return false;
    }
    bdl = (struct nexus_ac97_bdl_entry *)memory_phys_to_virt(bdl_physical);
    audio_buffer = (int16_t *)memory_phys_to_virt(audio_physical);
    if (bdl == NULL || audio_buffer == NULL) {
        (void)memory_free_page(bdl_physical);
        (void)memory_free_page(audio_physical);
        bdl_physical = 0U;
        audio_physical = 0U;
        bdl = NULL;
        audio_buffer = NULL;
        serial_write("AC97: DMA pages could not be mapped\r\n");
        return false;
    }

    for (size_t i = 0U; i < NEXUS_PAGE_SIZE / sizeof(uint32_t); ++i)
        ((uint32_t *)bdl)[i] = 0U;
    for (size_t i = 0U; i < NEXUS_AC97_MAX_BYTES / sizeof(int16_t); ++i)
        audio_buffer[i] = 0;

    if (!reset_pcm_output()) {
        serial_write("AC97: PCM output DMA reset timed out\r\n");
        return false;
    }
    io_out32((uint16_t)(busmaster_base + AC97_PCM_OUT_BASE +
                        AC97_PCM_OUT_BDBAR), (uint32_t)bdl_physical);
    initialized = true;
    serial_write("NEXUS AC97 AUDIO READY: 48 kHz S16 stereo DMA output\r\n");
    return true;
}

bool ac97_is_ready(void) {
    return initialized;
}

void ac97_poll(void) {
    if (!initialized || !playback_active) return;
    const uint16_t status_port = (uint16_t)(
        busmaster_base + AC97_PCM_OUT_BASE + AC97_PCM_OUT_STATUS);
    const uint16_t status = io_in16(status_port);
    const uint16_t pending = status & AC97_STATUS_W1C;
    if (pending != 0U) io_out16(status_port, pending);
    if ((status & AC97_STATUS_ERROR) != 0U) {
        playback_active = false;
        playback_failed = true;
        return;
    }
    if ((status & AC97_STATUS_DCH) == 0U) {
        playback_started = true;
    } else if (playback_started) {
        playback_active = false;
    }
}

enum nexus_ac97_submit_result ac97_write_pcm_s16_stereo_48k(
    const int16_t *interleaved_samples, uint32_t sample_frames) {
    if (!initialized) return NEXUS_AC97_SUBMIT_NOT_READY;
    if (interleaved_samples == NULL || sample_frames == 0U ||
        sample_frames > NEXUS_AC97_MAX_FRAMES)
        return NEXUS_AC97_SUBMIT_INVALID;

    ac97_poll();
    if (playback_active) return NEXUS_AC97_SUBMIT_BUSY;
    if (playback_failed) {
        playback_failed = false;
        if (!reset_pcm_output()) return NEXUS_AC97_SUBMIT_IO_ERROR;
    }

    const size_t sample_count = (size_t)sample_frames * 2U;
    for (size_t i = 0U; i < sample_count; ++i)
        audio_buffer[i] = interleaved_samples[i];

    if (!nexus_ac97_build_bdl_entry(audio_physical, sample_frames, &bdl[0]))
        return NEXUS_AC97_SUBMIT_INVALID;
    __asm__ volatile ("" : : : "memory");

    if (!reset_pcm_output()) return NEXUS_AC97_SUBMIT_IO_ERROR;
    io_out32((uint16_t)(busmaster_base + AC97_PCM_OUT_BASE +
                        AC97_PCM_OUT_BDBAR), (uint32_t)bdl_physical);
    io_out8((uint16_t)(busmaster_base + AC97_PCM_OUT_BASE +
                       AC97_PCM_OUT_LVI), 0U);
    io_out16((uint16_t)(busmaster_base + AC97_PCM_OUT_BASE +
                        AC97_PCM_OUT_STATUS), AC97_STATUS_W1C);
    io_out8((uint16_t)(busmaster_base + AC97_PCM_OUT_BASE +
                       AC97_PCM_OUT_CONTROL), AC97_CONTROL_RUN);
    playback_active = true;
    playback_started = false;
    playback_failed = false;
    return NEXUS_AC97_SUBMIT_OK;
}
