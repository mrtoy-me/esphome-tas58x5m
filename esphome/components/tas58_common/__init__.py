import esphome.codegen as cg
from esphome.components import i2c

CODEOWNERS = ["@mrtoy"]

tas58_common_ns = cg.esphome_ns.namespace("tas58_common")

Tas58Component = tas58_common_ns.class_("Tas58Component", i2c.I2CDevice)
