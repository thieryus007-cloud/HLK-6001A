import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
MULTI_CONF = False

hlk_ld6001a_ns = cg.esphome_ns.namespace("hlk_ld6001a")
HlkLd6001aComponent = hlk_ld6001a_ns.class_(
    "HlkLd6001aComponent", cg.Component, uart.UARTDevice
)

CONF_HLK_LD6001A_ID = "hlk_ld6001a_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HlkLd6001aComponent),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

# Debit ambigu selon la source (voir PROTOCOL.md, "Debit UART") -- 115200
# retenu par defaut, a corriger dans le YAML si le module ne repond pas.
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "hlk_ld6001a",
    require_tx=True,
    require_rx=True,
    baud_rate=115200,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
