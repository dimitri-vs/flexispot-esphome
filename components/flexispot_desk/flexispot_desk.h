#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace flexispot_desk {

enum class DeskState {
  BOOT,
  IDLE,
  WAKING_LOW,
  WAKING_HIGH,
  ACTIVE,
};

enum CommandIndex : uint8_t {
  CMD_STAND = 0,
  CMD_SIT = 1,
  CMD_PRESET_1 = 2,
  CMD_PRESET_2 = 3,
  CMD_UP = 4,
  CMD_DOWN = 5,
  CMD_MEMORY = 6,
  CMD_COUNT = 7,
};

struct CommandPacket {
  uint8_t data[8];
};

static const CommandPacket COMMANDS[] = {
    {{0x9B, 0x06, 0x02, 0x10, 0x00, 0xAC, 0xAC, 0x9D}},  // Stand
    {{0x9B, 0x06, 0x02, 0x00, 0x01, 0xAC, 0x60, 0x9D}},  // Sit
    {{0x9B, 0x06, 0x02, 0x04, 0x00, 0xAC, 0xA3, 0x9D}},  // Preset 1
    {{0x9B, 0x06, 0x02, 0x08, 0x00, 0xAC, 0xA6, 0x9D}},  // Preset 2
    {{0x9B, 0x06, 0x02, 0x01, 0x00, 0xFC, 0xA0, 0x9D}},  // Up
    {{0x9B, 0x06, 0x02, 0x02, 0x00, 0x0C, 0xA0, 0x9D}},  // Down
    {{0x9B, 0x06, 0x02, 0x20, 0x00, 0xAC, 0xB8, 0x9D}},  // Memory
};

static const uint8_t IDLE_PACKET[] = {0x9B, 0x06, 0x02, 0x00, 0x00, 0x6C, 0xA1, 0x9D};

// Valid range in display units (covers both inches and cm)
static const float HEIGHT_MIN = 20.0f;
static const float HEIGHT_MAX = 200.0f;

class FlexiSpotDesk : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_wake_pin(GPIOPin *pin) { this->wake_pin_ = pin; }
  void set_height_sensor(sensor::Sensor *sensor) { this->height_sensor_ = sensor; }
  void set_nudge_duration(uint32_t ms) { this->nudge_duration_ms_ = ms; }
  void set_preset_hold(uint32_t ms) { this->preset_hold_ms_ = ms; }

  void request_command(CommandIndex cmd);
  void stop_command();

 protected:
  GPIOPin *wake_pin_{nullptr};
  sensor::Sensor *height_sensor_{nullptr};

  DeskState state_{DeskState::BOOT};
  bool wake_pin_high_{false};

  uint8_t rx_buf_[32];
  size_t rx_len_{0};

  int pending_command_{-1};
  uint32_t command_start_time_{0};

  uint32_t boot_time_{0};
  uint32_t wake_low_start_{0};
  uint32_t wake_high_start_{0};
  uint32_t last_height_change_{0};
  uint32_t last_idle_send_{0};
  float current_height_{0.0f};
  float last_published_height_{-1.0f};
  bool got_initial_height_{false};

  static const uint32_t BOOT_DELAY_MS = 10000;
  static const uint32_t WAKE_LOW_MS = 100;
  static const uint32_t WAKE_HIGH_MS = 1100;
  static const uint32_t IDLE_POLL_MS = 3000;
  static const uint32_t ACTIVE_TIMEOUT_MS = 5000;

  // Configurable from YAML (`preset_hold:` / `nudge_duration:`).
  // Defaults match the values these had when they were `static const`.
  uint32_t preset_hold_ms_{1000};
  uint32_t nudge_duration_ms_{5000};

  void read_uart_();
  void process_packet_();
  void handle_height_packet_();
  void handle_poll_packet_();
  void send_packet_(const uint8_t *data, size_t len);
  void transition_to_(DeskState new_state);
  int decode_7seg_(uint8_t byte);
  bool has_decimal_(uint8_t byte);
  bool is_continuous_command_(int cmd);
};

class DeskButton : public button::Button, public Component {
 public:
  void set_parent(FlexiSpotDesk *parent) { this->parent_ = parent; }
  void set_command_index(uint8_t idx) { this->command_index_ = idx; }

  void press_action() override {
    this->parent_->request_command(static_cast<CommandIndex>(this->command_index_));
  }

 protected:
  FlexiSpotDesk *parent_{nullptr};
  uint8_t command_index_{0};
};

class DeskHeightSensor : public sensor::Sensor, public Component {
 public:
  void dump_config() override {}
};

}  // namespace flexispot_desk
}  // namespace esphome
