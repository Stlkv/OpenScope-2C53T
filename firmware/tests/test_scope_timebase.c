/*
 * Host tests for the timebase knob mapping and the per-code sample-rate table.
 *
 * Same philosophy as test_scope_cal.c: the rates came off a bench and a unit
 * test cannot second-guess them. What is tested is the set of PROPERTIES the
 * rest of the firmware leans on:
 *
 *   1. Every UI timebase entry maps to a code, and the mapping is monotonic —
 *      turning the knob one way must never make the engine sample FASTER.
 *   2. The mapped codes stay inside the ladder both benches have driven
 *      (0x07..0x12); out-of-domain UI indices clamp instead of walking into
 *      unmeasured codes.
 *   3. A code without a bench-measured rate returns exactly 0.0f, so a
 *      caller that forgets to check prints an obviously-broken zero rather
 *      than a plausible wrong frequency.
 *   4. The codes that DO have rates form the doubling ladder the table
 *      header claims (0x0E -> 0x0F -> 0x10 halves within tolerance), so a
 *      hand-edit that breaks the claim breaks the build's test run too.
 *   5. The power-on UI default maps to the code the FPGA arm block actually
 *      writes (0x08) — the first label the user sees must not disagree with
 *      the running hardware.
 */

#include "../src/ui/scope_timebase.h"
#include "../src/ui/scope_state.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void test_mapping_monotonic_and_bounded(void)
{
    uint8_t prev = 0;
    for (uint8_t i = 0; i < TIMEBASE_COUNT; i++) {
        uint8_t code = scope_timebase_reg_code(i);
        CHECK(code >= 0x07 && code <= 0x12,
              "entry %u: code 0x%02X outside the driven ladder", i, code);
        if (i > 0)
            CHECK(code >= prev,
                  "entry %u: code 0x%02X < previous 0x%02X — a slower "
                  "label selected a faster rate", i, code, prev);
        prev = code;
    }
}

static void test_out_of_domain_clamps(void)
{
    CHECK(scope_timebase_reg_code(TIMEBASE_COUNT) ==
          scope_timebase_reg_code(TIMEBASE_COUNT - 1),
          "index past the table must clamp to the last entry");
    CHECK(scope_timebase_reg_code(0xFF) ==
          scope_timebase_reg_code(TIMEBASE_COUNT - 1),
          "index 0xFF must clamp to the last entry");
}

static void test_unmeasured_codes_return_zero(void)
{
    /* 0x08 is the deliberate one: EXP-12 withdrew this bench's figures for
     * it, so it must stay unprintable until replicated here. */
    CHECK(scope_timebase_fs_hz(0x08) == 0.0f,
          "0x08 has no admitted rate and must return exactly 0.0f");
    CHECK(scope_timebase_fs_hz(0x00) == 0.0f, "0x00 must return 0.0f");
    CHECK(scope_timebase_fs_hz(0x13) == 0.0f, "0x13 must return 0.0f");
    CHECK(scope_timebase_fs_hz(0xFF) == 0.0f, "0xFF must return 0.0f");
}

static void test_measured_ladder_doubles(void)
{
    float f_0e = scope_timebase_fs_hz(0x0E);
    float f_0f = scope_timebase_fs_hz(0x0F);
    float f_10 = scope_timebase_fs_hz(0x10);

    CHECK(f_0e > 0.0f && f_0f > 0.0f && f_10 > 0.0f,
          "0x0E/0x0F/0x10 are the measured tier and must have rates");

    /* EXP-10 fits: 62,958 / 30,235 / 14,854 — adjacent ratios 2.08 and
     * 2.03. Allow 15%: wide enough for refits on the same bench, narrow
     * enough to catch a swapped row or a decimal slip. */
    float r1 = f_0e / f_0f;
    float r2 = f_0f / f_10;
    CHECK(r1 > 1.7f && r1 < 2.3f, "0x0E/0x0F ratio %f not ~2", (double)r1);
    CHECK(r2 > 1.7f && r2 < 2.3f, "0x0F/0x10 ratio %f not ~2", (double)r2);
}

static void test_default_matches_arm_block(void)
{
    /* scope_state_init() seeds timebase_idx = 9 ("5us"); the FPGA arm block
     * writes rate code 0x08 at config time. These must agree. */
    CHECK(scope_timebase_reg_code(9) == 0x08,
          "UI default entry must map to the arm block's 0x08");
}

int main(void)
{
    test_mapping_monotonic_and_bounded();
    test_out_of_domain_clamps();
    test_unmeasured_codes_return_zero();
    test_measured_ladder_doubles();
    test_default_matches_arm_block();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all scope_timebase tests passed\n");
    return 0;
}
