/**
 * Zigbee2MQTT external converter for the OpenRegister SR-4x10 smart HVAC register.
 *
 * Contract: docs/architecture.md
 *   - Endpoint 1, HA profile 0x0104, device id 0x0202 (Window Covering Device).
 *   - Window Covering (0x0102): ZCL lift 0 % = fully OPEN, 100 % = fully CLOSED.
 *     Z2M's standard windowCovering handling inverts at the boundary, so the
 *     published `position` follows the HA convention: 100 = open = airflow.
 *   - Pressure Measurement (0x0403): firmware maintains ScaledValue/Scale with
 *     1 Pa resolution (Scale = 3 -> ScaledValue is pressure in Pa).
 *   - Fail-safe custom cluster 0xFC00, manufacturer code 0x131B (Espressif).
 *
 * Install: copy into the Zigbee2MQTT data directory under
 * `external_converters/` (Z2M 2.x picks it up automatically) — see
 * docs/pairing-setup.md.
 *
 * Written against zigbee-herdsman-converters 20.x (Z2M 1.40+/2.x modernExtend
 * API). Places where an API name could drift are marked LIVE-VERIFY.
 */

import {Zcl} from 'zigbee-herdsman';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;
const ea = exposes.access;

// Espressif Systems' Zigbee manufacturer code — matches the node descriptor of
// the ESP32-C6 and the manufacturer code the firmware registers 0xFC00 with.
const MANUFACTURER_CODE = 0x131b;

// All 0xFC00 frames carry the manufacturer-specific bit + code 0x131B.
// LIVE-VERIFY: if reads/writes come back UNSUPPORTED_ATTRIBUTE the firmware
// registered the attributes as non-manufacturer-specific; drop this option
// everywhere it is passed below.
const manufacturerOptions = {manufacturerCode: MANUFACTURER_CODE};

const FAILSAFE_CLUSTER = 'openRegisterFailsafe';

const FAULT_CODES = {
    // Values per architecture.md fault_code enum8 (0..3).
    none: 0,
    homing_timeout: 1,
    stall: 2,
    sensor_fail: 3,
};

/**
 * Pressure in Pa from Pressure Measurement ScaledValue/Scale.
 *
 * Z2M's stock m.pressure() reads MeasuredValue (0.1 kPa units -> hPa/kPa
 * resolution), which is far too coarse for duct static pressure. The firmware
 * keeps ScaledValue (0x0010) at 1 Pa resolution with Scale (0x0014) = 3:
 *
 *   ScaledValue = pressure[kPa] * 10^Scale  =>  Pa = ScaledValue * 10^(3 - Scale)
 *
 * The scale attribute is read once during configure and cached in device meta
 * so reports that carry only scaledValue still convert correctly.
 */
const fzPressurePa = {
    cluster: 'msPressureMeasurement',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        const result = {};
        if (msg.data.scale !== undefined && meta.device.meta.pressureScale !== msg.data.scale) {
            meta.device.meta.pressureScale = msg.data.scale;
            meta.device.save();
        }
        if (msg.data.scaledValue !== undefined) {
            const scale = msg.data.scale ?? meta.device.meta.pressureScale ?? 3; // firmware default: Scale = 3 (Pa)
            result.pressure = Math.round(msg.data.scaledValue * 10 ** (3 - scale));
        }
        return result;
    },
};

/** Pa-resolution pressure exposure + reporting, packaged as a modernExtend. */
const openRegisterPressurePa = () => ({
    isModernExtend: true,
    fromZigbee: [fzPressurePa],
    exposes: [
        e
            .numeric('pressure', ea.STATE)
            .withUnit('Pa')
            .withValueStep(1)
            .withDescription('Duct static pressure at the register (1 Pa resolution, from ScaledValue)'),
    ],
    configure: [
        async (device, coordinatorEndpoint) => {
            const endpoint = device.getEndpoint(1);
            await reporting.bind(endpoint, coordinatorEndpoint, ['msPressureMeasurement']);
            // Firmware samples every 15–120 s (adaptive) and reports on >= 10 Pa
            // delta; min 30 s stops a chatty sensor from draining the battery,
            // max 1 h keeps a keepalive value flowing.
            await endpoint.configureReporting('msPressureMeasurement', [
                {attribute: 'scaledValue', minimumReportInterval: 30, maximumReportInterval: 3600, reportableChange: 10},
            ]);
            // Learn the scale once; also primes an initial pressure value.
            await endpoint.read('msPressureMeasurement', ['scale', 'scaledValue']);
        },
    ],
});

