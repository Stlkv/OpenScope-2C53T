/*
 * scope_timebase.h — the horizontal twin of scope_cal.h.
 *
 * Two independent mappings, deliberately in one place:
 *
 *   1. UI timebase entry -> FPGA reg-0x01 rate code (the knob).
 *      Nominal, by 1-2-5 alignment. This is what makes the horizontal
 *      knob change the hardware sample rate at all.
 *
 *   2. reg-0x01 rate code -> measured sample rate in Hz (the calibration).
 *      Tiered exactly like scope_cal.c tiers volts: a code either has a
 *      bench-measured rate or it has NOTHING, and callers must render the
 *      nothing honestly (samples, not seconds). No entry is ever estimated
 *      from an adjacent one at lookup time.
 *
 * Where the numbers come from, and the one open discrepancy, are documented
 * at the tables in scope_timebase.c.
 */

#ifndef SCOPE_TIMEBASE_H
#define SCOPE_TIMEBASE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The reg-0x01 code the acquisition engine should run for a UI timebase
 * entry (an index into timebase_table[], scope_state.h). Entries faster or
 * slower than the usable code ladder clamp to its ends. */
uint8_t scope_timebase_reg_code(uint8_t timebase_idx);

/* Measured sample rate for a reg-0x01 code, in samples/s.
 *
 * Returns 0.0f unless THIS project's bench has measured the code and the
 * figure survived the fold test (EXP-12's method finding: in-band R^2 alone
 * is not sufficient evidence for a sample rate). 0.0f means "quote samples,
 * not seconds" — the same contract as scope_cal_volts_per_count() == 0. */
float scope_timebase_fs_hz(uint8_t reg_code);

#ifdef __cplusplus
}
#endif

#endif /* SCOPE_TIMEBASE_H */
