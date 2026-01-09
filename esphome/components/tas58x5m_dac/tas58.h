#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome::tas58x5m_dac {

enum ControlState : uint8_t {
    CTRL_DEEP_SLEEP = 0x00, // Deep Sleep
    CTRL_SLEEP      = 0x01, // Sleep
    CTRL_HI_Z       = 0x02, // Hi-Z
    CTRL_PLAY       = 0x03, // Play
  };

enum DacMode : uint8_t {
  BTL  = 0, // Bridge tied load
  PBTL = 1, // Parallel load
};

enum MixerMode : uint8_t {
  STEREO = 0,
  STEREO_INVERSE,
  MONO,
  RIGHT,
  LEFT,
};

static const char* const MIXER_MODE_TEXT[] = {"STEREO", "STEREO_INVERSE", "MONO", "RIGHT", "LEFT"};

static const float TAS58_MIN_ANALOG_GAIN            = -15.5;
static const float TAS58_MAX_ANALOG_GAIN            = 0.0;

// set book and page registers
static const uint8_t TAS58_REG_PAGE_SET             = 0x00;
static const uint8_t TAS58_REG_BOOK_SET             = 0x7F;
static const uint8_t TAS58_REG_BOOK_CONTROL_PORT    = 0x00;
static const uint8_t TAS58_REG_PAGE_ZERO            = 0x00;

// tas5805m registers
static const uint8_t TAS58_DEVICE_CTRL_1            = 0x02;
static const uint8_t TAS58_DEVICE_CTRL_2            = 0x03;
static const uint8_t TAS58_FS_MON                   = 0x37;
static const uint8_t TAS58_BCK_MON                  = 0x38;
static const uint8_t TAS58_DIG_VOL_CTRL             = 0x4C;
static const uint8_t TAS58_ANA_CTRL                 = 0x53;
static const uint8_t TAS58_AGAIN                    = 0x54;
static const uint8_t TAS58_DSP_MISC                 = 0x66;
static const uint8_t TAS58_POWER_STATE              = 0x68;
static const uint8_t TAS58_FAULT_CLEAR              = 0x78;

static const uint8_t ANALOG_FAULT_CLEAR_COMMAND     = 0x80;

// Mixer registers
static const uint8_t TAS58_REG_BOOK_5               = 0x8C;
static const uint8_t TAS58_REG_BOOK_5_MIXER_PAGE    = 0x29;
static const uint8_t TAS58_REG_LEFT_TO_LEFT_GAIN    = 0x18;
static const uint8_t TAS58_REG_RIGHT_TO_LEFT_GAIN   = 0x1C;
static const uint8_t TAS58_REG_LEFT_TO_RIGHT_GAIN   = 0x20;
static const uint8_t TAS58_REG_RIGHT_TO_RIGHT_GAIN  = 0x24;
static const uint8_t TAS58_REG_BOOK_5_VOLUME_PAGE   = 0x2A;
static const uint8_t TAS58_REG_LEFT_VOLUME          = 0x24;
static const uint8_t TAS58_REG_RIGHT_VOLUME         = 0x28;
static const uint32_t TAS58_MIXER_VALUE_MUTE        = 0x00000000;
static const uint32_t TAS58_MIXER_VALUE_0DB         = 0x00008000;
static const uint32_t TAS58_MIXER_VALUE_MINUS6DB    = 0x00004000;

static const uint8_t TAS58_MUTE_MASK                = 0x08;  // LR Channel Mute
static const uint8_t TAS58_AGAIN_MASK               = 0xE0;  // mask to retain top 3 reserved bits of AGAIN register

class Tas58Component : public PollingComponent, public i2c::I2CDevice {
 public:
   Tas58Component() = default;

   void config_analog_gain(float analog_gain) { this->tas58_analog_gain_ = analog_gain; }
   void config_dac_mode(DacMode dac_mode) {this->tas58_dac_mode_ = dac_mode; }
   void config_mixer_mode(MixerMode mixer_mode) {this->tas58_mixer_mode_ = mixer_mode; }

   float get_setup_priority() const override { return setup_priority::IO - 1; }

   bool get_analog_gain_(uint8_t* raw_gain);
   bool set_analog_gain_(float gain_db);

   bool get_dac_mode_(DacMode* mode);
   bool set_dac_mode_(DacMode mode);

   bool tas58_is_muted_();

   bool set_deep_sleep_off_();
   bool set_deep_sleep_on_();

   bool get_digital_volume_(uint8_t* raw_volume);
   bool set_digital_volume_(uint8_t new_volume);

   bool get_mixer_mode_(MixerMode *mode);
   bool set_mixer_mode_(MixerMode mode);

   bool get_state_(ControlState* state);
   bool set_state_(ControlState state);

   bool clear_fault_registers_();

   // low level functions
   bool set_book_and_page_(uint8_t book, uint8_t page);

   bool tas58_read_byte_(uint8_t a_register, uint8_t* data);
   bool tas58_read_bytes_(uint8_t a_register, uint8_t* data, uint8_t number_bytes);
   bool tas58_write_byte_(uint8_t a_register, uint8_t data);
   bool tas58_write_bytes_(uint8_t a_register, uint8_t *data, uint8_t len);

   float tas58_analog_gain_{0};

   ControlState tas58_control_state_{0};

   DacMode tas58_dac_mode_{0};

   MixerMode tas58_mixer_mode_{0};

   // last i2c error, if there is error shown by 'dump_config'
   uint8_t i2c_error_{0};

};

}  // esphome::tas58x5m_dac
