# Spec: Multimeter alive in the scope build

**Track:** meter
**Stage now:** **S1 in `guest-coldtrace`, DC Voltage only.** Coexistence works —
scope and meter run in one image at the same time (EXP-23, 2026-09-04). Every
other meter function is unreachable. This is issue #15.
**Champion:** —

## What it is

One image where the mode button takes you from a live scope to a working
multimeter and back, on **every** meter function.

Coexistence itself is done. In `guest-coldtrace` a mode switch to meter raises
PC11, brings USART2 up, and DCV reads a cell correctly while SPI3 acquisition
continues (EXP-05 2026-08-17, re-confirmed EXP-23 2026-09-04: 1.6158 V against a
1.61 V reference, `SPI3 OK` climbing throughout).

What is missing is **submode selection**. `FPGA_WARM_HANDOFF_TEST` does double
duty — it means both "the FPGA is already configured, skip config" *and* "this
is a scope-only image, no-op every meter entry point" — so
`fpga_set_meter_mode()` returns before its first statement. Resistance,
continuity, diode, current and capacitance all update the UI label and reach the
hardware not at all: DCV vs Resistance gives a bit-identical GPIO posture and an
identical TX frame (EXP-23, with a passing positive control that the same
instrument *does* see relay changes). The build works on DCV because that is
the posture it happens to boot in.

## Prior art

Stock does both in one image, trivially — which is exactly why this gap is the
first thing a stock user would notice. Wishlist Tier 1 #2 (manual range lock)
and the >10 V decimal-latch bug are the next two meter asks, but both are
moot until the meter runs in the build people actually use.

## Our angle

Once coexistence works, our meter already beats stock's documented annoyances
in reach: the decoder is ours (band-level dp fix, stable low-Ω/kΩ readings),
so range lock and honest resolution are firmware-sized follow-ons, not
reverse-engineering projects. Each gets its own spec once this one is at S2.

## Hardware dependencies

**There is no "kill" to explain.** Both hypotheses this section used to carry —
that the uploaded bitstream removes a meter-only NV design, and that the meter
might not be the FPGA at all — were answers to a question that turned out to be
malformed. The meter was never killed by FPGA configuration; it was dead because
our runtime drove PC11 (meter MUX enable) LOW unconditionally and never applied
stock's per-mode GPIO posture. Fixed 2026-08-17 (`ca7f28b`, `7326eac`).

What remains is ordinary firmware work on our side:

- Stock applies a **mode-entry sequence** — pins, then bus, then commands
  (`0x0800E360` enter / `0x0800E3E4` exit). We now do pins and bus; the commands
  half is what `fpga_set_meter_mode()` would send if it ran.
- The meter analog frontend is never postured for a submode in this build:
  `fpga_set_meter_frontend_for_submode()` has no reachable caller under
  `FPGA_WARM_HANDOFF_TEST`.
- `fpga_init` returns early in coldtrace, before its meter-frontend and
  meter-USART steps, so the boot-time posture is scope's.
- `echo_frames` stays 0 — data frames arrive, echo frames never do. Open since
  EXP-05, unexplained, and worth resolving before trusting any command
  acknowledgement.

## Stage ladder

| To reach | Criterion (checkable) |
|---|---|
| ~~S1 (in coldtrace)~~ | **MET** — EXP-05 (2026-08-17), re-confirmed EXP-23 (2026-09-04). DCV tracks a bench source in `guest-coldtrace` with the scope still capturing. |
| S1 (all submodes) | `fpga_set_meter_mode()` runs in this build, and a submode change is observable on the hardware: switching DCV → Resistance moves the analog frontend posture (`gpio scan` diff non-empty) *and* changes the TX frame. Both halves, with the `fpga scope range` positive control from EXP-23 in the same session. |
| S2 | DCV 0–9 V and resistance re-verified within a few percent against a bench DMM *post-config*, same session writeup. |
| S3 | Host regression over captured USART frames for the decoder (exists in part); on-device `bench.py` acceptance: scope→meter→scope cycle with a live reading at each stop. |
| S4 | Range lock (wishlist #2), >10 V dp fix, honest trailing digits (wishlist #4) — each promoted through its own spec. |

## Open questions

1. **Why does `echo_frames` stay 0?** Data frames (`0x5A 0xA5`) arrive
   continuously; echo frames (`0xAA 0x55`) never do, on any build since
   EXP-05. Until this is understood we have no acknowledgement channel, so a
   submode command that is silently ignored looks exactly like one that worked.
   Cheapest next measurement, and it gates the S1-all-submodes criterion.
2. Once `fpga_set_meter_mode()` runs here, do the non-DCV submodes actually
   work — or only become reachable? Nothing on record answers this; EXP-05 and
   EXP-23 both tested DCV alone.
3. Should `FPGA_WARM_HANDOFF_TEST` be split? Its two meanings are independent,
   and every meter no-op is attached to the wrong one. Gating the no-ops on
   `!FPGA_CONFIG_B` is the candidate — a cold-configured build is not a warm
   handoff and has no reason to inherit its restraint.
