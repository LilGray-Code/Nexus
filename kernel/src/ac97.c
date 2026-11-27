#include "ac97.h"

#include "memory.h"
#include "pci.h"
#include "serial.h"
#include "timer.h"

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
/* ICH AC97 GLOB_STA.PCR: primary codec-ready status is bit 8. */
#define AC97_CODEC_PRIMARY_READY      (1U << 8)
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
static const struct nexus_pci_device *active_pci_device;
static bool playback_active;
static bool playback_started;
static bool playback_failed;
static uint64_t playback_start_tick;
static uint64_t playback_timeout_ticks;

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
    /*
     * Intel ICH AC97 requires up to 600 ms after GLOB_CNT.COLD_RESET for
     * the codec to initialize. The platform timer is already running here.
     * Enable interrupts around HLT so this wait works even when the caller
     * entered with IF clear.
     */
    const uint64_t start_tick = timer_ticks();
    uint32_t spin = 0U;
    while (timer_ticks() - start_tick < 60U &&
           spin < AC97_RESET_SPIN_LIMIT * 8U) {
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
        ++spin;
    }
    if (timer_ticks() - start_tick < 60U) return false;

    for (uint32_t i = 0U; i < AC97_RESET_SPIN_LIMIT; ++i) {
        if ((io_in32((uint16_t)(busmaster_base +
                AC97_BUSMASTER_GLOBAL_STATUS)) &
             AC97_CODEC_PRIMARY_READY) != 0U)
            return true;
        __asm__ volatile ("pause");
    }
    serial_write("AC97: primary codec-ready bit absent; GLOB_STA=");
    serial_write_hex(io_in32((uint16_t)(busmaster_base +
                          AC97_BUSMASTER_GLOBAL_STATUS)));
    serial_write("\r\n");
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
    active_pci_device = NULL;
    playback_active = false;
    playback_started = false;
    playback_failed = false;
    playback_start_tick = 0U;
    playback_timeout_ticks = 0U;
    mixer_base = 0U;
    busmaster_base = 0U;
    bdl = NULL;
    audio_buffer = NULL;
    bdl_physical = 0U;
    audio_physical = 0U;

    const struct nexus_pci_device *device = NULL;
    serial_write("AC97: PCI inventory entries=");
    serial_write_hex(pci_stored_device_count());
    serial_write("\\r\\n");
    for (uint32_t index = 0U; index < pci_stored_device_count(); ++index) {
        const struct nexus_pci_device *candidate = pci_device_at(index);
        if (candidate == NULL) continue;
        if (candidate->class_code == 0x04U) {
            serial_write("AC97: PCI multimedia candidate index/class/subclass=");
            serial_write_hex(index);
            serial_write("/");
            serial_write_hex(candidate->class_code);
            serial_write("/");
            serial_write_hex(candidate->subclass);
            serial_write("\\r\\n");
        }
        if (candidate->class_code != 0x04U ||
            candidate->subclass != 0x01U) continue;

        struct nexus_pci_bar mixer_bar;
        struct nexus_pci_bar busmaster_bar;
        const bool mixer_decoded = pci_decode_bar(candidate, 0U, &mixer_bar);
        const bool busmaster_decoded =
            pci_decode_bar(candidate, 1U, &busmaster_bar);
        serial_write("AC97: candidate PCI BAR0/BAR1 raw=");
        serial_write_hex(candidate->bars[0]);
        serial_write("/");
        serial_write_hex(candidate->bars[1]);
        serial_write(" decoded=");
        serial_write_hex((mixer_decoded ? 1U : 0U) |
                         (busmaster_decoded ? 2U : 0U));
        serial_write("\\r\\n");

        if (mixer_decoded && busmaster_decoded &&
            io_window_valid(&mixer_bar) && io_window_valid(&busmaster_bar)) {
            device = candidate;
            mixer_base = (uint16_t)mixer_bar.base;
            busmaster_base = (uint16_t)busmaster_bar.base;
            break;
        }
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
    io_out16((uint16_t)(mixer_base + AC97_MIXER_PCM_VOLUME), 0x0808U);

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
        (void)memory_free_page(bdl_physical);
        (void)memory_free_page(audio_physical);
        bdl_physical = 0U;
        audio_physical = 0U;
        bdl = NULL;
        audio_buffer = NULL;
        return false;
    }
    io_out32((uint16_t)(busmaster_base + AC97_PCM_OUT_BASE +
                        AC97_PCM_OUT_BDBAR), (uint32_t)bdl_physical);
    initialized = true;
    active_pci_device = device;
    serial_write("NEXUS AC97 AUDIO READY: 48 kHz S16 stereo DMA output\r\n");
    return true;
}

bool ac97_is_ready(void) {
    return initialized;
}

bool ac97_owns_pci_device(const struct nexus_pci_device *device) {
    return initialized && device != NULL && active_pci_device == device;
}

bool ac97_set_master_volume(uint8_t percent) {
    if (!initialized || percent > 100U) return false;
    const uint16_t port = (uint16_t)(mixer_base + AC97_MIXER_MASTER_VOLUME);
    if (percent == 0U) {
        io_out16(port, 0x8000U);
        return true;
    }
    const uint16_t attenuation = (uint16_t)(
        ((uint32_t)(100U - percent) * 31U + 50U) / 100U);
    io_out16(port, (uint16_t)((attenuation << 8) | attenuation));
    return true;
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
    } else if (playback_started ||
               timer_ticks() - playback_start_tick >=
                   playback_timeout_ticks) {
        /* DCH means the DMA chain has halted; the deadline handles a
         * device that completed before our first status poll. */
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
    playback_start_tick = timer_ticks();
    playback_timeout_ticks = ((uint64_t)sample_frames + 479ULL) /
                             480ULL + 2ULL;
    return NEXUS_AC97_SUBMIT_OK;
}
