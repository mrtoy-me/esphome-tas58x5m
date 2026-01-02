import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import tas58x5m_dac
from esphome.components.audio_dac import AudioDac
from esphome import pins

# DEPENDENCIES = tas58x5m_dac.DEPENDENCIES

AUTO_LOAD = ["tas58x5m_dac"]

from esphome.const import (
    CONF_ID,
    CONF_ENABLE_PIN,
)

CODEOWNERS = ["@mrtoy-me"]
DEPENDENCIES = ["i2c"]

CONF_IGNORE_FAULT = "ignore_fault"
CONF_REFRESH_EQ = "refresh_eq"
CONF_VOLUME_MIN = "volume_min"
CONF_VOLUME_MAX = "volume_max"
CONF_TAS5805M_ID = "tas5805m_id"

tas5805m_ns = cg.esphome_ns.namespace("tas5805m")
Tas5805mComponent = tas5805m_ns.class_("Tas5805mComponent", AudioDac, tas58x5m_dac.Tas58Component)

AutoRefreshMode = tas5805m_ns.enum("AutoRefreshMode")
AUTO_REFRESH_MODES = {
     "BY_GAIN"  : AutoRefreshMode.BY_GAIN,
     "BY_SWITCH": AutoRefreshMode.BY_SWITCH,
}

ExcludeIgnoreMode = tas5805m_ns.enum("ExcludeIgnoreModes")
EXCLUDE_IGNORE_MODES = {
     "NONE"        : ExcludeIgnoreMode.NONE,
     "CLOCK_FAULT" : ExcludeIgnoreMode.CLOCK_FAULT,
}

def validate_config(config):
    if (config[CONF_VOLUME_MAX] - config[CONF_VOLUME_MIN]) < 9:
        raise cv.Invalid("volume_max must at least 9db greater than volume_min")
    return config

CONFIG_SCHEMA = cv.All(
    tas58x5m_dac.BASE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(Tas5805mComponent),
            cv.Required(CONF_ENABLE_PIN): pins.gpio_output_pin_schema,

            cv.Optional(CONF_IGNORE_FAULT, default="CLOCK_FAULT"): cv.enum(
                        EXCLUDE_IGNORE_MODES, upper=True
            ),

            cv.Optional(CONF_REFRESH_EQ, default="BY_GAIN"): cv.enum(
                        AUTO_REFRESH_MODES, upper=True
            ),
            cv.Optional(CONF_VOLUME_MAX, default="24dB"): cv.All(
                        cv.decibel, cv.int_range(-103, 24)
            ),
            cv.Optional(CONF_VOLUME_MIN, default="-103dB"): cv.All(
                        cv.decibel, cv.int_range(-103, 24)
            ),
        }
    )
    .add_extra(validate_config),
    cv.only_with_esp_idf,
    cv.only_on_esp32,
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await tas58x5m_dac.tas58x5m_dac_to_code(var, config)

    # await cg.register_component(var, config)
    enable = await cg.gpio_pin_expression(config[CONF_ENABLE_PIN])
    cg.add(var.set_enable_pin(enable))
    cg.add(var.config_ignore_fault_mode(config[CONF_IGNORE_FAULT]))
    cg.add(var.config_refresh_eq(config[CONF_REFRESH_EQ]))
    cg.add(var.config_volume_max(config[CONF_VOLUME_MAX]))
    cg.add(var.config_volume_min(config[CONF_VOLUME_MIN]))
