#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/panel_boot_recovery.h"

void test_panel_boot_recovery_reset_stub(uint64_t next_exit);
uint32_t test_panel_boot_recovery_call_count(void);
uint64_t test_panel_boot_recovery_run_stub(void *ctx);
void test_panel_boot_recovery_log_stub(const char *line);
uint32_t test_panel_boot_recovery_log_count(void);

void test_panel_boot_recovery_max_attempts_constant_is_sane(void) {
    /*
     * The recovery budget must be at least 2: a value of 1 means
     * there is no recovery at all (the first exit stops the loop),
     * which defeats the point of having this constant. Anything
     * below 2 would be a regression against the ROADMAP item 10
     * contract.
     */
    TEST_ASSERT_TRUE(PANEL_BOOT_RECOVERY_MAX_ATTEMPTS >= 2U);
}

void test_panel_boot_recovery_decide_first_attempt_returns_continue(void) {
    /*
     * After the very first launch returns, attempts_used is 1 and
     * the budget is still untouched, so the recovery wrapper must
     * keep going. The exit code does not matter; we cover the
     * clean-exit and fault-exit shapes with the same expected
     * CONTINUE so a future "stop on clean exit" branch is forced to
     * change this test instead of slipping in silently.
     */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)PANEL_BOOT_RECOVERY_CONTINUE,
                             (uint64_t)panel_boot_recovery_decide(1U, 0U));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)PANEL_BOOT_RECOVERY_CONTINUE,
                             (uint64_t)panel_boot_recovery_decide(
                                 1U, 0xfffffffffffffff0ULL));
}

void test_panel_boot_recovery_decide_middle_attempt_returns_continue(void) {
    /*
     * attempts_used strictly less than the budget must always
     * CONTINUE so the wrapper actually recovers. Picking attempts
     * between 1 and MAX-1 exercises every "still room to try" slot.
     */
    for (uint32_t i = 1U; i < PANEL_BOOT_RECOVERY_MAX_ATTEMPTS; i++) {
        TEST_ASSERT_EQUAL_UINT64(
            (uint64_t)PANEL_BOOT_RECOVERY_CONTINUE,
            (uint64_t)panel_boot_recovery_decide(i, 0U));
        TEST_ASSERT_EQUAL_UINT64(
            (uint64_t)PANEL_BOOT_RECOVERY_CONTINUE,
            (uint64_t)panel_boot_recovery_decide(i, 42U));
    }
}

void test_panel_boot_recovery_decide_max_attempts_returns_stop(void) {
    /*
     * Reaching the budget must STOP_EXHAUSTED; otherwise the kernel
     * could loop forever relaunching a panel that crashes on every
     * boot. The recovery wrapper relies on this branch to bail out
     * after PANEL_BOOT_RECOVERY_MAX_ATTEMPTS attempts.
     */
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)PANEL_BOOT_RECOVERY_STOP_EXHAUSTED,
        (uint64_t)panel_boot_recovery_decide(
            PANEL_BOOT_RECOVERY_MAX_ATTEMPTS, 0U));
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)PANEL_BOOT_RECOVERY_STOP_EXHAUSTED,
        (uint64_t)panel_boot_recovery_decide(
            PANEL_BOOT_RECOVERY_MAX_ATTEMPTS,
            0xfffffffffffffff0ULL));
}

void test_panel_boot_recovery_decide_overshoot_returns_stop(void) {
    /*
     * Caller bug guard: if a future change ever increments the
     * counter past the budget (or someone calls decide after the
     * wrapper already gave up), the answer must still be STOP, not
     * CONTINUE. Otherwise we re-enter the loop and the kernel boots
     * in a tight restart cycle.
     */
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)PANEL_BOOT_RECOVERY_STOP_EXHAUSTED,
        (uint64_t)panel_boot_recovery_decide(
            PANEL_BOOT_RECOVERY_MAX_ATTEMPTS + 1U, 0U));
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)PANEL_BOOT_RECOVERY_STOP_EXHAUSTED,
        (uint64_t)panel_boot_recovery_decide(
            PANEL_BOOT_RECOVERY_MAX_ATTEMPTS + 100U,
            0xfffffffffffffff0ULL));
}

void test_panel_boot_recovery_decide_zero_attempts_returns_continue(void) {
    /*
     * Defensive guard: if the wrapper ever calls decide with a
     * zero counter (should not happen, but the type allows it),
     * returning CONTINUE keeps the loop going rather than
     * truncating to STOP_EXHAUSTED on the first run.
     */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)PANEL_BOOT_RECOVERY_CONTINUE,
                             (uint64_t)panel_boot_recovery_decide(0U, 0U));
}

void test_panel_boot_recovery_wrapper_stops_after_budget(void) {
    /*
     * The wrapper must invoke panel_boot_run exactly
     * PANEL_BOOT_RECOVERY_MAX_ATTEMPTS times so a persistently
     * crashing panel cannot hide behind an infinite restart loop.
     * Stub panel_boot_run keeps returning the same sentinel; the
     * wrapper should bail as soon as decide() says STOP_EXHAUSTED.
     */
    const uint64_t sentinel = 0xDEADBEEFDEADBEEFULL;

    test_panel_boot_recovery_reset_stub(sentinel);

    uint64_t exit_code = panel_boot_recovery_run(
        test_panel_boot_recovery_run_stub, 0, test_panel_boot_recovery_log_stub);

    TEST_ASSERT_EQUAL_UINT64(PANEL_BOOT_RECOVERY_MAX_ATTEMPTS,
                             (uint64_t)test_panel_boot_recovery_call_count());
    TEST_ASSERT_EQUAL_UINT64(sentinel, exit_code);
    TEST_ASSERT_EQUAL_UINT64(PANEL_BOOT_RECOVERY_MAX_ATTEMPTS * 2U + 1U,
                             (uint64_t)test_panel_boot_recovery_log_count());
}

void test_panel_boot_recovery_wrapper_propagates_last_exit_code(void) {
    /*
     * Whatever the final panel_boot_run returns, the wrapper must
     * surface it back to the caller. This keeps kernel_main's
     * "USER exit code: ..." line meaningful even after a
     * recovery loop, instead of masking the real cause behind the
     * wrapper's bookkeeping value.
     */
    test_panel_boot_recovery_reset_stub(42U);

    uint64_t exit_code = panel_boot_recovery_run(
        test_panel_boot_recovery_run_stub, 0, 0);

    TEST_ASSERT_EQUAL_UINT64(42U, exit_code);
    TEST_ASSERT_EQUAL_UINT64(PANEL_BOOT_RECOVERY_MAX_ATTEMPTS,
                             (uint64_t)test_panel_boot_recovery_call_count());
}

void test_panel_boot_recovery_run_rejects_null_callback(void) {
    test_panel_boot_recovery_reset_stub(7U);

    TEST_ASSERT_EQUAL_UINT64(0,
                             panel_boot_recovery_run(0, 0,
                                                     test_panel_boot_recovery_log_stub));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)test_panel_boot_recovery_call_count());
    TEST_ASSERT_EQUAL_UINT64(1,
                             (uint64_t)test_panel_boot_recovery_log_count());
}
