#pragma once
#include <cstdint>
inline uint32_t clock_ms=0;
namespace esphome { inline uint32_t millis() { return clock_ms; } }
