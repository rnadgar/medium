"""ZHA quirk (zigpy quirks v2) for the OpenRegister SR-4x10 smart HVAC register.

Contract: docs/architecture.md
  - Sleepy End Device, endpoint 1, profile 0x0104, device id 0x0202
    (Window Covering Device). ZHA's stock handling already gives you the cover
    entity (position 100 = open = airflow — ZHA inverts ZCL lift at the
    boundary), the battery sensors (Power Configuration), and an hPa pressure
    sensor from Pressure Measurement MeasuredValue.
  - This quirk adds:
      * the manufacturer-specific fail-safe cluster 0xFC00 (mfr code 0x131B),
      * config/diagnostic entities for its six attributes,
      * a 1 Pa resolution duct-pressure sensor from Pressure Measurement
        ScaledValue (firmware keeps Scale = 3, i.e. ScaledValue is Pa).

Install: drop this file into the directory configured as ZHA's
``custom_quirks_path`` and restart Home Assistant — see docs/pairing-setup.md.

Written against zigpy >= 0.66 / ZHA as shipped in Home Assistant 2024.9+
(quirks v2 with mandatory ``fallback_name``). Spots where the v2 API has
historically drifted are commented LIVE-VERIFY.
"""

try:
    # zigpy >= 2.0 relocates quirk base classes into the zha-quirks package
    # (importing CustomCluster from zigpy.quirks emits a DeprecationWarning
    # pointing here).
    from zhaquirks.clusters import CustomCluster
except ImportError:
    from zigpy.quirks import CustomCluster
from zigpy.quirks.v2 import QuirkBuilder, ReportingConfig

# LIVE-VERIFY: these enums moved into zigpy.quirks.v2.homeassistant.* when
# quirks v2 landed; on very old zigpy they lived in HA itself. Paths below are
# correct for zigpy 0.66+ (2024-2025).
from zigpy.quirks.v2.homeassistant import EntityPlatform, EntityType, UnitOfPressure
from zigpy.quirks.v2.homeassistant.binary_sensor import BinarySensorDeviceClass
from zigpy.quirks.v2.homeassistant.number import NumberDeviceClass
from zigpy.quirks.v2.homeassistant.sensor import SensorDeviceClass, SensorStateClass
import zigpy.types as t
from zigpy.zcl.clusters.measurement import PressureMeasurement
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef

# Espressif Systems' Zigbee manufacturer code. The node descriptor carries the
# same code, so zigpy automatically stamps it on every frame touching an
# attribute flagged is_manufacturer_specific=True below — no explicit
# manufacturer= argument is needed on reads/writes.
OPENREGISTER_MANUFACTURER_CODE = 0x131B

FAILSAFE_CLUSTER_ID = 0xFC00


class FaultCode(t.enum8):
    """fault_code attribute values (architecture.md: 0..3)."""

    None_ = 0x00  # trailing underscore: "None" is reserved in Python
    Homing_Timeout = 0x01
    Stall = 0x02
    Sensor_Fail = 0x03


class OpenRegisterFailsafe(CustomCluster):
    """Manufacturer-specific fail-safe cluster (0xFC00, mfr 0x131B)."""

    cluster_id: t.uint16_t = FAILSAFE_CLUSTER_ID
    name: str = "OpenRegister fail-safe"
    ep_attribute: str = "openregister_failsafe"

    class AttributeDefs(BaseAttributeDefs):
        # Access strings: r = readable, w = writeable, p = reportable.
        # LIVE-VERIFY: if the firmware turns out to register these attributes
        # WITHOUT the manufacturer-specific bit, flip
        # is_manufacturer_specific to False (reads would otherwise fail with
        # UNSUPPORTED_ATTRIBUTE).
        failsafe_tripped = ZCLAttributeDef(
            id=0x0000, type=t.Bool, access="rp", is_manufacturer_specific=True
        )
        failsafe_threshold_pa = ZCLAttributeDef(  # default 150 Pa, NVS-persisted
            id=0x0001, type=t.uint16_t, access="rw", is_manufacturer_specific=True
        )
        failsafe_clear_hysteresis_pa = ZCLAttributeDef(  # default 30 Pa
            id=0x0002, type=t.uint16_t, access="rw", is_manufacturer_specific=True
        )
        last_trip_pressure_pa = ZCLAttributeDef(
            id=0x0003, type=t.uint16_t, access="r", is_manufacturer_specific=True
        )
        auto_clear_enable = ZCLAttributeDef(  # default True
            id=0x0004, type=t.Bool, access="rw", is_manufacturer_specific=True
        )
        fault_code = ZCLAttributeDef(
            id=0x0005, type=FaultCode, access="rp", is_manufacturer_specific=True
        )


