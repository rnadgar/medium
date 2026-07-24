# Power Budget

Target: **> 1 year** on 4× AA lithium (LiFeS2, ~3200 mAh usable at low drain),
design estimate **~2.4 years**. All numbers below are design estimates to be
replaced with bench measurements at firmware bring-up (M2) — keep this file
current.

## Assumptions

- Pack: 4× Energizer L91 in series, 6.0 V nominal (7.2 V fresh open-circuit),
  ~3200 mAh to 4.0 V pack cutoff at these drain rates. LiFeS2 chosen for flat
  discharge, low self-discharge (~1 %/yr), and cold-floor performance.
- Always-on rail: TPS62125 (Iq ≈ 13 µA) feeding only the ESP32-C6.
- Switched rail (TPS22919, ≤ 100 nA off-leakage): DRV8833 + SDP810.
- Zigbee sleepy end device, automatic light sleep, long poll 5 s.
- 10 full-range damper moves/day (the room-control blueprint rate-limits and
  quantizes moves precisely to protect this line item).

## Average current

| Item | Basis | Average |
|---|---|---|
| Light-sleep floor | C6 light sleep + buck Iq + leakages ≈ 35–50 µA | **40 µA** |
| Zigbee data polls | ~5 ms @ ~20 mA every 5 s ≈ 0.1 mC/poll | **20 µA** |
| Pressure sampling | SDP810 ~4 mA × 50 ms + wake overhead ≈ 0.5 mC per sample, every 30 s | **17 µA** |
| Reports / rejoins / misc | attribute reports, occasional fast-poll windows | **10 µA** |
| Stepper moves | ~140 mA × 4 s ≈ 0.16 mAh/move × 10/day | **65 µA** |
| **Total** | | **~150 µA** |

## Battery life

```
3200 mAh / 0.15 mA ≈ 21,300 h ≈ 2.4 years
```

Still > 1 year at 2× estimate error. Self-discharge over that period is
negligible for LiFeS2.

## Biggest levers (in order) if measurements disappoint

1. **Long poll interval** 5 s → 7 s (polls scale linearly; latency cost only).
2. **Adaptive pressure sampling** — stretch idle period 120 s → 300 s; the
   fail-safe only needs fast sampling while the blower runs.
3. **Move quantization/rate limiting** in the HA blueprint (already 10 % steps,
   ≥ 5 min between moves).
4. Slower step rate during moves (lower peak current, better torque, slightly
   longer move time — net energy roughly neutral, but reduces battery sag).

## Design rules that protect the budget

- Nothing but the C6 on the always-on rail.
- Motor, driver, and pressure sensor are **hard-switched off** (load switch),
  not software-idled — parked draw is leakage only.
- No continuous pull-ups anywhere: limit-switch bias and the battery divider
  are GPIO-gated and only energized while in use.
- Coils never hold position — the gear train is non-backdrivable instead.

## Measurement plan (M2)

Bench with a µCurrent/joulescope on the pack:

1. Light-sleep floor with radio joined and idle (expect 35–50 µA).
2. Average over 10 min of normal polling (expect ≤ 70 µA incl. floor).
3. Energy per full-range move (expect ≈ 0.16 mAh).
4. Energy per pressure sample (expect ≈ 0.5 mC).
5. 24 h soak average — the number that goes in the table above.
