// Added by Mickael DE LA ROQUE on 2026-09-08 for the unofficial custom-map
// stream-stall workaround.
#include "stream_stall_recovery.h"

#include <stddef.h>

void stream_stall_recovery_reset(StreamStallMonitor *monitor) {
    if (monitor == NULL)
        return;

    monitor->observing = false;
    monitor->anchor_fraction = 0.0f;
    monitor->stable_since_ms = 0;
}

bool stream_stall_recovery_update(
    StreamStallMonitor *monitor,
    const StreamStallInput *input)
{
    if (monitor == NULL || input == NULL)
        return false;

    bool safe_to_observe =
        input->eligible_custom_map &&
        input->connection_primed &&
        input->valid_snapshot &&
        input->db_load_complete &&
        !input->engine_ready &&
        input->stream_fraction >= STREAM_STALL_FINAL_PHASE_FLOOR &&
        input->stream_fraction < 1.0f;

    if (!safe_to_observe) {
        if (monitor->observing)
            stream_stall_recovery_reset(monitor);
        return false;
    }

    float difference = input->stream_fraction - monitor->anchor_fraction;
    if (difference < 0.0f)
        difference = -difference;

    if (!monitor->observing ||
        input->stream_fraction > monitor->anchor_fraction ||
        difference > STREAM_STALL_PROGRESS_EPSILON ||
        input->now_ms < monitor->stable_since_ms) {
        monitor->observing = true;
        monitor->anchor_fraction = input->stream_fraction;
        monitor->stable_since_ms = input->now_ms;
        return false;
    }

    return input->now_ms - monitor->stable_since_ms >=
        STREAM_STALL_TIMEOUT_MS;
}

unsigned int stream_recovery_begin_load(StreamRecoveryLifecycle *lifecycle) {
    if (lifecycle == NULL)
        return 0;

    return atomic_fetch_add(&lifecycle->generation, 1) + 1;
}

unsigned int stream_recovery_current_generation(
    const StreamRecoveryLifecycle *lifecycle)
{
    if (lifecycle == NULL)
        return 0;

    return atomic_load(&lifecycle->generation);
}

bool stream_recovery_try_complete(
    StreamRecoveryLifecycle *lifecycle,
    unsigned int expected_generation)
{
    if (lifecycle == NULL || expected_generation == 0 ||
        atomic_load(&lifecycle->generation) != expected_generation)
        return false;

    atomic_store(&lifecycle->completed_generation, expected_generation);
    return atomic_load(&lifecycle->generation) == expected_generation;
}

bool stream_recovery_is_complete(const StreamRecoveryLifecycle *lifecycle) {
    if (lifecycle == NULL)
        return false;

    unsigned int completed = atomic_load(&lifecycle->completed_generation);
    return completed != 0 &&
        completed == atomic_load(&lifecycle->generation);
}
