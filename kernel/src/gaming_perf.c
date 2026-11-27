#include "gaming_perf.h"

#include <stddef.h>
#include <stdint.h>

#define PERF_FPS_SCALE 1000000000000ULL
#define PERF_RATIO_SCALE 10000ULL
#define PERF_RATIO_BINARY_BITS 14U

static uint64_t saturating_add(uint64_t left, uint64_t right) {
    return left > UINT64_MAX - right ? UINT64_MAX : left + right;
}

static uint64_t histogram_bucket(uint64_t value_ns) {
    const uint64_t bucket = value_ns == 0U ? 0U :
        (value_ns - 1U) / NEXUS_GAMING_PERF_BUCKET_WIDTH_NS;
    return bucket < NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS
        ? bucket : NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS - 1U;
}

static uint64_t percentile_rank(uint64_t count, uint32_t percentile) {
    const uint64_t whole = count / 100U;
    const uint64_t remainder = count % 100U;
    return whole * percentile +
           (remainder * percentile + 99U) / 100U;
}

static uint64_t histogram_percentile(
    const uint64_t histogram[NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS],
    uint64_t count, uint32_t percentile, uint64_t overflow_upper_ns) {
    if (count == 0U) return 0U;
    const uint64_t rank = percentile_rank(count, percentile);
    uint64_t cumulative = 0U;
    for (uint32_t i = 0U; i < NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS; ++i) {
        cumulative = saturating_add(cumulative, histogram[i]);
        if (cumulative >= rank) {
            if (i == NEXUS_GAMING_PERF_HISTOGRAM_BUCKETS - 1U)
                return overflow_upper_ns;
            return ((uint64_t)i + 1U) *
                   NEXUS_GAMING_PERF_BUCKET_WIDTH_NS;
        }
    }
    return overflow_upper_ns;
}

/*
 * Calculate value/denominator in basis points without multiplying a
 * potentially 64-bit nanosecond counter by 10000. The fractional component
 * uses 14 binary digits, giving less than one basis point of quantization.
 */
static uint64_t ratio_basis_points(uint64_t value, uint64_t denominator) {
    if (denominator == 0U) return 0U;
    const uint64_t whole = value / denominator;
    uint64_t remainder = value % denominator;
    if (whole > UINT64_MAX / PERF_RATIO_SCALE) return UINT64_MAX;
    uint64_t fraction = 0U;
    for (uint32_t bit = 0U; bit < PERF_RATIO_BINARY_BITS; ++bit) {
        fraction <<= 1;
        if (remainder >= denominator - remainder) {
            remainder -= denominator - remainder;
            fraction |= 1U;
        } else {
            remainder += remainder;
        }
    }
    const uint64_t fractional_basis_points =
        fraction * PERF_RATIO_SCALE / (1U << PERF_RATIO_BINARY_BITS);
    return saturating_add(whole * PERF_RATIO_SCALE,
                          fractional_basis_points);
}

bool nexus_gaming_frame_pacer_init(
    struct nexus_gaming_frame_pacer *pacer,
    uint64_t period_ns, uint64_t now_ns) {
    if (pacer == NULL || period_ns == 0U ||
        now_ns > UINT64_MAX - period_ns) return false;
    *pacer = (struct nexus_gaming_frame_pacer){
        .period_ns = period_ns,
        .next_deadline_ns = now_ns + period_ns,
        .missed_deadlines = 0U
    };
    return true;
}

bool nexus_gaming_frame_pacer_next(
    struct nexus_gaming_frame_pacer *pacer,
    uint64_t now_ns, uint64_t *deadline_ns) {
    if (pacer == NULL || deadline_ns == NULL || pacer->period_ns == 0U ||
        pacer->next_deadline_ns == 0U) return false;

    uint64_t deadline = pacer->next_deadline_ns;
    uint64_t missed = 0U;
    if (now_ns > deadline) {
        const uint64_t lateness = now_ns - deadline;
        missed = lateness / pacer->period_ns;
        if ((lateness % pacer->period_ns) != 0U) ++missed;
        if (missed > (UINT64_MAX - deadline) / pacer->period_ns)
            return false;
        deadline += missed * pacer->period_ns;
    }
    if (deadline > UINT64_MAX - pacer->period_ns) return false;

    pacer->missed_deadlines =
        saturating_add(pacer->missed_deadlines, missed);
    pacer->next_deadline_ns = deadline + pacer->period_ns;
    *deadline_ns = deadline;
    return true;
}

