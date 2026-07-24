# Pairing & Home Assistant Setup

How to get an SR-4x10 register from the box into Home Assistant, with either
Zigbee2MQTT (recommended — full feature set) or ZHA. Device background is in
[architecture.md](architecture.md); the one fact worth internalizing before
you start: this is a **sleepy end device**. It is asleep almost all the time
and only listens for ~5 s windows. Every "it's not responding" problem in this
guide comes back to that.

Everywhere the UI shows a position, **100 = fully open = maximum airflow**.

## 1. Zigbee2MQTT (recommended)

### 1.1 Install the external converter

Copy the converter into the `external_converters` folder inside your
Zigbee2MQTT data directory (the folder that contains `configuration.yaml`;
create `external_converters` if it doesn't exist):

```
# Home Assistant add-on install:
/config/zigbee2mqtt/external_converters/smart_register.mjs

# Generic install:
<z2m-data-dir>/external_converters/smart_register.mjs
```

The file is [`homeassistant/zigbee2mqtt/smart_register.mjs`](../homeassistant/zigbee2mqtt/smart_register.mjs)
in this repo. Zigbee2MQTT 2.x loads everything in that folder automatically on
startup — restart Zigbee2MQTT and check the log for a line loading the
converter (and no red errors mentioning `smart_register`).

> Z2M 1.x note: older versions instead require listing the file under
> `external_converters:` in `configuration.yaml`. Use Z2M 1.40+ or 2.x; the
> converter uses the modern extend API.

### 1.2 Pair

1. In Zigbee2MQTT, enable **Permit join** (ideally on the router nearest the
   register's final location — sleepy devices keep the parent they join
   through).
2. Insert batteries. A fresh (never-joined or factory-reset) unit starts
   network steering on its own.
3. Already-provisioned unit, or nothing happens: **hold the BOOT button for
   5 seconds**. The status LED starts the Identify blink and the device
   factory-resets and begins searching for a network.
4. Watch the Z2M log: `joined`, then `interview started`.

**Sleepy-device gotcha — keep it awake during the interview.** The interview
(reading endpoints, clusters, Basic strings) needs many round-trips, and the
device sleeps between 5 s polls. It fast-polls for a while after joining, but
if the interview stalls, **press the BOOT button briefly (short press)** —
that wakes it and opens a fast-poll window so the interview can proceed. It is
normal for the interview to fail once and succeed on retry (Z2M retries by
itself; you can also force it via *Reconfigure* on the device page). Keep the
device physically near a router (or the coordinator) for first pairing.

When the interview finishes the device shows up as **OpenRegister SR-4x10**.
If it instead shows as "unsupported", the external converter did not load —
recheck step 1.1.

### 1.3 What you get

| Entity | Type | Notes |
|---|---|---|
| `cover.<name>` | cover (position) | 100 = open = airflow, 0 = closed |
| `sensor.<name>_pressure` | sensor, Pa | duct static pressure, 1 Pa resolution (from `ScaledValue`) |
| `sensor.<name>_battery` | sensor, % | daily report; loaded-voltage based, honest for lithium AA |
| `sensor.<name>_voltage` | sensor, mV | battery voltage |
| `binary_sensor.<name>_failsafe_tripped` | binary_sensor (diagnostic) | local fail-open is active |
| `number.<name>_failsafe_threshold_pa` | number (config) | 50–500 Pa, step 5, default **150** |
| `number.<name>_failsafe_clear_hysteresis_pa` | number (config) | 5–100 Pa, default **30** |
| `sensor.<name>_last_trip_pressure_pa` | sensor (diagnostic) | pressure at last trip |
| `switch.<name>_auto_clear_enable` | switch (config) | ON = auto-resume after pressure recovers |
| `sensor.<name>_fault_code` | sensor (diagnostic) | `none` / `homing_timeout` / `stall` / `sensor_fail` |

Writes to the config entities (threshold, hysteresis, auto-clear) are
delivered on the device's next poll — allow up to ~5 s, and don't be surprised
that the UI value confirms only after the following read-back.

## 2. ZHA alternative

ZHA handles the cover, battery and a coarse (hPa) pressure sensor natively.
The custom fail-safe cluster needs the quirk.

1. Copy [`homeassistant/zha/smart_register_quirk.py`](../homeassistant/zha/smart_register_quirk.py)
   to a folder in your HA config, e.g. `/config/custom_zha_quirks/`.
2. Point ZHA at it in `configuration.yaml`:

   ```yaml
   zha:
     custom_quirks_path: /config/custom_zha_quirks/
   ```

3. Restart Home Assistant, then pair as in 1.2 (Settings → Devices → ZHA →
   Add device). The same BOOT-button rules apply.
4. Verify on the device page that the quirk is applied (Device info →
   "Quirk"). You get the same fail-safe entities as in the Z2M table above,
   plus a "Duct pressure" sensor in real Pa from `ScaledValue`.

If the device paired *before* the quirk was installed, remove and re-pair it
(or reconfigure) so the quirk's signature match and reporting setup apply.

## 3. Blueprints

Import both blueprints (Settings → Automations & Scenes → Blueprints →
Import blueprint, or copy the files):

```
config/blueprints/automation/openregister/room_register_control.yaml
config/blueprints/automation/openregister/pressure_floor_guard.yaml
```

Sources: [`homeassistant/blueprints/`](../homeassistant/blueprints/).

### 3.1 Per-room setup — one automation per room

For **each** room with a register, create an automation from
**"OpenRegister: room register control (proportional)"**:

1. **Room temperature sensor** — any temperature sensor in that room.
2. **Target temperature entity** — an `input_number` helper you create per
   room (e.g. `input_number.bedroom_setpoint`, 15–28 °C, step 0.5), **or**
   set the optional **Climate entity** to your thermostat to reuse its
   setpoint (the climate entity wins while it is available).
3. **Register cover entity** — that room's SR-4x10.
4. **Heat/cool mode** — `heat` opens the register when the room is too cold,
   `cool` when it is too warm. If your system switches seasonally, either
   flip this input twice a year or create two automations and enable the
   right one.
5. Leave the tuning inputs at their defaults to start: proportional band
   2.0 °C, deadband 0.3 °C, update every 5 min, step 10 %, minimum
   position 0 %.

Battery notes: the automation only moves the register when the commanded
change is ≥ one quantization step, so a well-tuned room settles and stops
moving. If a register is moving many times per hour, widen the proportional
band or increase the step.

### 3.2 Whole-system setup — exactly one pressure floor guard

Create **one** automation from **"OpenRegister: pressure floor guard"** for
the whole duct system:

- **Register cover entities** — every SR-4x10 on the system.
- **Minimum total open floor** — default 150 (= 1.5 registers-worth open).
  Rule of thumb: never let the room automations close more area than your
  blower tolerates; if unsure, keep ≥ 30–40 % of total register area open.
- **Fail-safe tripped sensors** — the registers' `failsafe_tripped`
  binary sensors, for trip notifications.
- **Notify service** — e.g. `notify.mobile_app_yourphone`.

The guard treats `unavailable`/`unknown` registers as **closed** (a
dead-battery register can't fail open — see architecture.md failure modes),
so a register dropping offline can legitimately cause the guard to open other
registers. That is by design.

### 3.3 Tuning threshold & hysteresis

The device-local fail-safe is the last line of defense; the numbers live on
the device (NVS) and work with HA completely offline.

1. With all registers open and the blower running, note the typical duct
   pressure (the `pressure` sensor).
2. Close registers (temporarily lower the guard floor if needed) until you
   reach the *most closed* configuration you would ever allow, note the
   pressure.
3. Set **failsafe_threshold_pa** comfortably above that worst-case normal
   pressure but below what your blower/ducts tolerate. Default **150 Pa**
   suits typical residential systems; static-pressure-rated ECM blowers may
   allow more.
4. **failsafe_clear_hysteresis_pa** (default **30 Pa**) sets how far pressure
   must fall below the threshold before the fail-safe clears. If a register
   trips and clears repeatedly (flapping) as the blower cycles, increase it.
5. **auto_clear_enable** ON means the register returns to its commanded
   position by itself once pressure recovers; OFF latches the fail-open until
   you toggle/clear from HA — useful while diagnosing why it tripped.

After a trip, `last_trip_pressure_pa` tells you how bad it actually got.

## 4. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Interview never completes / device stuck "interviewing" | Sleepy device slept through the interview | Short-press BOOT to wake it (fast-poll window), retry the interview; pair close to a router |
| "Failed to configure" in Z2M / entities exist but never update | Configure (bind + reporting setup) ran while the device was asleep | Short-press BOOT, then *Reconfigure* on the device page in Z2M/ZHA |
| Position/pressure updates stop entirely | Reporting was never configured, or device re-joined through a different parent | Reconfigure (see above); check LQI/parent in the network map |
| Cover position looks inverted (100 shows as closed) | Outdated or missing external converter (raw ZCL lift is inverted by design: lift 100 = closed) | Confirm `smart_register.mjs` is loaded and current; the converter maps lift → HA position so 100 = open. Do **not** "fix" with an HA template inversion |
| Device shows as unsupported (Z2M) | External converter not loaded | Check `external_converters/` path and Z2M startup log, restart Z2M |
| Fail-safe entities missing (ZHA) | Quirk not applied | Check `custom_quirks_path`, restart HA, re-pair or reconfigure the device |
| Writing threshold/hysteresis seems to do nothing | Write is queued until the next ~5 s poll; UI confirms on read-back | Wait a few seconds; short-press BOOT to force it through immediately |
| Fail-safe reads/writes fail with `UNSUPPORTED_ATTRIBUTE` | Manufacturer-code mismatch between firmware and converter/quirk (cluster 0xFC00, mfr code 0x131B) | Update converter/quirk and firmware to matching releases |
| Register won't move, `fault_code` ≠ `none` | Homing timeout / stall / pressure-sensor fault; device refuses normal moves | Check for vane obstruction, then power-cycle (re-homes on boot). `sensor_fail` with a working duct: check the sensor tubing |
| Frequent trip/clear cycling | Threshold too close to normal operating pressure | Raise `failsafe_threshold_pa` or increase `failsafe_clear_hysteresis_pa` |
| Battery draining fast | Automations commanding many small moves | Increase position step / proportional band in the room blueprint; check update interval; verify pressure reporting isn't flapping around ±10 Pa |
