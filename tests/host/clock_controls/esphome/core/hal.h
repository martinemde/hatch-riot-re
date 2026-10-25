#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t clock_ms = 0;
inline uint32_t millis() { return clock_ms; }
}