bool nexus_gaming_perf_begin(
    struct nexus_gaming_perf_session *session,
    uint64_t start_ns, uint64_t target_frame_period_ns,
    uint32_t online_cpu_count) {
    if (session == NULL || online_cpu_count == 0U) return false;
    *session = (struct nexus_gaming_perf_session){
        .start_ns = start_ns,
        .target_frame_period_ns = target_frame_period_ns,
        .last_present_ns = start_ns,
        .frame_time_min_ns = UINT64_MAX,
        .input_latency_min_ns = UINT64_MAX,
        .online_cpu_count = online_cpu_count
    };
    return true;
}

bool nexus_gaming_perf_set_loading_interval(
    struct nexus_gaming_perf_session *session,
    uint64_t load_start_ns, uint64_t load_complete_ns) {
    if (session == NULL || session->finished ||
        load_complete_ns < load_start_ns) return false;
    session->loading_time_ns = load_complete_ns - load_start_ns;
    session->loading_time_valid = true;
    return true;
}

bool nexus_gaming_perf_record_frame(
    struct nexus_gaming_perf_session *session,
    uint64_t present_ns, const struct nexus_gaming_perf_sample *sample) {
    if (session == NULL || sample == NULL || session->finished ||
        (sample->valid & ~NEXUS_GAMING_PERF_SAMPLE_ALL) != 0U ||
        present_ns <= session->last_present_ns) return false;

    const uint64_t frame_time = present_ns - session->last_present_ns;
    if ((sample->valid & NEXUS_GAMING_PERF_SAMPLE_GPU) != 0U &&
        sample->gpu_busy_ns > frame_time) return false;
    if ((sample->valid & NEXUS_GAMING_PERF_SAMPLE_CPU) != 0U) {
        const uint64_t cpu_capacity =
            frame_time > UINT64_MAX / session->online_cpu_count
                ? UINT64_MAX : frame_time * session->online_cpu_count;
        if (sample->cpu_busy_ns > cpu_capacity) return false;
    }
    if (session->frame_count == UINT64_MAX) return false;

    session->last_present_ns = present_ns;
    ++session->frame_count;
    session->frame_time_total_ns =
        saturating_add(session->frame_time_total_ns, frame_time);
    if (frame_time < session->frame_time_min_ns)
        session->frame_time_min_ns = frame_time;
    if (frame_time > session->frame_time_max_ns)
        session->frame_time_max_ns = frame_time;
    const uint64_t frame_bucket = histogram_bucket(frame_time);
    session->frame_histogram[frame_bucket] =
        saturating_add(session->frame_histogram[frame_bucket], 1U);

    if (session->target_frame_period_ns != 0U &&
        frame_time > session->target_frame_period_ns) {
        const uint64_t missed =
            (frame_time - 1U) / session->target_frame_period_ns;
        session->dropped_frames =
            saturating_add(session->dropped_frames, missed);
    }

    if ((sample->valid & NEXUS_GAMING_PERF_SAMPLE_CPU) != 0U) {
        session->cpu_busy_total_ns =
            saturating_add(session->cpu_busy_total_ns, sample->cpu_busy_ns);
        session->measured_sources |= NEXUS_GAMING_PERF_SAMPLE_CPU;
    }
    if ((sample->valid & NEXUS_GAMING_PERF_SAMPLE_GPU) != 0U) {
        session->gpu_busy_total_ns =
            saturating_add(session->gpu_busy_total_ns, sample->gpu_busy_ns);
        session->measured_sources |= NEXUS_GAMING_PERF_SAMPLE_GPU;
    }
    if ((sample->valid & NEXUS_GAMING_PERF_SAMPLE_INPUT) != 0U) {
        session->input_latency_total_ns = saturating_add(
            session->input_latency_total_ns, sample->input_latency_ns);
        if (sample->input_latency_ns < session->input_latency_min_ns)
            session->input_latency_min_ns = sample->input_latency_ns;
        if (sample->input_latency_ns > session->input_latency_max_ns)
            session->input_latency_max_ns = sample->input_latency_ns;
        session->input_latency_count =
            saturating_add(session->input_latency_count, 1U);
        const uint64_t input_bucket =
            histogram_bucket(sample->input_latency_ns);
        session->input_histogram[input_bucket] =
            saturating_add(session->input_histogram[input_bucket], 1U);
        session->measured_sources |= NEXUS_GAMING_PERF_SAMPLE_INPUT;
    }
    if ((sample->valid & NEXUS_GAMING_PERF_SAMPLE_MEMORY) != 0U) {
        session->resident_bytes_current = sample->resident_bytes;
        session->gpu_memory_bytes_current = sample->gpu_memory_bytes;
        if (sample->resident_bytes > session->resident_bytes_peak)
            session->resident_bytes_peak = sample->resident_bytes;
        if (sample->gpu_memory_bytes > session->gpu_memory_bytes_peak)
            session->gpu_memory_bytes_peak = sample->gpu_memory_bytes;
        session->measured_sources |= NEXUS_GAMING_PERF_SAMPLE_MEMORY;
    }
    return true;
}

