#include "tas5805m.h"
#include "tas5805m_minimal.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"

namespace esphome::tas5805m {

static const char *const TAG               = "tas5805m";
static const char *const ERROR             = "Error ";
static const char *const MIXER_MODE        = "Mixer Mode";
static const char *const EQ_BAND           = "EQ Band ";

static const uint8_t REMOVE_CLOCK_FAULT    = 0xFB;  // used to zero clock fault bit of global_fault1 register

// maximum delay allowed in "tas5805m_minimal.h" used in configure_registers()
static const uint8_t ESPHOME_MAXIMUM_DELAY = 5;     // milliseconds

// initial delay in 'loop' before writing eq gains to ensure on boot sound has
// played and tas5805m has detected i2s clock
static const uint8_t DELAY_LOOPS           = 20;    // 20 loop iterations ~ 320ms

// initial ms delay before starting fault updates
static const uint16_t INITIAL_UPDATE_DELAY = 4000;

void Tas5805mComponent::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();
    this->enable_pin_->digital_write(false);
    delay(10);
    this->enable_pin_->digital_write(true);
  }

  if (!this->configure_registers_()) {
    this->error_code_ = CONFIGURATION_FAILED;
    this->mark_failed();
  }

  // rescale -103db to 24db digital volume range to register digital volume range 254 to 0
  this->tas5805m_raw_volume_max_ = (uint8_t)((this->tas5805m_volume_max_ - 24) * -2);
  this->tas5805m_raw_volume_min_ = (uint8_t)((this->tas5805m_volume_min_ - 24) * -2);

  // initialise to now
  this->start_time_ = millis();
}

bool Tas5805mComponent::configure_registers_() {
  uint16_t i = 0;
  uint16_t counter = 0;
  uint16_t number_configurations = sizeof(TAS5805M_REGISTERS) / sizeof(TAS5805M_REGISTERS[0]);

  while (i < number_configurations) {
    switch (TAS5805M_REGISTERS[i].offset) {
      case TAS5805M_CFG_META_DELAY:
        if (TAS5805M_REGISTERS[i].value > ESPHOME_MAXIMUM_DELAY) return false;
        delay(TAS5805M_REGISTERS[i].value);
        break;
      default:
        if (!this->tas58_write_byte_(TAS5805M_REGISTERS[i].offset, TAS5805M_REGISTERS[i].value)) return false;
        counter++;
        break;
    }
    i++;
  }
  this->number_registers_configured_ = counter;

   // enable Tas5805m
  if(!this->set_deep_sleep_off_()) return false;

  // only setup once here
  if (!this->set_dac_mode_(this->tas58_dac_mode_)) return false;

  // note: setup of mixer mode deferred to 'loop' once 'refresh_settings' runs

  if (!this->set_analog_gain_(this->tas58_analog_gain_)) return false;

  if (!this->set_state_(CTRL_PLAY)) return false;

  #ifdef USE_TAS5805M_EQ
    #ifdef USE_SPEAKER
    if (!this->enable_eq(true)) return false;
    #else
    if (!this->enable_eq(false)) return false;
    #endif
  #endif

  return true;
}

