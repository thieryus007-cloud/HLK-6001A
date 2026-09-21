import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_HAS_TARGET, DEVICE_CLASS_OCCUPANCY

from . import CONF_HLK_LD6001A_ID, HlkLd6001aComponent

DEPENDENCIES = ["hlk_ld6001a"]

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_HLK_LD6001A_ID): cv.use_id(HlkLd6001aComponent),
    cv.Optional(CONF_HAS_TARGET): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_OCCUPANCY, icon="mdi:motion-sensor"
    ),
}


async def to_code(config):
    comp = await cg.get_variable(config[CONF_HLK_LD6001A_ID])
    if sub_config := config.get(CONF_HAS_TARGET):
        sens = await binary_sensor.new_binary_sensor(sub_config)
        cg.add(comp.set_has_target_binary_sensor(sens))
