"""Compile actual shared-bus methods with scripted bit-level call boundaries."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_expander_shared_transactions(tmp_path):
    text = (ROOT / "esphome/components/hatch_expander/hatch_expander.cpp").read_text()
    public = text[text.index("bool HatchExpander::read_device_registers("):text.index("void HatchExpander::mark_txn_")]
    low = text[text.index("bool HatchExpander::bb_write_reg_("):text.index("bool HatchExpander::bb_read_regs_(")]
    source = tmp_path / "expander.cpp"
    source.write_text(r'''#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#define ESP_LOGW(...) ((void)0)
constexpr int WRITE_ATTEMPTS=3;
constexpr uint32_t SETTLE_MS=20,RETRY_GAP_MS=10,MIN_GAP_MS=5;
class HatchExpander {
 public:
  bool ready_=true; uint8_t address_=0x58; int fail=-1,stage=0,stops=0,starts=0; bool permanent=false;
  std::vector<unsigned> bytes,gaps;
  void settle_() {}
  void mark_txn_(uint32_t gap) { gaps.push_back(gap); }
  bool bb_start_() { ++starts; return stage++!=fail; }
  bool bb_stop_() { ++stops;return stage++!=fail; }
  bool bb_write_byte_(uint8_t b) { bytes.push_back(b);return stage++!=fail && !permanent; }
  void bb_fail_(const char *) { bb_stop_(); }
  bool read_regs_(uint8_t reg,uint8_t *data,size_t n) {
    bytes.push_back(address_);bytes.push_back(reg);bytes.push_back(unsigned(n));
    for(size_t i=0;i<n;++i)data[i]=uint8_t(i);
    return !permanent;
  }
  bool read_device_registers(uint8_t,uint8_t,uint8_t *,size_t);
  bool write_device_register(uint8_t,uint8_t,uint8_t);
  bool write_device_registers(uint8_t,uint8_t,const uint8_t *,size_t);
  bool probe_device(uint8_t);
  bool bb_write_reg_(uint8_t,uint8_t);
  bool bb_write_regs_(uint8_t,const uint8_t *,size_t);
};
''' + public + low + r'''
int main() {
  uint8_t data[241]{};for(unsigned i=0;i<240;++i)data[i]=i;
  for(size_t count:{size_t(1),size_t(2),size_t(32),size_t(240)}) {
    HatchExpander bus;assert(bus.read_device_registers(0x10,0,data,count));
    assert(bus.address_==0x58);
    assert((bus.bytes==std::vector<unsigned>{0x10,0,unsigned(count)}));
    for(size_t i=0;i<count;++i)assert(data[i]==uint8_t(i));
  }
  {HatchExpander bus;bus.permanent=true;
    assert(!bus.read_device_registers(0x10,0,data,240));assert(bus.address_==0x58);}
  {HatchExpander bus;
    assert(!bus.read_device_registers(0x10,0,data,241));
    assert(!bus.read_device_registers(0x10,0,data,0));
    assert(!bus.read_device_registers(0x10,0,nullptr,1));assert(bus.bytes.empty());}
  for(int failure=-1;failure<244;++failure) {
    HatchExpander bus;bus.fail=failure;
    assert(bus.write_device_registers(0x10,0,data,240));
    assert(bus.address_==0x58);
    assert(bus.stops>=1 && bus.starts<=3);
    if(failure<0) {
      assert(bus.starts==1 && bus.stops==1 && bus.bytes.size()==242);
      assert(bus.bytes[0]==0x20 && bus.bytes[1]==0);
      for(unsigned i=0;i<240;++i)assert(bus.bytes[i+2]==i);
    }
  }
  for(int failure=-1;failure<3;++failure) {
    HatchExpander bus;bus.fail=failure;
    assert(bus.probe_device(0x60)==(failure<0));
    assert(bus.address_==0x58 && bus.stops==1 && bus.starts==1);
    assert(bus.bytes.size()==(failure==0?0:1));
    if(!bus.bytes.empty())assert(bus.bytes[0]==0xc0);
  }
  { HatchExpander bus;bus.permanent=true;
    assert(!bus.write_device_registers(0x10,0,data,240));
    assert(bus.address_==0x58 && bus.starts==3 && bus.stops==3);
    assert(bus.gaps.size()==3);
    for(auto gap:bus.gaps)assert(gap==RETRY_GAP_MS);
  }
  { HatchExpander bus;assert(bus.write_device_register(0x10,0x7a,0x20));
    assert((bus.bytes==std::vector<unsigned>{0x20,0x7a,0x20}));
  }
  for(bool ready:{false,true}) for(uint8_t address:{0,7,0x58,0x78,0xff}) {
    HatchExpander bus;bus.ready_=ready;
    assert(!bus.probe_device(address));
    assert(!bus.write_device_registers(address,0,data,1));
    assert(bus.starts==0 && bus.address_==0x58);
  }
  { HatchExpander bus;bus.ready_=false;
    assert(!bus.probe_device(0x10) && !bus.write_device_register(0x10,0,1));
    assert(bus.starts==0);
  }
  { HatchExpander bus;
    assert(!bus.write_device_registers(0x10,0,nullptr,1));
    assert(!bus.write_device_registers(0x10,0,data,0));
    assert(!bus.write_device_registers(0x10,0,data,241));assert(bus.starts==0);
  }
}
''')
    compiler = shutil.which("clang++") or shutil.which("g++")
    assert compiler
    executable = tmp_path / "expander"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