/** Bind/report/prime the fail-safe cluster (0xFC00). */
const openRegisterFailsafeConfigure = () => ({
    isModernExtend: true,
    configure: [
        async (device, coordinatorEndpoint) => {
            const endpoint = device.getEndpoint(1);
            await reporting.bind(endpoint, coordinatorEndpoint, [FAILSAFE_CLUSTER]);
            // Trip status and fault code must arrive "immediately": min 0 with a
            // change of 1 lets the firmware push the report the moment it happens
            // (it opens a fast-poll window so the report flushes through the
            // sleepy device's parent promptly). Max 1 h as a liveness backstop.
            // For discrete types (bool/enum8) ZCL ignores reportableChange; 1 is
            // passed to keep zigbee-herdsman's schema happy.
            await endpoint.configureReporting(
                FAILSAFE_CLUSTER,
                [
                    {attribute: 'failsafeTripped', minimumReportInterval: 0, maximumReportInterval: 3600, reportableChange: 1},
                    {attribute: 'faultCode', minimumReportInterval: 0, maximumReportInterval: 3600, reportableChange: 1},
                ],
                manufacturerOptions,
            );
            // Prime initial values for everything the UI shows.
            await endpoint.read(
                FAILSAFE_CLUSTER,
                ['failsafeTripped', 'failsafeThresholdPa', 'failsafeClearHysteresisPa', 'lastTripPressurePa', 'autoClearEnable', 'faultCode'],
                manufacturerOptions,
            );
        },
    ],
});

