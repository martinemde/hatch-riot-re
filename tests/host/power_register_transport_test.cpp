#include "esphome/components/hatch_plus_power/register_transport.h"
#include "esphome/components/hatch_plus_power/wireless_controller.h"
#include <array>
#include <cassert>
#include <vector>
using namespace esphome::hatch_plus_power;
struct Transaction { bool write; uint8_t address, reg; std::vector<uint8_t> bytes; };
struct Bus {
  std::array<uint8_t, 256> ram{};
  std::vector<Transaction> trace;
  int fail = -1;
  bool probe(uint8_t address) { assert(address == 0x60); return fail != 0; }
  bool read(uint8_t address, uint8_t reg, uint8_t *bytes, size_t length) {
    for (size_t i=0; i<length; ++i) bytes[i]=ram[reg+i];
    trace.push_back({false,address,reg,{bytes,bytes+length}});
    return int(trace.size())-1 != fail;
  }
  bool write(uint8_t address, uint8_t reg, const uint8_t *bytes, size_t length) {
    trace.push_back({true,address,reg,{bytes,bytes+length}});
    if (int(trace.size())-1 == fail) return false;
    for (size_t i=0; i<length; ++i) ram[reg+i]=bytes[i];
    return true;
  }
};
int main() {
  { Bus bus; GreenPakRegisterTransport<Bus> gp(bus,0x10);
    gp.set_available(false);
    assert(gp.read(0x3a5)==-2 && gp.read_pin(0x3a6)==-2 && gp.read()==-2);
    assert(gp.read_bits(0x462,6)==-2 && gp.read_bits(0,0)==-2);
    assert(!gp.set(0x40) && !gp.clear(0x40) && !gp.reference(52));
    assert(!gp.write_bits(0x462,6,52) && bus.trace.empty());
    gp.set_address(0x08);
    assert(gp.read()==-2 && bus.trace.empty());
    gp.set_available(true);
    assert(gp.read()==0 && bus.trace.size()==1 && bus.trace[0].address==0x08);
    assert(gp.read_bits(0,0)==-1);
  }
  for (uint16_t bit : {0x3a5,0x3a6,0x3a8,0x3b2,0x3b7,0x3db}) {
    Bus bus; GreenPakRegisterTransport<Bus> gp(bus,0x10);
    bus.ram[bit>>3]=1U<<(bit&7);
    assert(gp.read(bit)==1); assert(bus.trace[0].reg==(bit>>3));
    assert(bus.trace[0].address==0x10 && bus.trace[0].bytes.size()==1);
    bus.fail=1; assert(gp.read(bit)==-1);
  }
  for (bool set : {false,true}) for (int fail=-1; fail<2; ++fail) {
    Bus bus; bus.ram[0x7a]=0xa5; bus.fail=fail;
    GreenPakRegisterTransport<Bus> gp(bus,0x10);
    assert((set ? gp.set(0x18) : gp.clear(0x18)) == (fail<0));
    assert(bus.trace.size()==(fail==0?1:2));
    for (auto &tx:bus.trace) assert(tx.reg==0x7a && tx.bytes.size()==1);
    assert(bus.ram[0x7a]==(fail<0?(set?0xbd:0xa5):0xa5));
  }
  for (int fail=-1; fail<2; ++fail) for (unsigned value=0;value<256;++value) {
    Bus bus; bus.fail=fail; bus.ram[0x8c]=3;
    GreenPakRegisterTransport<Bus> gp(bus,0x10);
    assert(gp.reference(value)==(fail<0));
    assert(bus.trace.size()==(fail==0?1:2));
    for (auto &tx:bus.trace) assert(tx.reg==0x8c && tx.bytes.size()==1);
    assert(bus.ram[0x8c]==(fail<0?uint8_t((value<<2)|3):3));
  }
  { Bus bus; GreenPakRegisterTransport<Bus> gp(bus,0x10);
    bus.ram[0x20]=0xa5;bus.ram[0x21]=0x5a;
    assert(gp.read_bits(0x104,8)==0xaa);
    assert(gp.write_bits(0x104,8,0x12));
    assert(bus.ram[0x20]==0x25 && bus.ram[0x21]==0x51);
    assert(gp.read_bits(0,0)==-1 && !gp.write_bits(0,9,0));
  }
  for (int fail=-1;fail<2;++fail) {
    Bus bus; bus.fail=fail; bus.ram[0x15]=0xff;
    WirelessController<Bus> receiver(bus,true);
    assert(receiver.clear_receiver_bits(0x15,2)==(fail<0?0:fail==0?-4:-5));
    assert(bus.trace.size()==(fail==0?1:2));
    assert(bus.ram[0x15]==(fail<0?0xfd:0xff));
  }
  { Bus bus; WirelessController<Bus> receiver(bus);
    assert(receiver.temperature()==-3 && receiver.receiver_status()==-3);
    assert(receiver.set_receiver_voltage(-1)==-3);
    assert(receiver.clear_receiver_bits(0x15,2)==-3 && bus.trace.empty());
    assert(receiver.find_receiver()==0xc0);
    bus.fail=0; assert(receiver.find_receiver()==-2);
    bus.fail=-1; bus.ram[0x03]=3;assert(receiver.receiver_status()==3);
    bus.fail=1; assert(receiver.receiver_status()==-4);
  }
  assert(receiver_temperature_scaled(0)==100);
  assert(receiver_temperature_scaled(368638)==0);
  for (unsigned i=0;i<100;++i) {
    const auto upper=RECEIVER_TEMPERATURE_TABLE[i];
    const auto lower=RECEIVER_TEMPERATURE_TABLE[i+1];
    assert(receiver_temperature_scaled(upper)==int(i));
    assert(receiver_temperature_scaled(lower)==int(i+1));
    const auto midpoint=(upper+lower)/2;
    assert(receiver_temperature_scaled(midpoint)==int(i+1));
    assert(receiver_temperature_scaled(midpoint+1)==int(i));
  }
  for (unsigned code=0;code<1024;++code) {
    Bus bus; bus.ram[0x38]=(code&3)|0xfc;bus.ram[0x39]=code>>2;
    WirelessController<Bus> receiver(bus,true);
    assert(receiver.temperature()==receiver_temperature(code&3,code>>2));
    assert(bus.trace[0].address==0x60 && bus.trace[0].reg==0x38 && bus.trace[0].bytes.size()==2);
    bus.fail=1; assert(receiver.temperature()==-4);
  }
  { Bus bus; WirelessController<Bus> receiver(bus,true);
    assert(receiver.set_receiver_voltage(4.6)==0 && bus.ram[0x1d]==118);
    assert(receiver.set_receiver_voltage(-1)==-6);
    assert(receiver.set_receiver_voltage(INFINITY)==-6);
    assert(receiver.set_receiver_voltage(NAN)==-6 && bus.trace.size()==1);
    bus.fail=1; assert(receiver.set_receiver_voltage(4.6)==-5);
  }
}