void Tas5805mComponent::loop() {
  // 'play_file' is initiated by YAML on_boot with priority 220.0f
  // 'refresh_settings' is set by 'eq_gainband16000hz' or 'enable_eq_switch' (defined by YAML)
  // both have setup priority AFTER_CONNECTION = 100.0f
  // once tas5805m has detected i2s clock then mixer mode and eq gains settings
  // can be written within 'loop'

  // settings are only refreshed once
  // disable 'loop' once all eq gains have been written
  if (this->refresh_settings_complete_) {
    this->disable_loop(); // requires Esphome 2025.7.0
    return;
  }

  // refresh of settings has not been triggered yet
  if (!this->refresh_settings_triggered_) return;

  // once refresh settings is triggered then wait 'DELAY_LOOPS' before proceeding
  // to ensure on boot sound has played and tas5805m has detected i2s clock
  if (this->loop_counter_ < DELAY_LOOPS) {
    this->loop_counter_++;
    return;
  }

  if (!this->mixer_mode_configured_) {
    if (!this->set_mixer_mode_(this->tas58_mixer_mode_)) {
      // show warning but continue as if mixer mode was set ok
      ESP_LOGW(TAG, "%ssetting mixer mode: %s", ERROR, MIXER_MODE);
    }

    this->mixer_mode_configured_ = true;

    // if eq gains have not been configured in YAML
    // then once mixer mode is set, the refresh of settings is complete
    if (!this->using_eq_gains_) {
      this->refresh_settings_complete_ =  true;
      return;
    }

    // start writing eq gains next 'loop'
    return;
  }

  #ifdef USE_TAS5805M_EQ
  // write gains for all eq bands when triggered by boolean 'refresh_settings_triggered_'
  // write gains one eq band per 'loop' so component does not take too long in 'loop'

  if (this->refresh_band_ == NUMBER_EQ_BANDS) {
    // finished writing all gains
    this->refresh_settings_complete_ = true;
    this->refresh_band_ = 0;
    this->loop_counter_ = 0;
    return;
  }

  // write gains of current band and increment to next band ready for when loop next runs
  if (!this->set_eq_gain(this->refresh_band_, this->tas5805m_eq_gain_[this->refresh_band_])) {
    // show warning but continue as if eq gain was set ok
    ESP_LOGW(TAG, "%ssetting EQ Band %d Gain", ERROR, this->refresh_band_);
  }
  this->refresh_band_++;
  #endif

  return;
}

void Tas5805mComponent::update() {
  // initial delay before proceeding with updates
  if (!this->update_delay_finished_) {
    uint32_t current_time = millis();
    this->update_delay_finished_ = ((current_time - this->start_time_) > INITIAL_UPDATE_DELAY);

    if(!this->update_delay_finished_) return;

    // finished delay so clear faults
    if (!this->tas5_write_byte_(TAS5805M_FAULT_CLEAR, TAS5805M_ANALOG_FAULT_CLEAR)) {
      ESP_LOGW(TAG, "%sinitialising faults", ERROR);
    }

    // publish all binary sensors as false on first update
    #ifdef USE_TAS5805M_BINARY_SENSOR
    this->publish_faults_();
    #endif

    // read and process faults from next update
    return;
  }

  // if there was a fault last update then clear any faults
  if (this->have_fault_to_clear_) {
    if (!this->clear_fault_registers_()) {
      ESP_LOGW(TAG, "%sclearing faults", ERROR);
    }
    this->times_faults_cleared_++;
  }

  if (!this->read_fault_registers_()) {
    ESP_LOGW(TAG, "%sreading faults", ERROR);
    return;
  }

  // is there a fault that should be cleared next update
  this->have_fault_to_clear_ =
     ((this->tas5805m_faults_.clock_fault && (!this->ignore_clock_faults_when_clearing_faults_) ) || this->tas5805m_faults_.have_fault_except_clock_fault);


  // if no change in faults bypass publishing
  if ( !(this->is_new_common_fault_ || this->is_new_over_temperature_issue_ || this->is_new_channel_fault_ || this->is_new_global_fault_) ) return;

  #ifdef USE_TAS5805M_BINARY_SENSOR
  this->publish_faults_();
  #endif
}