bool nexus_gaming_perf_finish(
    struct nexus_gaming_perf_session *session,
    uint64_t end_ns, bool crashed) {
    if (session == NULL || session->finished ||
        end_ns < session->start_ns || end_ns < session->last_present_ns)
        return false;
    session->end_ns = end_ns;
    session->crashed = crashed;
    session->finished = true;
    return true;
}

bool nexus_gaming_perf_report(
    const struct nexus_gaming_perf_session *session,
    struct nexus_gaming_perf_report *report) {
    if (session == NULL || report == NULL || !session->finished ||
        session->end_ns < session->start_ns) return false;

    const uint64_t elapsed = session->end_ns - session->start_ns;
    const uint64_t average_frame = session->frame_count != 0U
        ? session->frame_time_total_ns / session->frame_count : 0U;
    const uint64_t frames_per_second_milli = average_frame != 0U
        ? PERF_FPS_SCALE / average_frame : 0U;
    const bool cpu_valid =
        (session->measured_sources & NEXUS_GAMING_PERF_SAMPLE_CPU) != 0U &&
        elapsed != 0U;
    const bool gpu_valid =
        (session->measured_sources & NEXUS_GAMING_PERF_SAMPLE_GPU) != 0U &&
        elapsed != 0U;
    uint64_t cpu_utilization = 0U;
    if (cpu_valid) {
        cpu_utilization = ratio_basis_points(
            session->cpu_busy_total_ns, elapsed) /
            session->online_cpu_count;
        if (cpu_utilization > PERF_RATIO_SCALE)
            cpu_utilization = PERF_RATIO_SCALE;
    }
    uint64_t gpu_utilization = 0U;
    if (gpu_valid) {
        gpu_utilization = ratio_basis_points(
            session->gpu_busy_total_ns, elapsed);
        if (gpu_utilization > PERF_RATIO_SCALE)
            gpu_utilization = PERF_RATIO_SCALE;
    }
    const bool input_valid =
        (session->measured_sources & NEXUS_GAMING_PERF_SAMPLE_INPUT) != 0U &&
        session->input_latency_count != 0U;
    const bool memory_valid =
        (session->measured_sources & NEXUS_GAMING_PERF_SAMPLE_MEMORY) != 0U;

    *report = (struct nexus_gaming_perf_report){
        .elapsed_ns = elapsed,
        .loading_time_ns = session->loading_time_ns,
        .frame_count = session->frame_count,
        .dropped_frames = session->dropped_frames,
        .average_fps_milli = frames_per_second_milli,
        .average_frame_time_ns = average_frame,
        .minimum_frame_time_ns = session->frame_count != 0U
            ? session->frame_time_min_ns : 0U,
        .maximum_frame_time_ns = session->frame_time_max_ns,
        .frame_p50_upper_ns = histogram_percentile(
            session->frame_histogram, session->frame_count, 50U,
            session->frame_time_max_ns),
        .frame_p95_upper_ns = histogram_percentile(
            session->frame_histogram, session->frame_count, 95U,
            session->frame_time_max_ns),
        .frame_p99_upper_ns = histogram_percentile(
            session->frame_histogram, session->frame_count, 99U,
            session->frame_time_max_ns),
        .cpu_utilization_basis_points = cpu_utilization,
        .gpu_utilization_basis_points = gpu_utilization,
        .average_input_latency_ns = input_valid
            ? session->input_latency_total_ns / session->input_latency_count
            : 0U,
        .maximum_input_latency_ns = input_valid
            ? session->input_latency_max_ns : 0U,
        .input_p95_upper_ns = input_valid
            ? histogram_percentile(session->input_histogram,
                  session->input_latency_count, 95U,
                  session->input_latency_max_ns) : 0U,
        .resident_bytes_current = session->resident_bytes_current,
        .resident_bytes_peak = session->resident_bytes_peak,
        .gpu_memory_bytes_current = session->gpu_memory_bytes_current,
        .gpu_memory_bytes_peak = session->gpu_memory_bytes_peak,
        .measured_sources = session->measured_sources,
        .loading_time_valid = session->loading_time_valid,
        .cpu_utilization_valid = cpu_valid,
        .gpu_utilization_valid = gpu_valid,
        .input_latency_valid = input_valid,
        .memory_valid = memory_valid,
        .crashed = session->crashed
    };
    return true;
}