export default {
    // Must match the firmware Basic cluster strings exactly.
    zigbeeModel: ['SR-4x10'],
    model: 'SR-4x10',
    vendor: 'OpenRegister',
    description: 'Open-hardware Zigbee smart HVAC register (4"x10", pressure fail-safe, battery powered)',
    extend: [
        // ---- Cover ---------------------------------------------------------
        // Firmware is ZCL-compliant: CurrentPositionLiftPercentage 100 = closed.
        // Z2M's standard windowCovering pipeline already flips that so the
        // published/HA position is 100 = open = airflow. coverInverted stays
        // false — it exists only for non-compliant devices that report 100 =
        // open on the wire. LIVE-VERIFY once: position slider at 100 must mean
        // vanes open; if it reads backwards, the fix is here (not in HA).
        m.windowCovering({controls: ['lift'], coverInverted: false, configureReporting: true}),

        // ---- Identify (status LED blink) -----------------------------------
        m.identify(),

        // ---- Battery (Power Configuration) ---------------------------------
        // BatteryVoltage 0x0020 + BatteryPercentageRemaining 0x0021 (0.5 % units,
        // handled by the stock extend). Firmware reports daily.
        m.battery({
            voltage: true,
            voltageReporting: true,
            percentageReporting: true,
            percentageReportingConfig: {min: 3600, max: 86400, change: 2}, // 2 = 1 %
            voltageReportingConfig: {min: 3600, max: 86400, change: 1}, // 100 mV
        }),

        // ---- Pressure in Pa ------------------------------------------------
        openRegisterPressurePa(),

        // ---- Fail-safe custom cluster 0xFC00 -------------------------------
        m.deviceAddCustomCluster(FAILSAFE_CLUSTER, {
            ID: 0xfc00,
            manufacturerCode: MANUFACTURER_CODE,
            attributes: {
                failsafeTripped: {ID: 0x0000, type: Zcl.DataType.BOOLEAN},
                failsafeThresholdPa: {ID: 0x0001, type: Zcl.DataType.UINT16},
                failsafeClearHysteresisPa: {ID: 0x0002, type: Zcl.DataType.UINT16},
                lastTripPressurePa: {ID: 0x0003, type: Zcl.DataType.UINT16},
                autoClearEnable: {ID: 0x0004, type: Zcl.DataType.BOOLEAN},
                faultCode: {ID: 0x0005, type: Zcl.DataType.ENUM8},
            },
            commands: {},
            commandsResponse: {},
        }),

        // failsafe_tripped -> read-only binary (HA binary_sensor).
        m.binary({
            name: 'failsafe_tripped',
            cluster: FAILSAFE_CLUSTER,
            attribute: 'failsafeTripped',
            valueOn: [true, 1],
            valueOff: [false, 0],
            access: 'STATE_GET',
            entityCategory: 'diagnostic',
            zigbeeCommandOptions: manufacturerOptions,
            description: 'Local fail-open is active (duct overpressure drove the damper fully open)',
        }),

        // failsafe_threshold_pa -> RW number, 50..500 Pa step 5 (default 150).
        m.numeric({
            name: 'failsafe_threshold_pa',
            cluster: FAILSAFE_CLUSTER,
            attribute: 'failsafeThresholdPa',
            valueMin: 50,
            valueMax: 500,
            valueStep: 5,
            unit: 'Pa',
            access: 'ALL',
            entityCategory: 'config',
            zigbeeCommandOptions: manufacturerOptions,
            description: 'Trip fail-open when duct pressure exceeds this for 2 consecutive samples (default 150 Pa)',
        }),

        // failsafe_clear_hysteresis_pa -> RW number, 5..100 Pa (default 30).
        m.numeric({
            name: 'failsafe_clear_hysteresis_pa',
            cluster: FAILSAFE_CLUSTER,
            attribute: 'failsafeClearHysteresisPa',
            valueMin: 5,
            valueMax: 100,
            valueStep: 5,
            unit: 'Pa',
            access: 'ALL',
            entityCategory: 'config',
            zigbeeCommandOptions: manufacturerOptions,
            description: 'Fail-safe clears once pressure falls below threshold minus this hysteresis (default 30 Pa)',
        }),

        // last_trip_pressure_pa -> read-only numeric sensor.
        m.numeric({
            name: 'last_trip_pressure_pa',
            cluster: FAILSAFE_CLUSTER,
            attribute: 'lastTripPressurePa',
            unit: 'Pa',
            access: 'STATE_GET',
            entityCategory: 'diagnostic',
            zigbeeCommandOptions: manufacturerOptions,
            description: 'Duct pressure recorded at the last fail-safe trip',
        }),

        // auto_clear_enable -> RW binary (HA switch).
        m.binary({
            name: 'auto_clear_enable',
            cluster: FAILSAFE_CLUSTER,
            attribute: 'autoClearEnable',
            valueOn: ['ON', 1],
            valueOff: ['OFF', 0],
            access: 'ALL',
            entityCategory: 'config',
            zigbeeCommandOptions: manufacturerOptions,
            description: 'ON: resume the commanded position automatically when pressure recovers. OFF: latch fail-open until cleared from HA.',
        }),

        // fault_code -> read-only enum sensor.
        m.enumLookup({
            name: 'fault_code',
            cluster: FAILSAFE_CLUSTER,
            attribute: 'faultCode',
            lookup: FAULT_CODES,
            access: 'STATE_GET',
            entityCategory: 'diagnostic',
            zigbeeCommandOptions: manufacturerOptions,
            description: 'Actuator/sensor fault (none / homing_timeout / stall / sensor_fail)',
        }),

        // Reporting + initial reads for the fail-safe cluster.
        openRegisterFailsafeConfigure(),
    ],
    meta: {},
    // Sleepy end device (rx_on_when_idle = false, long poll ~5 s). Interview
    // and configure only succeed while it is awake — press the BOOT button to
    // hold a fast-poll window if configure keeps timing out (docs/pairing-setup.md).
};
