"""Actual owner LED methods: discovery gates and field-preserving writes."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def test_indicator_owner(tmp_path):
    source = (ROOT / "esphome/components/hatch_plus_power/hatch_plus_power.cpp").read_text()
    header = (ROOT / "esphome/components/hatch_plus_power/hatch_plus_power.h").read_text()
    def method(text, signature):
        start = text.index(signature)
        brace = text.index("{", start)
        depth, end = 1, brace + 1
        while depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        return text[start:end]
    methods = method(source, "bool HatchPlusPower::set_indicator(") + "\n" + method(source, "float HatchPlusPower::indicator_battery_percent(")
    methods += "\n" + method(source, "bool HatchPlusPower::prepare_power_off(")
    ready = method(header, "bool indicators_ready()")
    fixture = r"""
#define ESP_LOGW(...) ((void)0)
#include <cassert>
#include <cmath>
#include "esphome/components/hatch_plus_power/register_transport.h"
#include "esphome/components/hatch_plus_power/greenpak_boot_controller.h"
using namespace esphome::hatch_plus_power;
struct Bus {
 uint8_t bytes[256]{}; unsigned reads=0,writes=0; bool ok=true,write_ok=true;
 bool read(uint8_t address,uint8_t reg,uint8_t *data,size_t n) {
  assert(address==0x10); ++reads; if(!ok)return false;
  for(size_t i=0;i<n;i++)data[i]=bytes[reg+i];return true;
 }
 bool write(uint8_t address,uint8_t reg,const uint8_t *data,size_t n) {
  assert(address==0x10); ++writes;if(!write_ok)return false;
  for(size_t i=0;i<n;i++)bytes[reg+i]=data[i];return true;
 }
};
struct Runtime {
 struct State {struct Battery{int reported=42;} battery;} state;
 Runtime &controller(){return *this;} const State &task_state(){return state;}
 bool has_report(){return report;} bool report=true;
};
class HatchPlusPower {
 public:
 bool indicator_blank_failed_=false;
 bool allow_control_=true,stopped_=false,preparing_=false,failed=false;
 unsigned found_=0; Bus *bus_;Runtime *runtime_;
 bool is_failed() const{return failed;}
 READY
 bool set_indicator(uint8_t,uint8_t);
 bool prepare_power_off();
 float indicator_battery_percent() const;
};
METHODS
int main(){
 Bus bus;Runtime runtime;HatchPlusPower owner;owner.bus_=&bus;owner.runtime_=&runtime;
 for(unsigned index=0;index<3;index++)for(unsigned state=0;state<4;state++){
  for(auto &b:bus.bytes)b=0xa5;
  assert(owner.set_indicator(index,state));
  const unsigned fields[]={0x192,0x198,0x1a4};const unsigned codes[]={0x11,0,0x17,0x10};
  unsigned bit=fields[index],shift=bit&7,reg=bit>>3,mask=0x3f<<shift;
  unsigned actual=bus.bytes[reg]|(unsigned(bus.bytes[reg+1])<<8);
  assert(((actual&mask)>>shift)==codes[state]);assert((actual&~mask)==(0xa5a5&~mask));
 }
 // Shutdown blanks even while the normal indicator publisher is suspended.
 owner.preparing_=true;
 for(auto &b:bus.bytes)b=0xa5;
 uint8_t before[256];for(unsigned i=0;i<256;i++)before[i]=bus.bytes[i];
 assert(owner.prepare_power_off());
 for(unsigned bit=0;bit<2048;bit++){
  bool expected=bool((before[bit/8]>>(bit%8))&1);
  // Stock OFF code is 0x11 (0x00 is green), applied independently to all three fields.
  for(unsigned field:{0x192u,0x198u,0x1a4u})
   if(bit>=field&&bit<field+6)expected=bool((0x11>>(bit-field))&1);
  bool value=(bus.bytes[bit/8]>>(bit%8))&1;
  assert(value==expected);
 }
 owner.preparing_=false;
 owner.found_=2;auto no_device_reads=bus.reads;
 assert(!owner.prepare_power_off()&&bus.reads==no_device_reads);owner.found_=0;
 auto reads=bus.reads,writes=bus.writes;
 assert(!owner.set_indicator(3,0)&&!owner.set_indicator(0,4));
 for(unsigned found=1;found<3;found++){owner.found_=found;assert(!owner.set_indicator(0,1));}
 owner.found_=0;owner.allow_control_=false;assert(!owner.set_indicator(0,1));
 owner.allow_control_=true;owner.preparing_=true;assert(!owner.set_indicator(0,1));
 owner.preparing_=false;owner.stopped_=true;assert(!owner.set_indicator(0,1));
 owner.stopped_=false;owner.failed=true;assert(!owner.set_indicator(0,1));
 assert(bus.reads==reads&&bus.writes==writes);
 owner.failed=false;bus.ok=false;assert(!owner.set_indicator(0,1));assert(!owner.prepare_power_off());assert(bus.writes==writes);
 assert(owner.indicator_battery_percent()==42);runtime.report=false;assert(std::isnan(owner.indicator_battery_percent()));
 bus.ok=true;bus.write_ok=false;auto failed_reads=bus.reads,failed_writes=bus.writes;
 assert(!owner.prepare_power_off());assert(bus.reads==failed_reads+3&&bus.writes==failed_writes+3);
 owner.runtime_=nullptr;assert(!owner.set_indicator(0,1));assert(std::isnan(owner.indicator_battery_percent()));
}
"""
    path=tmp_path/"owner.cpp"
    path.write_text(fixture.replace("READY", ready).replace("METHODS",methods))
    exe=tmp_path/"owner"
    subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-misleading-indentation","-I",str(ROOT),str(path),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
