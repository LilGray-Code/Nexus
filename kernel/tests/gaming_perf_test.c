#include "gaming_perf.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_frame_pacer_deadlines_and_missed_frames(void) {
    struct nexus_gaming_frame_pacer pacer;
    uint64_t deadline = 0U;
    assert(!nexus_gaming_frame_pacer_init(NULL, 10U, 100U));
    assert(!nexus_gaming_frame_pacer_init(&pacer, 0U, 100U));
    assert(!nexus_gaming_frame_pacer_init(&pacer, 10U, UINT64_MAX - 5U));

    assert(nexus_gaming_frame_pacer_init(&pacer, 10U, 100U));
    assert(nexus_gaming_frame_pacer_next(&pacer, 105U, &deadline));
    assert(deadline == 110U && pacer.missed_deadlines == 0U);
    assert(nexus_gaming_frame_pacer_next(&pacer, 115U, &deadline));
    assert(deadline == 120U && pacer.missed_deadlines == 0U);
    assert(nexus_gaming_frame_pacer_next(&pacer, 131U, &deadline));
    assert(deadline == 140U && pacer.missed_deadlines == 1U);
    assert(nexus_gaming_frame_pacer_next(&pacer, 171U, &deadline));
    assert(deadline == 180U && pacer.missed_deadlines == 4U);
    assert(!nexus_gaming_frame_pacer_next(&pacer, 180U, NULL));
}

static void test_frame_pacer_overflow_is_fail_closed(void) {
    struct nexus_gaming_frame_pacer pacer;
    uint64_t deadline = 0U;
    assert(nexus_gaming_frame_pacer_init(
        &pacer, 1U, UINT64_MAX - 2U));
    assert(nexus_gaming_frame_pacer_next(
        &pacer, UINT64_MAX - 1U, &deadline));
    assert(deadline == UINT64_MAX - 1U);
    assert(!nexus_gaming_frame_pacer_next(
        &pacer, UINT64_MAX, &deadline));
    assert(pacer.missed_deadlines == 0U);
}

static struct nexus_gaming_perf_sample full_sample(
    uint64_t cpu_busy, uint64_t gpu_busy, uint64_t input_latency,
    uint64_t resident, uint64_t gpu_memory) {
    return (struct nexus_gaming_perf_sample){
        .valid = NEXUS_GAMING_PERF_SAMPLE_ALL,
        .cpu_busy_ns = cpu_busy,
        .gpu_busy_ns = gpu_busy,
        .input_latency_ns = input_latency,
        .resident_bytes = resident,
        .gpu_memory_bytes = gpu_memory
    };
}

static void test_benchmark_metrics_and_validity(void) {
    struct nexus_gaming_perf_session session;
    struct nexus_gaming_perf_report report;
    const uint64_t period = 16666667ULL;
    const uint64_t start = 1000000000ULL;
    assert(nexus_gaming_perf_begin(&session, start, period, 4U));
    assert(!nexus_gaming_perf_begin(&session, start, period, 0U));
    assert(!nexus_gaming_perf_set_loading_interval(&session, 20U, 10U));
    assert(nexus_gaming_perf_set_loading_interval(
        &session, 2000000000ULL, 2500000000ULL));

    /* 60 measured frame intervals: 2/4 CPU cores and one GPU timeline busy
     * for half of each interval. Input latency is a distinct measured source.
     */
    for (uint64_t i = 1U; i <= 60U; ++i) {
        const uint64_t present = start + i * period;
        const struct nexus_gaming_perf_sample sample = full_sample(
            2U * period, period / 2U, 4000000ULL,
            256U * 1024U * 1024U + i * 1024U,
            128U * 1024U * 1024U + i * 512U);
        assert(nexus_gaming_perf_record_frame(&session, present, &sample));
    }
    assert(!nexus_gaming_perf_record_frame(
        &session, start + 60U * period, &(struct nexus_gaming_perf_sample){0}));
    assert(!nexus_gaming_perf_finish(
        &session, start + 60U * period - 1U, false));
    assert(nexus_gaming_perf_finish(
        &session, start + 60U * period, false));
    assert(!nexus_gaming_perf_record_frame(
        &session, start + 61U * period, &(struct nexus_gaming_perf_sample){0}));
    assert(!nexus_gaming_perf_finish(
        &session, start + 61U * period, true));

    assert(nexus_gaming_perf_report(&session, &report));
    assert(report.elapsed_ns == 60U * period);
    assert(report.loading_time_valid && report.loading_time_ns == 500000000ULL);
    assert(report.frame_count == 60U && report.dropped_frames == 0U);
    assert(report.average_frame_time_ns == period);
    assert(report.average_fps_milli >= 59998U &&
           report.average_fps_milli <= 60001U);
    assert(report.minimum_frame_time_ns == period);
    assert(report.maximum_frame_time_ns == period);
    assert(report.frame_p50_upper_ns == 17000000ULL);
    assert(report.frame_p95_upper_ns == 17000000ULL);
    assert(report.frame_p99_upper_ns == 17000000ULL);
    assert(report.cpu_utilization_valid);
    assert(report.cpu_utilization_basis_points >= 4999U &&
           report.cpu_utilization_basis_points <= 5001U);
    assert(report.gpu_utilization_valid);
    assert(report.gpu_utilization_basis_points >= 4999U &&
           report.gpu_utilization_basis_points <= 5001U);
    assert(report.input_latency_valid);
    assert(report.average_input_latency_ns == 4000000ULL);
    assert(report.maximum_input_latency_ns == 4000000ULL);
    assert(report.input_p95_upper_ns == 4000000ULL);
    assert(report.memory_valid);
    assert(report.resident_bytes_current == 256U * 1024U * 1024U + 60U * 1024U);
    assert(report.resident_bytes_peak == report.resident_bytes_current);
    assert(report.gpu_memory_bytes_peak == 128U * 1024U * 1024U + 60U * 512U);
    assert(!report.crashed);
}

