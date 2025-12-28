import esphome.codegen as cg
from esphome.components import i2c

CODEOWNERS = ["@mrtoy"]

tas58x5m_dac_ns = cg.esphome_ns.namespace("tas58x5m_dac")

Tas58x5mDac = tas58x5m_dac_ns.class_("Tas58x5mDac", i2c.I2CDevice)
