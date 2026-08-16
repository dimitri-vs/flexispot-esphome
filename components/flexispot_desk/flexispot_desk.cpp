#include "flexispot_desk.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace flexispot_desk {

static const char *const TAG = "flexispot_desk";

static const uint8_t SEG_PATTERNS[] = {
    0x3F,  // 0
    0x06,  // 1
    0x5B,  // 2
    0x4F,  // 3
    0x66,  // 4
    0x6D,  // 5
    0x7D,  // 6
    0x07,  // 7
    0x7F,  // 8
    0x6F,  // 9
};
static const uint8_t SEG_DASH = 0x40;

void FlexiSpotDesk::setup() {
  this->boot_time_ = millis();

  if (this->wake_pin_ != nullptr) {
    this->wake_pin_->setup();
    this->wake_pin_->digital_write(true);
    this->wake_pin_high_ = true;
    ESP_LOGI(TAG, "PIN 20 set HIGH (enables UART communication)");
  }
}

void FlexiSpotDesk::dump_config() {
  ESP_LOGCONFIG(TAG, "FlexiSpot Desk:");
  ESP_LOGCONFIG(TAG, "  Wake pin configured: %s", this->wake_pin_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Nudge duration: %u ms", this->nudge_duration_ms_);
  ESP_LOGCONFIG(TAG, "  Preset hold: %u ms", this->preset_hold_ms_);
  if (this->height_sensor_ != nullptr) {
    LOG_SENSOR("  ", "Height", this->height_sensor_);
  }
}

void FlexiSpotDesk::loop() {
  uint32_t now = millis();

  this->read_uart_();

  switch (this->state_) {
    case DeskState::BOOT:
      if (now - this->boot_time_ >= BOOT_DELAY_MS) {
        ESP_LOGI(TAG, "Boot delay complete, sending initial M command");
        const uint8_t m_cmd[] = {0x9B, 0x06, 0x02, 0x20, 0x00, 0xAC, 0xB8, 0x9D};
        this->send_packet_(m_cmd, sizeof(m_cmd));
        this->transition_to_(DeskState::IDLE);
      }
      break;

    case DeskState::IDLE:
      if (now - this->last_idle_send_ >= IDLE_POLL_MS) {
        this->send_packet_(IDLE_PACKET, sizeof(IDLE_PACKET));
        this->last_idle_send_ = now;
      }
      break;

    case DeskState::WAKING_LOW:
      if (now - this->wake_low_start_ >= WAKE_LOW_MS) {
        if (this->wake_pin_ != nullptr) {
          this->wake_pin_->digital_write(true);
          this->wake_pin_high_ = true;
        }
        this->wake_high_start_ = millis();
        this->transition_to_(DeskState::WAKING_HIGH);
      }
      break;

    case DeskState::WAKING_HIGH:
      if (now - this->wake_high_start_ >= WAKE_HIGH_MS) {
        ESP_LOGD(TAG, "Wake sequence complete, entering active state");
        this->command_start_time_ = millis();
        this->transition_to_(DeskState::ACTIVE);
      }
      break;

    case DeskState::ACTIVE:
      if (this->pending_command_ >= 0) {
        uint32_t max_hold = this->is_continuous_command_(this->pending_command_)
                                ? this->nudge_duration_ms_
                                : this->preset_hold_ms_;
        if (now - this->command_start_time_ >= max_hold) {
          ESP_LOGD(TAG, "Command %d hold complete, releasing", this->pending_command_);
          this->pending_command_ = -1;
        }
      }

      if (this->pending_command_ < 0 &&
          now - this->last_height_change_ >= ACTIVE_TIMEOUT_MS) {
        ESP_LOGI(TAG, "No activity, returning to idle");
        this->transition_to_(DeskState::IDLE);
      }
      break;
  }
}

void FlexiSpotDesk::request_command(CommandIndex cmd) {
  if (cmd >= CMD_COUNT) {
    ESP_LOGW(TAG, "Invalid command index: %d", cmd);
    return;
  }

  ESP_LOGI(TAG, "Command requested: %d", cmd);
  this->pending_command_ = cmd;

  if (this->state_ == DeskState::IDLE || this->state_ == DeskState::BOOT) {
    if (this->wake_pin_ != nullptr) {
      this->wake_pin_->digital_write(false);
      this->wake_pin_high_ = false;
    }
    this->wake_low_start_ = millis();
    this->transition_to_(DeskState::WAKING_LOW);
  } else if (this->state_ == DeskState::ACTIVE) {
    this->command_start_time_ = millis();
  }
}

void FlexiSpotDesk::stop_command() {
  if (this->pending_command_ >= 0) {
    ESP_LOGD(TAG, "Stopping command %d", this->pending_command_);
    this->pending_command_ = -1;
  }
}

void FlexiSpotDesk::read_uart_() {
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);

    if (this->rx_len_ == 0) {
      if (byte == 0x9B) {
        this->rx_buf_[this->rx_len_++] = byte;
      }
      continue;
    }

    this->rx_buf_[this->rx_len_++] = byte;

    if (this->rx_len_ == 2) {
      if (this->rx_buf_[1] > sizeof(this->rx_buf_) - 2) {
        ESP_LOGW(TAG, "Packet length %d too large, resetting", this->rx_buf_[1]);
        this->rx_len_ = 0;
      }
      continue;
    }

    size_t expected = this->rx_buf_[1] + 2;
    if (this->rx_len_ >= expected) {
      if (byte == 0x9D) {
        this->process_packet_();
      } else {
        ESP_LOGD(TAG, "Bad end byte: 0x%02X", byte);
      }
      this->rx_len_ = 0;
    }
  }
}

