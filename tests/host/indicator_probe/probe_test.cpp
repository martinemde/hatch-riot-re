#include "hatch_plus_indicator_probe.h"
#include <cassert>
#include <vector>
using namespace esphome;
struct Bus : i2c::I2CBus {
  std::vector<int> calls;
  i2c::ErrorCode programmed{i2c::ERROR_OK}, blank{i2c::ERROR_OK}, reading{i2c::ERROR_OK}, status_reading{i2c::ERROR_OK};
  i2c::ErrorCode write_readv(uint8_t address, const uint8_t *tx, size_t n, uint8_t *rx, size_t m) override {
    calls.push_back(address);
    if (!n) { assert(!tx && !rx && !m); return address==0x10 ? programmed : blank; }
    assert(address==0x10 && n==1 && rx);
    assert((*tx==0x32 && m==4) || (*tx==0x74 && m==3));
    for (unsigned i=0;i<m;i++) rx[i]=uint8_t((*tx==0x32 ? 0x91 : 0x61)+i);
    return *tx==0x32 ? reading : status_reading;
  }
};
int main() {
  hatch_plus_indicator_probe::HatchPlusIndicatorProbe probe;
  Bus bus; probe.set_i2c_bus(&bus);
  uint8_t byte=0;
  assert(!probe.register_byte(0,byte));
  probe.update(); assert(probe.valid() && probe.programmed_address_ack() && !probe.blank_address_ack());
  assert((bus.calls==std::vector<int>{0x10,0x10,0x10}));
  assert(probe.status_valid() && probe.status_byte(2,byte) && byte==0x63);
  assert(!probe.status_byte(3,byte));
  assert(probe.register_byte(3,byte) && byte==0x94); assert(!probe.register_byte(4,byte));
  bus.calls.clear(); bus.reading=i2c::ERROR_TIMEOUT;
  probe.update(); assert(!probe.valid() && !probe.register_byte(0,byte));
  assert(probe.status_valid());
  assert((bus.calls==std::vector<int>{0x10,0x10,0x10}));
  bus.calls.clear(); bus.reading=i2c::ERROR_OK; bus.status_reading=i2c::ERROR_TIMEOUT;
  probe.update(); assert(probe.valid() && !probe.status_valid() && !probe.status_byte(0,byte));
  assert((bus.calls==std::vector<int>{0x10,0x10,0x10}));
  bus.calls.clear(); bus.programmed=i2c::ERROR_NOT_ACKNOWLEDGED;
  probe.update(); assert(!probe.valid() && !probe.status_valid() && !probe.programmed_address_ack() && probe.blank_address_ack());
  assert((bus.calls==std::vector<int>{0x10,0x08}));
  bus.calls.clear(); bus.blank=i2c::ERROR_NOT_ACKNOWLEDGED;
  probe.update(); assert(!probe.blank_address_ack() && !probe.valid());
  assert((bus.calls==std::vector<int>{0x10,0x08}));
  bus.calls.clear(); bus.programmed=i2c::ERROR_TIMEOUT;
  probe.update(); assert(!probe.valid() && !probe.blank_address_ack());
  assert((bus.calls==std::vector<int>{0x10}));
  bus.calls.clear(); probe.on_shutdown(); probe.update(); assert(bus.calls.empty() && !probe.valid());
}