(
    # Match on the Basic cluster manufacturer/model strings from the firmware.
    QuirkBuilder("OpenRegister", "SR-4x10")
    # Add the custom cluster to endpoint 1 (the only endpoint).
    .adds(OpenRegisterFailsafe)
    # ---- failsafe_tripped -> binary_sensor (diagnostic) --------------------
    .binary_sensor(
        OpenRegisterFailsafe.AttributeDefs.failsafe_tripped.name,
        FAILSAFE_CLUSTER_ID,
        device_class=BinarySensorDeviceClass.PROBLEM,
        entity_type=EntityType.DIAGNOSTIC,
        # Immediate on change; hourly keepalive. reportable_change is moot for
        # a discrete (bool) type but the dataclass requires a value.
        reporting_config=ReportingConfig(
            min_interval=0, max_interval=3600, reportable_change=1
        ),
        translation_key="failsafe_tripped",
        fallback_name="Fail-safe tripped",
    )
    # ---- failsafe_threshold_pa -> number (config), 50..500 Pa step 5 -------
    .number(
        OpenRegisterFailsafe.AttributeDefs.failsafe_threshold_pa.name,
        FAILSAFE_CLUSTER_ID,
        min_value=50,
        max_value=500,
        step=5,
        unit=UnitOfPressure.PA,
        device_class=NumberDeviceClass.PRESSURE,
        translation_key="failsafe_threshold",
        fallback_name="Fail-safe threshold",
    )
    # ---- failsafe_clear_hysteresis_pa -> number (config), 5..100 Pa --------
    .number(
        OpenRegisterFailsafe.AttributeDefs.failsafe_clear_hysteresis_pa.name,
        FAILSAFE_CLUSTER_ID,
        min_value=5,
        max_value=100,
        step=5,
        unit=UnitOfPressure.PA,
        device_class=NumberDeviceClass.PRESSURE,
        translation_key="failsafe_clear_hysteresis",
        fallback_name="Fail-safe clear hysteresis",
    )
    # ---- last_trip_pressure_pa -> sensor (diagnostic) ----------------------
    .sensor(
        OpenRegisterFailsafe.AttributeDefs.last_trip_pressure_pa.name,
        FAILSAFE_CLUSTER_ID,
        unit=UnitOfPressure.PA,
        device_class=SensorDeviceClass.PRESSURE,
        state_class=SensorStateClass.MEASUREMENT,
        entity_type=EntityType.DIAGNOSTIC,
        translation_key="last_trip_pressure",
        fallback_name="Last trip pressure",
    )
    # ---- auto_clear_enable -> switch (config) ------------------------------
    .switch(
        OpenRegisterFailsafe.AttributeDefs.auto_clear_enable.name,
        FAILSAFE_CLUSTER_ID,
        entity_type=EntityType.CONFIG,
        translation_key="failsafe_auto_clear",
        fallback_name="Fail-safe auto clear",
    )
    # ---- fault_code -> enum sensor (diagnostic) ----------------------------
    # LIVE-VERIFY: QuirkBuilder.enum defaults to a SELECT (writeable); the
    # entity_platform=SENSOR override producing a read-only enum sensor is the
    # documented v2 way but is the most drift-prone call in this file.
    .enum(
        OpenRegisterFailsafe.AttributeDefs.fault_code.name,
        FaultCode,
        FAILSAFE_CLUSTER_ID,
        entity_platform=EntityPlatform.SENSOR,
        entity_type=EntityType.DIAGNOSTIC,
        translation_key="fault_code",
        fallback_name="Fault code",
    )
    # ---- duct pressure in Pa from ScaledValue ------------------------------
    # ZHA's stock pressure sensor uses MeasuredValue (hPa resolution). The
    # firmware keeps ScaledValue at Scale = 3, i.e. the raw value IS pascals,
    # so no divisor/multiplier is needed. Reporting mirrors the firmware
    # strategy: >= 10 Pa delta, 30 s minimum, hourly keepalive.
    .sensor(
        PressureMeasurement.AttributeDefs.scaled_value.name,
        PressureMeasurement.cluster_id,
        unit=UnitOfPressure.PA,
        device_class=SensorDeviceClass.PRESSURE,
        state_class=SensorStateClass.MEASUREMENT,
        reporting_config=ReportingConfig(
            min_interval=30, max_interval=3600, reportable_change=10
        ),
        translation_key="duct_pressure",
        fallback_name="Duct pressure",
    )
    .add_to_registry()
)
