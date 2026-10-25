#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace esphome::uart {
inline constexpr int UART_CONFIG_PARITY_ODD = 1;
class UARTDevice {
 public:
  std::vector<std::vector<uint8_t>> frames;
  unsigned flushes{0};
  void write_array(const uint8_t *data, size_t size) { frames.emplace_back(data, data + size); }
  void flush() { ++flushes; }
  void check_uart_settings(int, int, int, int) {}
};
}
