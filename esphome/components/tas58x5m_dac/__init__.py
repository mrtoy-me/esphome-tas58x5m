import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv

CODEOWNERS = ["@mrtoy"]

CONF_ANALOG_GAIN = "analog_gain"
CONF_DAC_MODE = "dac_mode"
CONF_MIXER_MODE = "mixer_mode"

tas58x5m_dac_ns = cg.esphome_ns.namespace("tas58x5m_dac")

Tas58Component = tas58x5m_dac_ns.class_("Tas58Component", cg.PollingComponent, i2c.I2CDevice)

DacMode = tas58x5m_dac_ns.enum("DacMode")
DAC_MODES = {
    "BTL" : DacMode.BTL,
    "PBTL": DacMode.PBTL,
}

MixerMode = tas58x5m_dac_ns.enum("MixerMode")
MIXER_MODES = {
    "STEREO"         : MixerMode.STEREO,
    "STEREO_INVERSE" : MixerMode.STEREO_INVERSE,
    "MONO"           : MixerMode.MONO,
    "RIGHT"          : MixerMode.RIGHT,
    "LEFT"           : MixerMode.LEFT,
}

ANALOG_GAINS = [-15.5, -15, -14.5, -14, -13.5, -13, -12.5, -12, -11.5, -11, -10.5, -10, -9.5, -9, -8.5, -8,
                 -7.5,  -7,  -6.5,  -6,  -5.5,  -5,  -4.5,  -4,  -3.5,  -3,  -2.5,  -2, -1.5, -1, -0.5,  0]

def validate_config(config):
    if config[CONF_DAC_MODE] == "PBTL" and (config[CONF_MIXER_MODE] == "STEREO" or config[CONF_MIXER_MODE] == "STEREO_INVERSE"):
        raise cv.Invalid("dac_mode: PBTL must have mixer_mode: MONO or RIGHT or LEFT")
    return config

BASE_SCHEMA = (
    cv.Schema(
        {
            cv.Optional(CONF_ANALOG_GAIN, default="-15.5dB"): cv.All(
                        cv.decibel, cv.one_of(*ANALOG_GAINS)
            ),
            cv.Optional(CONF_DAC_MODE, default="BTL"): cv.enum(
                        DAC_MODES, upper=True
            ),
            cv.Optional(CONF_MIXER_MODE, default="STEREO"): cv.enum(
                        MIXER_MODES, upper=True
            ),
        }
  )
  .extend(cv.polling_component_schema("10s"))
  .extend(i2c.i2c_device_schema(0x2D))
  validate_config
)

async def tas58x5m_dac_to_code(var, config):
    await cg.register_component(var, config)

    await i2c.register_i2c_device(var, config)

    cg.add(var.config_analog_gain(config[CONF_ANALOG_GAIN]))
    cg.add(var.config_dac_mode(config[CONF_DAC_MODE]))
    cg.add(var.config_mixer_mode(config[CONF_MIXER_MODE]))
