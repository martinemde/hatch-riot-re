#pragma once
#include "greenpak_boot_controller.h"
#include "greenpak_configuration.h"
#include "register_transport.h"
namespace esphome::hatch_plus_power {
// Bus read/write/probe use seven-bit
// addresses and return bool. Clock.delay(ticks) preserves stock tick units.
// A bool bus cannot represent queue failure separately from completion failure.
template<class Bus,class Clock> class GreenPakBootTransport : public GreenpakProgramIO, public GreenpakDiscoveryIO, public GreenpakBootIO {
 public:
  GreenPakBootTransport(Bus &bus,Clock &clock,unsigned &found,bool allow_control,
                        bool allow_nvm_programming=false)
      : bus_(bus),clock_(clock),found_(found),allow_control_(allow_control),
        allow_nvm_programming_(allow_nvm_programming) {}
  // First boot transport/permission fault; controller execution is not stopped.
  int last_error() const {return last_error_;}
  unsigned address7() const {return found_==0 ? 0x10 : 0x08;}
  int probe(unsigned stock_address) override {
    if (!allow_control_) return -1;
    return bus_.probe(uint8_t(stock_address>>1)) ? 0 : 1;
  }
  int find() override {return greenpak_discover(allow_control_,found_,*this);}
  int rediscover() override {return find();}
  bool allocate() override {return allow_control_;}
  int read(unsigned reg,uint8_t &value) override {
    return allow_control_ && bus_.read(address7(),uint8_t(reg),&value,1) ? 0 : -1;
  }
  int write(const uint8_t *data,unsigned size) override {
    if (!allow_control_ || size<2) return -1;
    return bus_.write(uint8_t(address7() | (nvm_ && size==17 ? 2 : 0)),data[0],data+1,size-1) ? 0 : -1;
  }
  int compare_bytes(const uint8_t *base,unsigned size) override {
    if (!allow_control_ || size!=240) return -7;
    uint8_t data[240];
    if (!bus_.read(uint8_t(address7()|2),0,data,size)) return -7;
    for(unsigned j=0;j<size;j++) if(data[j]!=base[j]) return 1;
    return 0;
  }
  int compare_nvm() override {
    return track_error(greenpak_compare_nvm(allow_control_,found_,GREENPAK_BASE,*this));
  }
  int program(bool nvm) override {
    if (!allow_control_) return track_error(-100);
    // A mismatch remains visible but cannot silently authorize persistent erase.
    if (nvm && !allow_nvm_programming_) return track_error(-101);
    // NVM wire address has stock flag4, i.e. seven-bit flag2.
    nvm_=nvm;
    int result=nvm ? greenpak_program_nvm(allow_control_,found_,GREENPAK_BASE,*this)
                   : greenpak_program_ram(allow_control_,found_,GREENPAK_BASE,*this);
    nvm_=false;
    return track_error(result);
  }
  int reset() override {return track_error(greenpak_reset(allow_control_,found_,*this));}
  int latch(uint8_t &value) override {
    if (!allow_control_) return -1;
    GreenPakRegisterTransport<Bus> regs(bus_,address7());
    int result=regs.read(0x3b7);if(result<0)return -1;value=result;return 0;
  }
  int virtual_input(bool set,unsigned mask) override {
    if (!allow_control_) return -1;
    GreenPakRegisterTransport<Bus> regs(bus_,address7());
    return (set ? regs.set(mask) : regs.clear(mask)) ? 0 : -1;
  }
  void delay(unsigned ticks) override {if(allow_control_)clock_.delay(ticks);}
 private:
  int track_error(int result) {
    if(result<0 && !last_error_)last_error_=result;
    return result;
  }
  int last_error_{0};
  Bus &bus_;Clock &clock_;unsigned &found_;bool allow_control_;bool allow_nvm_programming_;bool nvm_{false};
};
}
