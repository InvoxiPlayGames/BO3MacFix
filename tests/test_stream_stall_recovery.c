// Added by Mickael DE LA ROQUE on 2026-09-08 to test the unofficial custom-map
// stream-stall workaround.
#include <assert.h>
#include <stdio.h>

#include "../source/stream_stall_recovery.h"

static StreamStallInput ready_input(float fraction, uint64_t now_ms) {
    return (StreamStallInput) {
        .eligible_custom_map = true,
        .connection_primed = true,
        .valid_snapshot = true,
        .db_load_complete = true,
        .engine_ready = false,
        .stream_fraction = fraction,
        .now_ms = now_ms,
    };
}

static void test_recovers_a_stable_non_098_stall(void) {
    StreamStallMonitor monitor = {0};
    StreamStallInput input = ready_input(0.82f, 1000);

    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += STREAM_STALL_TIMEOUT_MS - 1;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += 1;
    assert(stream_stall_recovery_update(&monitor, &input));
}

static void test_progress_restarts_the_stability_timer(void) {
    StreamStallMonitor monitor = {0};
    StreamStallInput input = ready_input(0.80f, 1000);

    assert(!stream_stall_recovery_update(&monitor, &input));
    input.stream_fraction = 0.81f;
    input.now_ms += 10000;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += STREAM_STALL_TIMEOUT_MS - 1;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += 1;
    assert(stream_stall_recovery_update(&monitor, &input));
}

static void test_small_forward_progress_restarts_the_stability_timer(void) {
    StreamStallMonitor monitor = {0};
    StreamStallInput input = ready_input(0.80f, 1000);

    assert(!stream_stall_recovery_update(&monitor, &input));
    input.stream_fraction += STREAM_STALL_PROGRESS_EPSILON / 2.0f;
    input.now_ms += STREAM_STALL_TIMEOUT_MS - 1;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += STREAM_STALL_TIMEOUT_MS - 1;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += 1;
    assert(stream_stall_recovery_update(&monitor, &input));
}

static void test_requires_the_safe_recovery_envelope(void) {
    StreamStallMonitor monitor = {0};
    StreamStallInput input = ready_input(0.90f, 1000);

    input.eligible_custom_map = false;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input = ready_input(0.90f, 1000);
    input.connection_primed = false;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input = ready_input(0.90f, 1000);
    input.valid_snapshot = false;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input = ready_input(0.90f, 1000);
    input.db_load_complete = false;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input = ready_input(0.90f, 1000);
    input.engine_ready = true;
    assert(!stream_stall_recovery_update(&monitor, &input));

    input = ready_input(STREAM_STALL_FINAL_PHASE_FLOOR - 0.01f, 1000);
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += STREAM_STALL_TIMEOUT_MS * 2;
    assert(!stream_stall_recovery_update(&monitor, &input));

    input = ready_input(1.0f, 1000);
    assert(!stream_stall_recovery_update(&monitor, &input));
}

static void test_prerequisite_loss_restarts_the_stability_timer(void) {
    StreamStallMonitor monitor = {0};
    StreamStallInput input = ready_input(0.90f, 1000);

    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += STREAM_STALL_TIMEOUT_MS - 1;
    input.valid_snapshot = false;
    assert(!stream_stall_recovery_update(&monitor, &input));

    input.valid_snapshot = true;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += STREAM_STALL_TIMEOUT_MS - 1;
    assert(!stream_stall_recovery_update(&monitor, &input));
    input.now_ms += 1;
    assert(stream_stall_recovery_update(&monitor, &input));
}

static void test_new_load_invalidates_completed_generation(void) {
    StreamRecoveryLifecycle lifecycle =
        STREAM_RECOVERY_LIFECYCLE_INITIALIZER;

    unsigned int first_generation = stream_recovery_begin_load(&lifecycle);
    assert(!stream_recovery_is_complete(&lifecycle));
    assert(stream_recovery_try_complete(&lifecycle, first_generation));
    assert(stream_recovery_is_complete(&lifecycle));

    unsigned int second_generation = stream_recovery_begin_load(&lifecycle);
    assert(second_generation != first_generation);
    assert(!stream_recovery_is_complete(&lifecycle));
    assert(!stream_recovery_try_complete(&lifecycle, first_generation));
    assert(stream_recovery_try_complete(&lifecycle, second_generation));
    assert(stream_recovery_is_complete(&lifecycle));
}

int main(void) {
    test_recovers_a_stable_non_098_stall();
    test_progress_restarts_the_stability_timer();
    test_small_forward_progress_restarts_the_stability_timer();
    test_requires_the_safe_recovery_envelope();
    test_prerequisite_loss_restarts_the_stability_timer();
    test_new_load_invalidates_completed_generation();
    puts("stream stall recovery tests passed");
    return 0;
}
