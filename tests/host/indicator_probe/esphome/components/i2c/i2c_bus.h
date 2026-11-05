#pragma once
#include <cstddef>
#include <cstdint>
namespace esphome::i2c {
enum ErrorCode { ERROR_OK=0, ERROR_NOT_ACKNOWLEDGED=2, ERROR_TIMEOUT=3, ERROR_UNKNOWN=6 };
class I2CBus { public: virtual ~I2CBus()=default;
virtual ErrorCode write_readv(uint8_t, const uint8_t *, size_t, uint8_t *, size_t)=0;
};
}
