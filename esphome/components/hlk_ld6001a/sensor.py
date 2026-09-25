import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    STATE_CLASS_MEASUREMENT,
    UNIT_METER,
)

from . import CONF_HLK_LD6001A_ID, HlkLd6001aComponent

DEPENDENCIES = ["hlk_ld6001a"]

# Must match MAX_TARGETS in hlk_ld6001a.h.
MAX_TARGETS = 6

UNIT_METER_PER_SECOND = "m/s"

CONF_TARGET_COUNT = "target_count"
CONF_RECOVERY_COUNT = "recovery_count"
_POSITION_AXES = ("x", "y", "z")
_VELOCITY_AXES = ("vx", "vy", "vz")
_SETTERS = {
    "x": "set_target_x_sensor",
    "y": "set_target_y_sensor",
    "z": "set_target_z_sensor",
    "vx": "set_target_vx_sensor",
    "vy": "set_target_vy_sensor",
    "vz": "set_target_vz_sensor",
    "id": "set_target_id_sensor",
}


def _position_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_METER,
        accuracy_decimals=2,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:axis-arrow",
    )


def _velocity_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_METER_PER_SECOND,
        accuracy_decimals=2,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:speedometer",
    )


def _id_schema():
    # Persistent target ID, raw identifier not a physical measurement --
    # no unit/state_class, diagnostic like on the HLK-LD6001B project.
    return sensor.sensor_schema(
        accuracy_decimals=0,
        icon="mdi:identifier",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


# No respiration/heartrate/gesture here (unlike HLK-LD6001B): this module's
# TLV stream doesn't provide them at all -- see PROTOCOL.md.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_HLK_LD6001A_ID): cv.use_id(HlkLd6001aComponent),
        cv.Optional(CONF_TARGET_COUNT): sensor.sensor_schema(icon=ICON_COUNTER),
        cv.Optional(CONF_RECOVERY_COUNT): sensor.sensor_schema(
            icon="mdi:heart-pulse", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }
).extend(
    {
        cv.Optional(f"target_{n + 1}_{axis}"): _position_schema()
        for n in range(MAX_TARGETS)
        for axis in _POSITION_AXES
    }
).extend(
    {
        cv.Optional(f"target_{n + 1}_{axis}"): _velocity_schema()
        for n in range(MAX_TARGETS)
        for axis in _VELOCITY_AXES
    }
).extend(
    {
        cv.Optional(f"target_{n + 1}_id"): _id_schema()
        for n in range(MAX_TARGETS)
    }
)


async def to_code(config):
    comp = await cg.get_variable(config[CONF_HLK_LD6001A_ID])
    if sub_config := config.get(CONF_TARGET_COUNT):
        sens = await sensor.new_sensor(sub_config)
        cg.add(comp.set_target_count_sensor(sens))

    if sub_config := config.get(CONF_RECOVERY_COUNT):
        sens = await sensor.new_sensor(sub_config)
        cg.add(comp.set_recovery_count_sensor(sens))

    for n in range(MAX_TARGETS):
        for field in _POSITION_AXES + _VELOCITY_AXES + ("id",):
            key = f"target_{n + 1}_{field}"
            if sub_config := config.get(key):
                sens = await sensor.new_sensor(sub_config)
                cg.add(getattr(comp, _SETTERS[field])(n, sens))