#ifdef USE_TAS5805M_BINARY_SENSOR
void Tas5805mComponent::publish_faults_() {
  if (this->is_new_common_fault_) {
    if (this->have_fault_binary_sensor_ != nullptr) {
      this->have_fault_binary_sensor_->publish_state(this->tas5805m_faults_.have_fault);
    }

    if (this->clock_fault_binary_sensor_ != nullptr) {
      this->clock_fault_binary_sensor_->publish_state(this->tas5805m_faults_.clock_fault);
    }
  }

  if (this->is_new_over_temperature_issue_) {
    if (this->over_temperature_shutdown_fault_binary_sensor_ != nullptr) {
      this->over_temperature_shutdown_fault_binary_sensor_->publish_state(this->tas5805m_faults_.temperature_fault);
    }

    if (this->over_temperature_warning_binary_sensor_ != nullptr) {
      this->over_temperature_warning_binary_sensor_->publish_state(this->tas5805m_faults_.temperature_warning);
    }
  }

  // publish channel and global faults in separate loop iterations to spread component time when publishing binary sensors
  if (this->is_new_channel_fault_) {
    this->set_timeout("", 15, [this]() { this->publish_channel_faults_(); });
  }
  else {
    if (this->is_new_global_fault_) {
      this->set_timeout("", 15, [this]() { this->publish_global_faults_(); });
    }
  }
}

void Tas5805mComponent::publish_channel_faults_() {
  if (this->right_channel_over_current_fault_binary_sensor_ != nullptr) {
    this->right_channel_over_current_fault_binary_sensor_->publish_state(this->tas5805m_faults_.channel_fault & (1 << 0));
  }

  if (this->left_channel_over_current_fault_binary_sensor_ != nullptr) {
    this->left_channel_over_current_fault_binary_sensor_->publish_state(this->tas5805m_faults_.channel_fault & (1 << 1));
  }

  if (this->right_channel_dc_fault_binary_sensor_ != nullptr) {
    this->right_channel_dc_fault_binary_sensor_->publish_state(this->tas5805m_faults_.channel_fault & (1 << 2));
  }

  if (this->left_channel_dc_fault_binary_sensor_ != nullptr) {
    this->left_channel_dc_fault_binary_sensor_->publish_state(this->tas5805m_faults_.channel_fault & (1 << 3));
  }

  if (this->is_new_global_fault_) {
      this->set_timeout("", 15, [this]() { this->publish_global_faults_(); });
  }
}


void Tas5805mComponent::publish_global_faults_() {
  if (this->pvdd_under_voltage_fault_binary_sensor_ != nullptr) {
    this->pvdd_under_voltage_fault_binary_sensor_->publish_state(this->tas5805m_faults_.global_fault & (1 << 0));
  }

  if (this->pvdd_over_voltage_fault_binary_sensor_ != nullptr) {
    this->pvdd_over_voltage_fault_binary_sensor_->publish_state(this->tas5805m_faults_.global_fault & (1 << 1));
  }

  if (this->bq_write_failed_fault_binary_sensor_ != nullptr) {
    this->bq_write_failed_fault_binary_sensor_->publish_state(this->tas5805m_faults_.global_fault & (1 << 6));
  }

  if (this->otp_crc_check_error_binary_sensor_ != nullptr) {
    this->otp_crc_check_error_binary_sensor_->publish_state(this->tas5805m_faults_.global_fault & (1 << 7));
  }
}
#endif

