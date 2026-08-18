/*
 * scope_timebase.c — timebase knob mapping and per-code sample rates.
 *
 * See scope_timebase.h for the two contracts. This file is the "same
 * treatment volts just got" that scope_cal.h asked for: measure the rate
 * per reg-0x01 code, tier it, and refuse to print when there is no entry.
 *
 * THE CODE LADDER (what both benches agree on)
 * --------------------------------------------
 * Reg 0x01 selects the acquisition sample rate from a 1-2-5 ladder, faster
 * codes = lower values. Adjacent-code RATIOS are exact 2 / 2.5 steps and are
 * generator-independent — measured by in-window period counting on unit #2
 * (Stlkv, issue #18: 250/100/50/25/10 samples per period across 0x08-0x0C on
 * one input) and consistent with this bench's fits at 0x0E/0x0F/0x10
 * (EXP-10: 63k/30.2k/14.9k, ratios 2.08/2.03).
 *
 * THE ABSOLUTE SCALE (what they do not yet agree on)
 * --------------------------------------------------
 * Absolute rates from the two benches differ by a consistent ~1.19x
 * (unit #1: 14,854 S/s at 0x10; unit #2: 12,437 S/s at the same code).
 * Each bench traces its absolutes to a single uncharacterized generator
 * (ESP32 vs FNIRSI DST-210), so this is exactly the SOURCE_SCALE situation
 * scope_cal.h documents for volts, and it resolves the same way: one
 * trusted-source measurement of ONE code rescales everything. Until then
 * the fs table below carries only figures measured on THIS bench that
 * survived EXP-12's fold test, and every other code returns 0.0f.
 */

#include "scope_timebase.h"
#include "scope_state.h"

/*
 * Knob mapping: timebase_table[] entry -> reg-0x01 code.
 *
 * The renderer draws 1 sample per pixel with a vertical grid rule every
 * 32 px (scope_ui.c draw_scope_grid), so one horizontal division is 32
 * samples and the on-screen time per division at a given code is 32/fs.
 * Codes are assigned to the UI's 1-2-5 labels by ladder position:
 *
 *   code   0x07   0x08  0x09  0x0A  0x0B  0x0C   0x0D   0x0E   0x0F  0x10  0x11  0x12
 *   label  2us    5us   10us  20us  50us  100us  200us  500us  1ms   2ms   5ms   10ms
 *
 * With unit #1's measured rates the label sits within ~2-8% of the true
 * 32/fs figure (e.g. 0x10: 32/14,854 = 2.15 ms vs the "2ms" label); with
 * unit #2's scale it would be a uniform ~+28%. Either way the LABEL is
 * nominal — anything printed in seconds or Hz must come from
 * scope_timebase_fs_hz(), never from the label.
 *
 * Ends clamp: entries faster than 2us/div pin to 0x07 (codes 0x02-0x06
 * measured <=2 edges at 50 kHz on unit #2 — either >20 MS/s or broken
 * capture, indistinguishable there, so not offered); 20ms/div pins to 0x12
 * (0x13 measured dead on unit #2).
 */
static const uint8_t tb_reg_code[TIMEBASE_COUNT] = {
    0x07,   /*  0: "5ns"   — clamped to the fastest usable code */
    0x07,   /*  1: "10ns"  — clamped */
    0x07,   /*  2: "20ns"  — clamped */
    0x07,   /*  3: "50ns"  — clamped */
    0x07,   /*  4: "100ns" — clamped */
    0x07,   /*  5: "200ns" — clamped */
    0x07,   /*  6: "500ns" — clamped */
    0x07,   /*  7: "1us"   — clamped */
    0x07,   /*  8: "2us"   — 12.5 MS/s class (unit #2) */
    0x08,   /*  9: "5us"   — the power-on default the arm block writes */
    0x09,   /* 10: "10us"  */
    0x0A,   /* 11: "20us"  */
    0x0B,   /* 12: "50us"  */
    0x0C,   /* 13: "100us" */
    0x0D,   /* 14: "200us" */
    0x0E,   /* 15: "500us" */
    0x0F,   /* 16: "1ms"   */
    0x10,   /* 17: "2ms"   */
    0x11,   /* 18: "5ms"   */
    0x12,   /* 19: "10ms"  */
    0x12,   /* 20: "20ms"  — clamped to the slowest usable code */
};

_Static_assert(sizeof(tb_reg_code) == TIMEBASE_COUNT,
               "knob mapping must cover every timebase_table entry");

uint8_t scope_timebase_reg_code(uint8_t timebase_idx)
{
    if (timebase_idx >= TIMEBASE_COUNT)
        timebase_idx = TIMEBASE_COUNT - 1;
    return tb_reg_code[timebase_idx];
}

/*
 * Per-code sample rates, unit #1 (this repo's bench), in S/s.
 *
 * Admission rule, per EXP-12's method finding: an in-band linear fit alone
 * does not qualify — the figure must also predict its own above-Nyquist
 * folds. Codes measured on unit #2 only (0x07-0x0C, 0x11, 0x12) are NOT
 * entered: their internal consistency is good but their absolute scale
 * disagrees with this bench by the unresolved ~1.19x above, and a table
 * mixing the two scales would poison every Freq/Per readout with a
 * unit-dependent error. They join as `scripts/measure_sample_rate.py`
 * reaches them (0x0C and faster additionally need a source quicker than
 * the ESP32, per EXP-10 §5d).
 *
 * 0x08 stays empty on purpose: EXP-12 established that in free-run this
 * build's read paths do not get a coherent record at that code (unit #2's
 * 5.00 MS/s there is measured through a different read discipline and is
 * not adopted until replicated here — see the EXP-12 blind spots).
 */
static const struct { uint8_t code; float fs_hz; } tb_fs[] = {
    { 0x0E, 62958.0f },     /* EXP-10 acq-buffer fit, R^2 0.9804 */
    { 0x0F, 30235.0f },     /* EXP-10 acq-buffer fit, R^2 0.9895 */
    { 0x10, 14854.0f },     /* five independent fits, EXP-10/12, incl. fold test */
};

float scope_timebase_fs_hz(uint8_t reg_code)
{
    for (unsigned i = 0; i < sizeof(tb_fs) / sizeof(tb_fs[0]); i++) {
        if (tb_fs[i].code == reg_code)
            return tb_fs[i].fs_hz;
    }
    return 0.0f;
}
