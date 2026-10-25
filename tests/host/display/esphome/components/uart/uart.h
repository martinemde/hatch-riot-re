#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "esphome/core/hal.h"
namespace esphome::uart {
constexpr int UART_CONFIG_PARITY_ODD = 1;
struct Frame { uint32_t at; std::vector<uint8_t> bytes; };
class UARTDevice {
 public:
  std::vector<Frame> frames;
  void write_array(const uint8_t *data, size_t size) { frames.push_back({millis(), {data, data + size}}); }
  void flush() {}
  void check_uart_settings(int, int, int, int) {}
};
}