void Tas5805mComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Tas5805m Audio Dac:");

  switch (this->error_code_) {
    case CONFIGURATION_FAILED:
      ESP_LOGE(TAG, "  %s setting up Tas5805m: %i", ERROR, this->i2c_error_);
      break;
    case NONE:
      LOG_PIN("  Enable Pin: ", this->enable_pin_);
      LOG_I2C_DEVICE(this);
      ESP_LOGCONFIG(TAG,
              "  Registers Configured: %i\n"
              "  Analog Gain: %3.1fdB\n"
              "  DAC Mode: %s\n"
              "  Mixer Mode: %s\n"
              "  Volume Maximum: %idB\n"
              "  Volume Minimum: %idB\n"
              "  Ignore Fault: %s\n"
              "  Refresh EQ: %s\n",
              this->number_registers_configured_, this->tas58_analog_gain_,
              this->tas58_dac_mode_ ? "PBTL" : "BTL",
              MIXER_MODE_TEXT[this->tas58_mixer_mode_],
              this->tas5805m_volume_max_, this->tas5805m_volume_min_,
              this->ignore_clock_faults_when_clearing_faults_ ? "CLOCK FAULTS" : "NONE",
              this->auto_refresh_ ? "BY SWITCH" : "BY GAIN"
              );
      LOG_UPDATE_INTERVAL(this);
      break;
  }

  #ifdef USE_TAS5805M_BINARY_SENSOR
  ESP_LOGCONFIG(TAG, "Tas5805m Binary Sensors:");
  LOG_BINARY_SENSOR("  ", "Any Faults", this->have_fault_binary_sensor_);
  ESP_LOGCONFIG(TAG, "    Exclude: %s", this->exclude_clock_fault_from_have_faults_ ? "CLOCK FAULTS" : "NONE");

  LOG_BINARY_SENSOR("  ", "Right Channel Over Current", this->right_channel_over_current_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Left Channel Over Current", this->left_channel_over_current_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Right Channel DC Fault", this->right_channel_dc_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Left Channel DC Fault", this->left_channel_dc_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "PVDD Under Voltage", this->pvdd_under_voltage_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "PVDD Over Voltage", this->pvdd_over_voltage_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Clock Fault", this->clock_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BQ Write Failed", this->bq_write_failed_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OTP CRC Check Error", this->otp_crc_check_error_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Over Temperature Shutdown", this->over_temperature_shutdown_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Over Temperature Warning", this->over_temperature_warning_binary_sensor_);
  #endif
}


// public

// used by 'enable_dac_switch'
void Tas5805mComponent::enable_dac(bool enable) {
  enable ? this->set_deep_sleep_off_() : this->set_deep_sleep_on_();
}

// used by 'enable_eq_switch'
bool Tas5805mComponent::enable_eq(bool enable) {
  #ifdef USE_TAS5805M_EQ
  enable ? this->set_eq_on_() : this->set_eq_off_();
  #endif
  return true;
}

// used by eq gain numbers
#ifdef USE_TAS5805M_EQ
bool Tas5805mComponent::set_eq_gain(uint8_t band, int8_t gain) {
  if (band < 0 || band >= NUMBER_EQ_BANDS) {
    ESP_LOGE(TAG, "Invalid %s%d", EQ_BAND, band);
    return false;
  }
  if (gain < TAS5805M_EQ_MIN_DB || gain > TAS5805M_EQ_MAX_DB) {
    ESP_LOGE(TAG, "Invalid %s%d Gain: %ddB", EQ_BAND, band, gain);
    return false;
  }

  // EQ Gains initially set by tas5805 number component setups
  if (!this->refresh_settings_triggered_) {
    this->tas5805m_eq_gain_[band] = gain;
    return true;
  }

  // runs when 'refresh_settings_triggered_' is true

  ESP_LOGD(TAG, "Set %s%d Gain: %ddB", EQ_BAND, band, gain);

  uint8_t x = (gain + TAS5805M_EQ_MAX_DB);

  const RegisterSequenceEq* reg_value = &TAS5805M_EQ_REGISTERS[x][band];
  if (reg_value == NULL) {
    ESP_LOGE(TAG, "%sNULL@eq_registers[%d][%d]",ERROR, x, band);
    return false;
  }

  if(!this->set_book_and_page_(TAS5805M_REG_BOOK_EQ, reg_value->page)) {
    ESP_LOGE(TAG, "%s%s%d @ page 0x%02X", ERROR, EQ_BAND, band, reg_value->page);
    return false;
  }

  if(!this->tas58_write_bytes_(reg_value->offset1, const_cast<uint8_t *>(reg_value->value), reg_value->bytes_in_block1)) {
    ESP_LOGE(TAG, "%s%s%d Gain: offset 0x%02X for %d bytes", ERROR, EQ_BAND, band, reg_value->offset1, reg_value->bytes_in_block1);
  }

  uint8_t bytes_in_block2 = COEFFICENTS_PER_EQ_BAND - reg_value->bytes_in_block1;
  if (bytes_in_block2 != 0) {
    uint8_t next_page = reg_value->page + 1;
    if(!this->set_book_and_page_(TAS5805M_REG_BOOK_EQ, next_page)) {
      ESP_LOGE(TAG, "%s%s%d @ page 0x%02X", ERROR, EQ_BAND, band, next_page);
      return false;
    }
    if(!this->tas58_write_bytes_(reg_value->offset2, const_cast<uint8_t *>(reg_value->value + reg_value->bytes_in_block1), bytes_in_block2)) {
      ESP_LOGE(TAG, "%s%s%d Gain: offset 0x%02X for %d bytes", ERROR, EQ_BAND, band, reg_value->offset2, bytes_in_block2);
      return false;
    }
  }

  return this->set_book_and_page_(TAS58_REG_BOOK_CONTROL_PORT, TAS58_REG_PAGE_ZERO);
}
#endif

