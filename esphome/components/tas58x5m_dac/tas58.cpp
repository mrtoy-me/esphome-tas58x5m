#include "tas58.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"

namespace esphome::tas58x5m_dac {

static const char *const TAG = "tas58x5m_dac";

bool Tas58Component::get_analog_gain_(uint8_t* raw_gain) {
  uint8_t current;
  if (!this->tas58_read_byte_(TAS58_AGAIN, &current)) return false;
  // remove top 3 reserved bits
  *raw_gain = current & 0x1F;
  return true;
}

// Analog Gain Control , with 0.5dB one step
// lower 5 bits controls the analog gain.
// 00000: 0 dB (29.5V peak voltage)
// 00001: -0.5db
// 11111: -15.5 dB
// set analog gain in dB
bool Tas58Component::set_analog_gain_(float gain_db) {
  if ((gain_db < TAS58_MIN_ANALOG_GAIN) || (gain_db > TAS58_MAX_ANALOG_GAIN)) return false;

  uint8_t new_again = static_cast<uint8_t>(-gain_db * 2.0);

  uint8_t current_again;
  if (!this->tas58_read_byte_(TAS58_AGAIN, &current_again)) return false;

  // keep top 3 reserved bits combine with bottom 5 analog gain bits
  new_again = (current_again & TAS58_AGAIN_MASK) | new_again;
  if (!this->tas58_write_byte_(TAS58_AGAIN, new_again)) return false;

  ESP_LOGD(TAG, "Analog Gain: %fdB", gain_db);
  return true;
}

bool Tas58Component::get_dac_mode_(DacMode* mode) {
    uint8_t current_value;
    if (!this->tas58_read_byte_(TAS58_DEVICE_CTRL_1, &current_value)) return false;
    if (current_value & (1 << 2)) {
        *mode = PBTL;
    } else {
        *mode = BTL;
    }
    this->tas58_dac_mode_ = *mode;
    return true;
}

// only runs once from 'setup'
bool Tas58Component::set_dac_mode_(DacMode mode) {
  uint8_t current_value;
  if (!this->tas58_read_byte_(TAS58_DEVICE_CTRL_1, &current_value)) return false;

  // Update bit 2 based on the mode
  if (mode == PBTL) {
      current_value |= (1 << 2);  // Set bit 2 to 1 (PBTL mode)
  } else {
      current_value &= ~(1 << 2); // Clear bit 2 to 0 (BTL mode)
  }
  if (!this->tas58_write_byte_(TAS58_DEVICE_CTRL_1, current_value)) return false;

  // 'tas5805m_state_' global already has dac mode from YAML config
  // save anyway so 'set_dac_mode' could be used more generally
  this->tas58_dac_mode_ = mode;
  ESP_LOGD(TAG, "DAC mode: %s", this->tas58_dac_mode_ ? "PBTL" : "BTL");
  return true;
}

bool Tas58Component::tas58_is_muted_(){
  uint8_t current;
  if(!this->tas58_read_byte_(TAS58_DEVICE_CTRL_2, &current)) return false;
  return (current && TAS58_MUTE_MASK);
}

bool Tas58Component::set_deep_sleep_off_() {
  if (this->tas58_control_state_ != CTRL_DEEP_SLEEP) return true; // already not in deep sleep
  // preserve mute state
  uint8_t new_value = this->tas58_is_muted_() ? (CTRL_PLAY + TAS58_MUTE_MASK) : CTRL_PLAY;
  if (!this->tas58_write_byte_(TAS58_DEVICE_CTRL_2, new_value)) return false;

  this->tas58_control_state_ = CTRL_PLAY;                           // set Control State to play
  ESP_LOGD(TAG, "Deep Sleep Off");
  return true;
}

bool Tas58Component::set_deep_sleep_on_() {
  if (this->tas58_control_state_ == CTRL_DEEP_SLEEP) return true; // already in deep sleep

  // preserve mute state
  uint8_t new_value = this->tas58_is_muted_() ? (CTRL_DEEP_SLEEP + TAS58_MUTE_MASK) : CTRL_DEEP_SLEEP;
  if (!this->tas58_write_byte_(TAS58_DEVICE_CTRL_2, new_value)) return false;

  this->tas58_control_state_ = CTRL_DEEP_SLEEP;                     // set Control State to deep sleep
  ESP_LOGD(TAG, "Deep Sleep On");
  return true;
}

bool Tas58Component::get_digital_volume_(uint8_t* raw_volume) {
  uint8_t current = 254; // lowest raw volume
  if(!this->tas58_read_byte_(TAS58_DIG_VOL_CTRL, &current)) return false;
  *raw_volume = current;
  return true;
}

// controls both left and right channel digital volume
// digital volume is 24 dB to -103 dB in -0.5 dB step
// 00000000: +24.0 dB
// 00000001: +23.5 dB
// 00101111: +0.5 dB
// 00110000: 0.0 dB
// 00110001: -0.5 dB
// 11111110: -103 dB
// 11111111: Mute
bool Tas58Component::set_digital_volume_(uint8_t raw_volume) {
  if (!this->tas58_write_byte_(TAS58_DIG_VOL_CTRL, raw_volume)) return false;
  return true;
}

bool  Tas58Component::get_mixer_mode_(MixerMode *mode) {
  *mode = this->tas58_mixer_mode_;
  return true;
}


bool  Tas58Component::set_mixer_mode_(MixerMode mode) {
  uint32_t mixer_l_to_l, mixer_r_to_r, mixer_l_to_r, mixer_r_to_l;

  switch (mode) {
    case STEREO:
      mixer_l_to_l = TAS58_MIXER_VALUE_0DB;
      mixer_r_to_r = TAS58_MIXER_VALUE_0DB;
      mixer_l_to_r = TAS58_MIXER_VALUE_MUTE;
      mixer_r_to_l = TAS58_MIXER_VALUE_MUTE;
      break;

    case STEREO_INVERSE:
      mixer_l_to_l = TAS58_MIXER_VALUE_MUTE;
      mixer_r_to_r = TAS58_MIXER_VALUE_MUTE;
      mixer_l_to_r = TAS58_MIXER_VALUE_0DB;
      mixer_r_to_l = TAS58_MIXER_VALUE_0DB;
      break;

    case MONO:
      mixer_l_to_l = TAS58_MIXER_VALUE_MINUS6DB;
      mixer_r_to_r = TAS58_MIXER_VALUE_MINUS6DB;
      mixer_l_to_r = TAS58_MIXER_VALUE_MINUS6DB;
      mixer_r_to_l = TAS58_MIXER_VALUE_MINUS6DB;
      break;

    case LEFT:
      mixer_l_to_l = TAS58_MIXER_VALUE_0DB;
      mixer_r_to_r = TAS58_MIXER_VALUE_MUTE;
      mixer_l_to_r = TAS58_MIXER_VALUE_0DB;
      mixer_r_to_l = TAS58_MIXER_VALUE_MUTE;
      break;

    case RIGHT:
      mixer_l_to_l = TAS58_MIXER_VALUE_MUTE;
      mixer_r_to_r = TAS58_MIXER_VALUE_0DB;
      mixer_l_to_r = TAS58_MIXER_VALUE_MUTE;
      mixer_r_to_l = TAS58_MIXER_VALUE_0DB;
      break;

    default:
      ESP_LOGD(TAG, "Invalid %s", MIXER_MODE);
      return false;
  }

  if(!this->set_book_and_page_(TAS58_REG_BOOK_5, TAS58_REG_BOOK_5_MIXER_PAGE)) {
    ESP_LOGE(TAG, "%s begin Set %s", ERROR, MIXER_MODE);
    return false;
  }

  if (!this->tas58_write_bytes_(TAS58_REG_LEFT_TO_LEFT_GAIN, reinterpret_cast<uint8_t *>(&mixer_l_to_l), 4)) {
    ESP_LOGE(TAG, "%s Mixer L-L Gain", ERROR);
    return false;
  }

  if (!this->tas58_write_bytes_(TAS58_REG_RIGHT_TO_RIGHT_GAIN, reinterpret_cast<uint8_t *>(&mixer_r_to_r), 4)) {
    ESP_LOGE(TAG, "%s Mixer R-R Gain", ERROR);
    return false;
  }

  if (!this->tas58_write_bytes_(TAS58_REG_LEFT_TO_RIGHT_GAIN, reinterpret_cast<uint8_t *>(&mixer_l_to_r), 4)) {
    ESP_LOGE(TAG, "%s Mixer L-R Gain", ERROR);
    return false;
  }

  if (!this->tas58_write_bytes_(TAS58_REG_RIGHT_TO_LEFT_GAIN, reinterpret_cast<uint8_t *>(&mixer_r_to_l), 4)) {
    ESP_LOGE(TAG, "%s Mixer R-L Gain", ERROR);
    return false;
  }

  if (!this->set_book_and_page_(TAS58_REG_BOOK_CONTROL_PORT, TAS58_REG_PAGE_ZERO)) {
    ESP_LOGE(TAG, "%s end Set %s", ERROR, MIXER_MODE);
    return false;
  }

  // 'tas5805m_state_' global already has mixer mode from YAML config
  // save anyway so 'set_mixer_mode' could be used more generally in future
  this->tas58_mixer_mode_ = mode;
  ESP_LOGD(TAG, "%s: %s", MIXER_MODE, MIXER_MODE_TEXT[this->tas58_mixer_mode_]);
  return true;
}

bool Tas58Component::get_state_(ControlState* state) {
  *state = this->tas58_control_state_;
  return true;
}

bool Tas58Component::set_state_(ControlState state) {
  if (this->tas58_control_state_ == state) return true;
  if (!this->tas58_write_byte_(TAS58_DEVICE_CTRL_2, state)) return false;
  this->tas58_control_state_ = state;
  return true;
}

bool Tas58Component::clear_fault_registers_() {
  if (!this->tas58_write_byte_(TAS58_FAULT_CLEAR, ANALOG_FAULT_CLEAR_COMMAND)) return false;
  ESP_LOGD(TAG, "Faults cleared");
  return true;
}


// low level functions

bool Tas58Component::set_book_and_page_(uint8_t book, uint8_t page) {
  if (!this->tas58_write_byte_(TAS5805M_REG_PAGE_SET, TAS5805M_REG_PAGE_ZERO)) {
    ESP_LOGE(TAG, "%s page 0", ERROR);
    return false;
  }
  if (!this->tas58_write_byte_(TAS5805M_REG_BOOK_SET, book)) {
    ESP_LOGE(TAG, "%s book 0x%02X", ERROR, book);
    return false;
  }
  if (!this->tas58_write_byte_(TAS5805M_REG_PAGE_SET, page)) {
    ESP_LOGE(TAG, "%s page 0x%02X", ERROR, page);
    return false;
  }
  return true;
}

bool Tas58Component::tas58_read_byte_(uint8_t a_register, uint8_t* data) {
  return this->tas58_read_bytes_(a_register, data, 1);
}

bool Tas58Component::tas58_read_bytes_(uint8_t a_register, uint8_t* data, uint8_t number_bytes) {
  i2c::ErrorCode error_code;
  error_code = this->write(&a_register, 1);
  if (error_code != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "Write error:: %i", error_code);
    this->i2c_error_ = (uint8_t)error_code;
    return false;
  }
  error_code = this->read_register(a_register, data, number_bytes);
  if (error_code != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "Read error: %i", error_code);
    this->i2c_error_ = (uint8_t)error_code;
    return false;
  }
  return true;
}

bool Tas58Component::tas58_write_byte_(uint8_t a_register, uint8_t data) {
  return this->tas58_write_bytes_(a_register, &data, 1);
}

bool Tas58Component::tas58_write_bytes_(uint8_t a_register, uint8_t* data, uint8_t len) {
  i2c::ErrorCode error_code = this->write_register(a_register, data, len);
  if (error_code != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "Write error: %i", error_code);
    this->i2c_error_ = (uint8_t)error_code;
    return false;
  }
  return true;
}

}  // esphome::tas58x5m_dac