void FlexiSpotDesk::process_packet_() {
  uint8_t msg_type = this->rx_buf_[2];
  uint8_t msg_len = this->rx_buf_[1];

  if (msg_type == 0x12 && (msg_len == 7 || msg_len == 10)) {
    this->handle_height_packet_();
  } else if (msg_type == 0x11) {
    this->handle_poll_packet_();
  } else {
    ESP_LOGV(TAG, "Unknown packet type: 0x%02X (len=%d)", msg_type, msg_len);
  }
}

void FlexiSpotDesk::handle_poll_packet_() {
  if (this->state_ == DeskState::ACTIVE && this->pending_command_ >= 0) {
    ESP_LOGV(TAG, "Poll -> command %d", this->pending_command_);
    this->send_packet_(COMMANDS[this->pending_command_].data, 8);
  }
}

void FlexiSpotDesk::handle_height_packet_() {
  uint8_t d1_raw = this->rx_buf_[3];
  uint8_t d2_raw = this->rx_buf_[4];
  uint8_t d3_raw = this->rx_buf_[5];

  ESP_LOGV(TAG, "Height raw: [0x%02X 0x%02X 0x%02X]", d1_raw, d2_raw, d3_raw);

  int d1 = this->decode_7seg_(d1_raw);
  int d2 = this->decode_7seg_(d2_raw);
  int d3 = this->decode_7seg_(d3_raw);

  if (d1 == -2 && d2 == -2 && d3 == -2) return;
  if (d1 == -2) d1 = 0;

  if (d1 == -3 || d2 == -3 || d3 == -3) {
    ESP_LOGD(TAG, "Desk displaying dash (resetting)");
    return;
  }

  if (d1 < 0 || d2 < 0 || d3 < 0) {
    ESP_LOGD(TAG, "Unknown display data: 0x%02X 0x%02X 0x%02X", d1_raw, d2_raw, d3_raw);
    return;
  }

  if (d1 == 0 && d2 == 0 && d3 == 0) return;

  int raw = d1 * 100 + d2 * 10 + d3;
  float height;
  if (this->has_decimal_(d2_raw)) {
    height = raw / 10.0f;
  } else {
    height = static_cast<float>(raw);
  }

  if (height < HEIGHT_MIN || height > HEIGHT_MAX) {
    ESP_LOGD(TAG, "Height %.1f outside valid range, discarding", height);
    return;
  }

  if (height != this->current_height_) {
    this->last_height_change_ = millis();
    if (this->current_height_ > 0 && this->state_ == DeskState::IDLE) {
      ESP_LOGI(TAG, "Height change detected, entering active state");
      this->transition_to_(DeskState::ACTIVE);
    }
    this->current_height_ = height;
  }

  if (!this->got_initial_height_) {
    this->got_initial_height_ = true;
    ESP_LOGI(TAG, "Initial height: %.1f", height);
  }

  if (this->height_sensor_ != nullptr &&
      this->current_height_ != this->last_published_height_ &&
      this->current_height_ > 0) {
    this->height_sensor_->publish_state(this->current_height_);
    this->last_published_height_ = this->current_height_;
  }
}

void FlexiSpotDesk::send_packet_(const uint8_t *data, size_t len) {
  this->write_array(data, len);
  this->flush();
}

void FlexiSpotDesk::transition_to_(DeskState new_state) {
  if (this->state_ == new_state) return;

  const char *state_names[] = {"BOOT", "IDLE", "WAKING_LOW", "WAKING_HIGH", "ACTIVE"};
  ESP_LOGD(TAG, "State: %s -> %s",
           state_names[static_cast<int>(this->state_)],
           state_names[static_cast<int>(new_state)]);

  this->state_ = new_state;
}

int FlexiSpotDesk::decode_7seg_(uint8_t byte) {
  uint8_t segments = byte & 0x7F;

  if (segments == 0x00) return -2;
  if (segments == SEG_DASH) return -3;

  for (int i = 0; i < 10; i++) {
    if (segments == SEG_PATTERNS[i]) return i;
  }

  return -1;
}

bool FlexiSpotDesk::has_decimal_(uint8_t byte) {
  return (byte & 0x80) != 0;
}

bool FlexiSpotDesk::is_continuous_command_(int cmd) {
  return cmd == CMD_UP || cmd == CMD_DOWN;
}

}  // namespace flexispot_desk
}  // namespace esphome
