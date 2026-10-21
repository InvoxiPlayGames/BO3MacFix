// Added by Mickael DE LA ROQUE on 2026-09-08 for the unofficial custom-map
// stream-stall workaround.
#ifndef STREAM_STALL_RECOVERY_H
#define STREAM_STALL_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

// BO3's stream-progress function reserves its 0.75-1.0 range for the final
// completion phase. Recovery is unsafe while it is still in an earlier phase.
#define STREAM_STALL_FINAL_PHASE_FLOOR 0.75f
#define STREAM_STALL_PROGRESS_EPSILON 0.0005f
#define STREAM_STALL_TIMEOUT_MS 15000ULL

typedef struct StreamStallMonitor {
    bool observing;
    float anchor_fraction;
    uint64_t stable_since_ms;
} StreamStallMonitor;

typedef struct StreamStallInput {
    bool eligible_custom_map;
    bool connection_primed;
    bool valid_snapshot;
    bool db_load_complete;
    bool engine_ready;
    float stream_fraction;
    uint64_t now_ms;
} StreamStallInput;

typedef struct StreamRecoveryLifecycle {
    atomic_uint generation;
    atomic_uint completed_generation;
} StreamRecoveryLifecycle;

#define STREAM_RECOVERY_LIFECYCLE_INITIALIZER {0}

void stream_stall_recovery_reset(StreamStallMonitor *monitor);
bool stream_stall_recovery_update(
    StreamStallMonitor *monitor,
    const StreamStallInput *input);
unsigned int stream_recovery_begin_load(StreamRecoveryLifecycle *lifecycle);
unsigned int stream_recovery_current_generation(
    const StreamRecoveryLifecycle *lifecycle);
bool stream_recovery_try_complete(
    StreamRecoveryLifecycle *lifecycle,
    unsigned int expected_generation);
bool stream_recovery_is_complete(const StreamRecoveryLifecycle *lifecycle);

#endif