bool Tas5805mComponent::set_mute_off() {
  if (!this->is_muted_) return true;
  if (!this->tas58_write_byte_(tas58x5m_dac::TAS58_DEVICE_CTRL_2, this->tas58_control_state_)) return false;
  this->is_muted_ = false;
  ESP_LOGD(TAG, "Mute Off");
  return true;
}

// set bit 3 MUTE in TAS5805M_DEVICE_CTRL_2 and retain current Control State
// ensures get_state = get_power_state
bool Tas5805mComponent::set_mute_on() {
  if (this->is_muted_) return true;
  if (!this->tas58_write_byte_(tas58x5m_dac::TAS58_DEVICE_CTRL_2, this->tas58_control_state_ + tas58x5m_dac::TAS58_MUTE_MASK)) return false;
  this->is_muted_ = true;
  ESP_LOGD(TAG, "Mute On");
  return true;
}

// used by 'enable_eq_switch' and 'eq_gain_band16000hz'
void Tas5805mComponent::refresh_settings() {
  // if 'this->refresh_settings_triggered_' is true then refresh of settings
  // has been initiated so no action is required
  if (this->refresh_settings_triggered_) return;

  // triggers 'loop' to configure mixer mode and eq gains
  // allows 'set_eq_gains' to now write eq gains rather than deferring for later setup
  // 'refresh_settings_triggered_' remains true once refresh of settings has completed
  // which allows 'set_eq_gains' continue to write eq gains
  this->refresh_settings_triggered_ = true;

  #ifdef USE_TAS5805M_EQ
  ESP_LOGD(TAG, "Refresh triggered: EQ %s", this->tas5805m_eq_enabled_ ? "Enabled" : "Disabled");
  #endif
  return;
}

// used by fault sensor
uint32_t Tas5805mComponent::times_faults_cleared() {
  return this->times_faults_cleared_;
}

// used by 'eq_gain_band16000hz' to determine if it should 'refresh_settings()'
bool Tas5805mComponent::use_eq_gain_refresh() {
  return (this->auto_refresh_ == AutoRefreshMode::BY_GAIN);
}

// used by 'enable_eq_switch' to determine if it should 'refresh_settings()'
bool Tas5805mComponent::use_eq_switch_refresh() {
  return (this->auto_refresh_ == AutoRefreshMode::BY_SWITCH);
}

float Tas5805mComponent::volume() {
  uint8_t raw_volume;
  this->get_digital_volume_(&raw_volume);

  return remap<float, uint8_t>(raw_volume, this->tas5805m_raw_volume_min_,
                                           this->tas5805m_raw_volume_max_,
                                           0.0f, 1.0f);
}

bool Tas5805mComponent::set_volume(float volume) {
  float new_volume = clamp(volume, 0.0f, 1.0f);
  uint8_t raw_volume = remap<uint8_t, float>(new_volume, 0.0f, 1.0f,
                                                         this->tas5805m_raw_volume_min_,
                                                         this->tas5805m_raw_volume_max_);
  if (!this->set_digital_volume_(raw_volume)) return false;

  int8_t dB = -(raw_volume / 2) + 24;
  ESP_LOGD(TAG, "Volume: %idB", dB);
  return true;
}


