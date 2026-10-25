#pragma once
#include <cstdint>
#include <array>
namespace esphome { namespace hatch_plus_power {
// No hardware adapter. Every entry requires an explicit explicit control gate.
struct GreenpakBootIO {
  virtual ~GreenpakBootIO() = default;
  virtual int find() = 0;
  virtual int compare_nvm() = 0;
  virtual int program(bool nvm) = 0;
  virtual int reset() = 0;
};
inline void greenpak_boot(bool allow_control, uint32_t adc, GreenpakBootIO &io) {
  if (!allow_control) return;
  if (uint32_t(io.find()) >= 2) return;
  uint32_t scaled=adc*25;
  int result=io.compare_nvm();
  if (result==0) { if (scaled>0x107ba) io.reset(); }
  else if (result==1 && io.program(true)==0) {
    if (scaled>0x107ba) io.reset();
    else io.program(false);
  }
}
struct GreenpakRamIO {
  virtual ~GreenpakRamIO() = default;
  virtual bool allocate() = 0;
  // Return zero on successful queued/completed transaction.
  virtual int write(const uint8_t *data,unsigned size) = 0;
  virtual int latch(uint8_t &value) = 0;
  virtual int virtual_input(bool set,unsigned mask) = 0;
};
inline int greenpak_program_ram(bool allow_control, unsigned &found,
                               const std::array<uint8_t,240> &base,
                               GreenpakRamIO &io) {
  if (!allow_control) return -100;
  if (found==2) return -2;
  if (!io.allocate()) return -3;
  const uint8_t addresses[]={0x6a,0x6b,0x6c,0x39,0x3a,0x3b,0xca};
  const uint8_t values[]={0,0x80,0,0xec,0x0f,0,uint8_t(found ? 1 : 2)};
  for (unsigned j=0;j<7;j++) {
    uint8_t pair[]={addresses[j],values[j]};
    if (io.write(pair,2)) return -4;
  }
  std::array<uint8_t,241> payload{};
  for (unsigned j=0;j<240;j++) payload[j+1]=base[j];
  for (unsigned j=0;j<7;j++) payload[addresses[j]+1]=values[j];
  if (io.write(payload.data(),payload.size())) return -4;
  uint8_t value=0;
  if (io.latch(value)) return -4;
  if (!value && (io.virtual_input(true,0x40) || io.virtual_input(false,0x40))) return -4;
  for (unsigned j=0;j<7;j++) {
    uint8_t pair[]={addresses[j],base[addresses[j]]};
    if (io.write(pair,2)) return -4;
  }
  found=0;
  return 0;
}
struct GreenpakDiscoveryIO {
  virtual ~GreenpakDiscoveryIO() = default;
  // 0 ACK, 1 completion NACK/error, -1 failed to queue.
  virtual int probe(unsigned stock_address_byte) = 0;
};
inline unsigned greenpak_discover(bool allow_control,unsigned &found,GreenpakDiscoveryIO &io) {
  if (!allow_control) return found;
  int result=io.probe(0x20);
  if (result==0) return found=0;
  if (result<0) return found=2;
  result=io.probe(0x10);
  return found=result==0 ? 1 : 2;
}
inline unsigned greenpak_address(unsigned found) {return found==0 ? 0x10 : 0x08;}
struct GreenpakProgramIO : GreenpakRamIO {
  virtual int read(unsigned reg, uint8_t &value) = 0;
  virtual void delay(unsigned ticks) = 0;
  virtual int rediscover() = 0;
  virtual int compare_bytes(const uint8_t *base,unsigned size) = 0;
};
inline int greenpak_reset(bool allow_control,unsigned found,GreenpakProgramIO &io) {
  if (!allow_control) return -100;
  if (found==2) return -2;
  uint8_t value=0;
  if (io.read(0xc8,value)) return -7;
  uint8_t pair[]={0xc8,uint8_t(value|2)};
  if (io.write(pair,2)) return -4;
  io.delay(3);
  // Stock returns the successful write status; rediscovery result is ignored.
  io.rediscover();
  return 0;
}
inline int greenpak_program_nvm(bool allow_control,unsigned found,
                               const std::array<uint8_t,240> &base,GreenpakProgramIO &io) {
  if (!allow_control) return -100;
  if (found==2) return -2;
  if (!io.allocate()) return -3;
  for (unsigned page=0;page<15;page++) {
    uint8_t erase[]={0xe3,uint8_t(0x80|page)};
    if (io.write(erase,2)) return -4;
    io.delay(20);
  }
  for (unsigned offset=0;offset<240;offset+=16) {
    uint8_t chunk[17]; chunk[0]=offset;
    for (unsigned j=0;j<16;j++) chunk[j+1]=base[offset+j];
    if (io.write(chunk,17)) return -4;
    io.delay(20);
  }
  return 0;
}
inline int greenpak_compare_nvm(bool allow_control,unsigned found,
                               const std::array<uint8_t,240> &base,GreenpakProgramIO &io) {
  if (!allow_control) return -100;
  if (found==2) return -2;
  if (!io.allocate()) return -3;
  return io.compare_bytes(base.data(),base.size());
}
}} // namespace esphome::hatch_plus_power