static void test_missing_measurements_are_not_reported_as_zero(void) {
    struct nexus_gaming_perf_session session;
    struct nexus_gaming_perf_report report;
    const uint64_t start = 500U;
    assert(nexus_gaming_perf_begin(&session, start, 0U, 1U));
    const struct nexus_gaming_perf_sample sample = {0};
    assert(nexus_gaming_perf_record_frame(&session, 1000500U, &sample));
    assert(nexus_gaming_perf_record_frame(&session, 2000500U, &sample));
    assert(nexus_gaming_perf_finish(&session, 2000500U, false));
    assert(nexus_gaming_perf_report(&session, &report));
    assert(report.frame_count == 2U);
    assert(!report.cpu_utilization_valid && !report.gpu_utilization_valid);
    assert(!report.input_latency_valid && !report.memory_valid);
    assert(report.cpu_utilization_basis_points == 0U);
    assert(report.gpu_utilization_basis_points == 0U);
}

static void test_stutter_percentiles_dropped_frames_and_crashes(void) {
    struct nexus_gaming_perf_session session;
    struct nexus_gaming_perf_report report;
    const uint64_t start = 1000U;
    const uint64_t target = 10000000ULL;
    assert(nexus_gaming_perf_begin(&session, start, target, 8U));

    struct nexus_gaming_perf_sample sample = {0};
    assert(nexus_gaming_perf_record_frame(&session, start + target, &sample));
    assert(nexus_gaming_perf_record_frame(
        &session, start + target + 5000000ULL, &sample));
    assert(nexus_gaming_perf_record_frame(
        &session, start + target + 45000000ULL, &sample));
    assert(session.dropped_frames == 3U);
    assert(nexus_gaming_perf_finish(
        &session, start + target + 45000000ULL, true));
    assert(nexus_gaming_perf_report(&session, &report));
    assert(report.crashed);
    assert(report.frame_count == 3U);
    assert(report.frame_p95_upper_ns >= 30000000ULL);
    assert(report.maximum_frame_time_ns == 40000000ULL);
}

static void test_invalid_samples_are_rejected_without_mutation(void) {
    struct nexus_gaming_perf_session session;
    const uint64_t start = 100U;
    assert(nexus_gaming_perf_begin(&session, start, 1000U, 2U));
    struct nexus_gaming_perf_sample sample = {
        .valid = NEXUS_GAMING_PERF_SAMPLE_CPU,
        .cpu_busy_ns = 2001U
    };
    assert(!nexus_gaming_perf_record_frame(&session, start + 1000U, &sample));
    sample.valid = NEXUS_GAMING_PERF_SAMPLE_GPU;
    sample.gpu_busy_ns = 1001U;
    assert(!nexus_gaming_perf_record_frame(&session, start + 1000U, &sample));
    sample.valid = 0x80000000U;
    sample.gpu_busy_ns = 0U;
    assert(!nexus_gaming_perf_record_frame(&session, start + 1000U, &sample));
    sample.valid = 0U;
    assert(nexus_gaming_perf_record_frame(&session, start + 1000U, &sample));
    assert(session.frame_count == 1U);
    assert(session.frame_time_total_ns == 1000U);
    assert(!nexus_gaming_perf_record_frame(&session, start + 999U, &sample));
}

int main(void) {
    test_frame_pacer_deadlines_and_missed_frames();
    test_frame_pacer_overflow_is_fail_closed();
    test_benchmark_metrics_and_validity();
    test_missing_measurements_are_not_reported_as_zero();
    test_stutter_percentiles_dropped_frames_and_crashes();
    test_invalid_samples_are_rejected_without_mutation();
    puts("NEXUS GAMING FRAME PACING AND PERFORMANCE TESTS PASSED");
    return 0;
}
