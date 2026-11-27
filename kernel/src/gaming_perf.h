#ifndef NEXUS_GAMING_PERF_H
#define NEXUS_GAMING_PERF_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS 128U
#define NEXUS_GAMING_PERF_BUCKET_WIDTH_NS 500000ULL

#define NEXUS_GAMING_PERF_SAMPLE_CPU       (1U << 0)
#define NEXUS_GAMING_PERF_SAMPLE_GPU       (1U << 1)
#define NEXUS_GAMING_PERF_SAMPLE_INPUT     (1U << 2)
#define NEXUS_GAMING_PERF_SAMPLE_MEMORY    (1U << 3)
#define NEXUS_GAMING_PERF_SAMPLE_ALL       0x0FU

struct nexus_gaming_frame_pacer {
    uint64_t period_ns;
    uint64_t next_deadline_ns;
    uint64_t missed_deadlines;
};

/*
 * A small monotonic-clock frame pacer. The caller waits until the returned
 * deadline using its scheduler/timer; this module does not busy-wait.
 */
bool nexus_gaming_frame_pacer_init(
    struct nexus_gaming_frame_pacer *pacer,
    uint64_t period_ns, uint64_t now_ns);
bool nexus_gaming_frame_pacer_next(
    struct nexus_gaming_frame_pacer *pacer,
    uint64_t now_ns, uint64_t *deadline_ns);

struct nexus_gaming_perf_sample {
    uint32_t valid;
    uint64_t cpu_busy_ns;       /* aggregate process CPU time across its threads */
    uint64_t gpu_busy_ns;       /* union busy time for the measured GPU timeline */
    uint64_t input_latency_ns;  /* input event timestamp to corresponding present */
    uint64_t resident_bytes;
    uint64_t gpu_memory_bytes;
};

struct nexus_gaming_perf_session {
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t target_frame_period_ns;
    uint64_t last_present_ns;
    uint64_t frame_count;
    uint64_t dropped_frames;
    uint64_t frame_time_total_ns;
    uint64_t frame_time_min_ns;
    uint64_t frame_time_max_ns;
    uint64_t cpu_busy_total_ns;
    uint64_t gpu_busy_total_ns;
    uint64_t input_latency_total_ns;
    uint64_t input_latency_count;
    uint64_t input_latency_min_ns;
    uint64_t input_latency_max_ns;
    uint64_t resident_bytes_current;
    uint64_t resident_bytes_peak;
    uint64_t gpu_memory_bytes_current;
    uint64_t gpu_memory_bytes_peak;
    uint64_t loading_time_ns;
    uint64_t frame_histogram[NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS];
    uint64_t input_histogram[NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS];
    uint32_t online_cpu_count;
    uint32_t measured_sources;
    bool loading_time_valid;
    bool finished;
    bool crashed;
};

struct nexus_gaming_perf_report {
    uint64_t elapsed_ns;
    uint64_t loading_time_ns;
    uint64_t frame_count;
    uint64_t dropped_frames;
    uint64_t average_fps_milli;
    uint64_t average_frame_time_ns;
    uint64_t minimum_frame_time_ns;
    uint64_t maximum_frame_time_ns;
    uint64_t frame_p50_upper_ns;
    uint64_t frame_p95_upper_ns;
    uint64_t frame_p99_upper_ns;
    uint64_t cpu_utilization_basis_points; /* 10000 = 100%, normalized by CPUs */
    uint64_t gpu_utilization_basis_points; /* 10000 = 100%, measured busy union */
    uint64_t average_input_latency_ns;
    uint64_t maximum_input_latency_ns;
    uint64_t input_p95_upper_ns;
    uint64_t resident_bytes_current;
    uint64_t resident_bytes_peak;
    uint64_t gpu_memory_bytes_current;
    uint64_t gpu_memory_bytes_peak;
    uint32_t measured_sources;
    bool loading_time_valid;
    bool cpu_utilization_valid;
    bool gpu_utilization_valid;
    bool input_latency_valid;
    bool memory_valid;
    bool crashed;
};

bool nexus_gaming_perf_begin(
    struct nexus_gaming_perf_session *session,
    uint64_t start_ns, uint64_t target_frame_period_ns,
    uint32_t online_cpu_count);
bool nexus_gaming_perf_set_loading_interval(
    struct nexus_gaming_perf_session *session,
    uint64_t load_start_ns, uint64_t load_complete_ns);
bool nexus_gaming_perf_record_frame(
    struct nexus_gaming_perf_session *session,
    uint64_t present_ns, const struct nexus_gaming_perf_sample *sample);
bool nexus_gaming_perf_finish(
    struct nexus_gaming_perf_session *session,
    uint64_t end_ns, bool crashed);
bool nexus_gaming_perf_report(
    const struct nexus_gaming_perf_session *session,
    struct nexus_gaming_perf_report *report);

#endif