// protected

#ifdef USE_TAS5805M_EQ
bool Tas5805mComponent::get_eq_(bool* enabled) {
  uint8_t current_value;
  if (!this->tas58_read_byte_(TAS58_DSP_MISC, &current_value)) return false;
  *enabled = !(current_value & 0x01);
  this->tas5805m_eq_enabled_ = *enabled;
  return true;
}
#endif

bool Tas5805mComponent::set_eq_off_() {
  #ifdef USE_TAS5805M_EQ
  if (!this->tas5805m_eq_enabled_) return true;
  if (!this->tas580_write_byte_(TAS58_DSP_MISC, TAS5805M_CTRL_EQ_OFF)) return false;
  this->tas5805m_eq_enabled_ = false;
  ESP_LOGD(TAG, "EQ control Off");
  #endif
  return true;
}

bool Tas5805mComponent::set_eq_on_() {
  #ifdef USE_TAS5805M_EQ
  if (this->tas5805m_eq_enabled_) return true;
  if (!this->tas58_write_byte_(TAS58_DSP_MISC, TAS5805M_CTRL_EQ_ON)) return false;
  this->tas5805m_eq_enabled_ = true;
  ESP_LOGD(TAG, "EQ control On");
  #endif
  return true;
}

bool Tas5805mComponent::read_fault_registers_() {
  uint8_t current_faults[4];

  // read all faults registers
  if (!this->tas58_read_bytes_(TAS5805M_CHAN_FAULT, current_faults, 4)) return false;

  // note: new state is saved regardless as it is not worth conditionally saving state based on whether state has changed

  // check if any change CHAN_FAULT register as it contains 4 fault conditions(binary sensors)
  this->is_new_channel_fault_ = (current_faults[0] != this->tas5805m_faults_.channel_fault);
  this->tas5805m_faults_.channel_fault = current_faults[0];

  // separate clock fault from GLOBAL_FAULT1 register since clock faults can occur often
  // check if any change in GLOBAL_FAULT1 register as it contains 4 fault conditions(binary sensors) excluding clock fault
  uint8_t current_global_fault = current_faults[1] & REMOVE_CLOCK_FAULT;
  this->is_new_global_fault_ = (current_global_fault != this->tas5805m_faults_.global_fault);
  this->tas5805m_faults_.global_fault = current_global_fault;

  // over temperature fault is only fault condition in global_fault2 register
  this->is_new_over_temperature_issue_ = (current_faults[2] != this->tas5805m_faults_.temperature_fault);
  this->tas5805m_faults_.temperature_fault = current_faults[2];

  // over temperature warning is only fault condition in ot_warning register
  this->is_new_over_temperature_issue_ = (this->is_new_over_temperature_issue_ || (current_faults[3] != this->tas5805m_faults_.temperature_warning));
  this->tas5805m_faults_.temperature_warning = current_faults[3];

  bool new_fault_state; // reuse for temporary storage of new fault state

  // process clock_fault binary sensor
  new_fault_state = (current_faults[1] & (1 << 2));
  this->is_new_common_fault_ = (new_fault_state != this->tas5805m_faults_.clock_fault);
  this->tas5805m_faults_.clock_fault = new_fault_state;

  // process have_fault binary sensor
  this->tas5805m_faults_.have_fault_except_clock_fault =
    ( this->tas5805m_faults_.channel_fault || this->tas5805m_faults_.global_fault ||
      this->tas5805m_faults_.temperature_fault || this->tas5805m_faults_.temperature_warning );

  new_fault_state = (this->tas5805m_faults_.have_fault_except_clock_fault || (this->tas5805m_faults_.clock_fault && (!this->exclude_clock_fault_from_have_faults_)));
  this->is_new_common_fault_ = this->is_new_common_fault_ || (new_fault_state != this->tas5805m_faults_.have_fault);
  this->tas5805m_faults_.have_fault = new_fault_state;

  return true;
}

}  // namespace esphome::tas5805m
