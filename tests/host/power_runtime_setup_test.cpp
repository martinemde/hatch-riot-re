#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include "esphome/components/hatch_plus_power/load_controller.h"
#include "esphome/components/hatch_plus_power/init_controller.h"
using namespace esphome::hatch_plus_power;
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
struct Config {bool adc_ready=true,post_ready=true;int raw=100,found=0,read_failure=-1,boot_error=0;uint32_t calibration_input=0;unsigned reads=0,boot_calls=0,runtime_calls=0;uint32_t runtime_initial_tick=0;bool init_failure=false;std::vector<unsigned> events;} config;
// Peripheral storage outlives reconstructed software owners. Boot discovery is
// mocked; the real power_init algorithm below owns RAM7a and retained init fields.
struct Peripheral {std::array<uint8_t,256> ram{},nvm{};uint16_t outputs=0;unsigned init_writes=0;} peripheral;
namespace hatch_expander {struct HatchExpander {}; }
struct NativeOwner {};
struct Bus {explicit Bus(NativeOwner&){} };
struct Adc {bool initialized=false;void adc_width(int){}void characterize(int,int,int,int){}void adc_attenuation(int,int){}bool ready(){return initialized?config.post_ready:config.adc_ready;}int raw(int){return config.raw;}int calibrate(uint32_t n){config.calibration_input=n;return int(n);}};
enum class LoadExpander {AW9110B};
template<class Owner>struct LoadRegisterTransport {LoadRegisterTransport(Owner&,LoadExpander){}int initialize(){config.events.push_back(0x500);if(config.read_failure>=0)return 8;peripheral.outputs&=~uint16_t(0x0500);return 0;}};
template<class B,class O>struct GreenPakBootTransport {int last_error(){return config.boot_error;}unsigned &found;GreenPakBootTransport(B&,O&,unsigned &f,bool,bool):found(f){}};
template<class Boot>void greenpak_boot(bool,int,Boot &boot){config.boot_calls++;boot.found=config.found;}
unsigned greenpak_address(unsigned n){return n;}
struct Runtime;
class HatchPlusPower {
public:
 using Runtime=::Runtime;using Bus=::Bus;
 std::unique_ptr<Bus>bus_;std::unique_ptr<Runtime>runtime_;std::unique_ptr<GreenPakBootTransport<Bus,HatchPlusPower>>boot_;
 std::unique_ptr<LoadRegisterTransport<hatch_expander::HatchExpander>>loads_;
 hatch_expander::HatchExpander expander_object,*expander_=&expander_object;
 NativeOwner native_owner,*power_bus_=&native_owner;
 void *light_=this,*player_=this,*speaker_=this,*display_=this;
 std::vector<uint8_t>pwm_channels_{0,1,2,3};Adc adc_;LoadState load_;
 unsigned found_=2;bool owner_fault_=false,allow_nvm_programming_=false;bool allow_control_=true,init_failed_=false,failed=false;
 uint32_t led_second_tick_=0;uint32_t tick(){return 42;}void mark_failed(){failed=true;}void setup();
};
struct Runtime {
 bool available=true;void set_available(bool value){available=value;}
 Adc &adc;HatchPlusPower &owner;InitState state{0xa5,0xdeadbeef};
 Runtime(Bus&,unsigned,Adc &a,HatchPlusPower&o,uint32_t initial_tick):adc(a),owner(o){config.runtime_calls++;config.runtime_initial_tick=initial_tick;}
 unsigned variant(){return 2;}void adc_width(unsigned x){adc.adc_width(x);}
 void characterize(unsigned a,unsigned b,unsigned c,unsigned d){adc.characterize(a,b,c,d);}
 void adc_attenuation(unsigned a,unsigned b){adc.adc_attenuation(a,b);}
 void configure_virtual(unsigned,unsigned,unsigned reg,unsigned value){
  if(!available||config.init_failure){owner.init_failed_=true;return;}
  peripheral.ram[reg]=value;peripheral.init_writes++;
 }
 void initialize(){power_init(state,*this);adc.initialized=true;}
};
inline int amplifier_request(LoadState &s,int arg,HatchPlusPower &){assert(s.amp_initialized&&arg==0);return 0;}
// OWNER_METHODS
int main(){
 // Cold owner then reconstructed warm owner with the same RAM/NVM. The actual
 // setup calls real power_init through Runtime; GreenPAK boot policy is mocked.
 for(int found: {0,1,2}) {
  peripheral={};peripheral.ram[0x55]=0x71;peripheral.nvm[0x55]=0x39;
  for(unsigned visit=0;visit<2;visit++) {
   peripheral.ram[0x7a]=uint8_t(0xa0+visit);peripheral.outputs=0xffff;
   config={};config.found=found;
   {
    HatchPlusPower owner;owner.setup();
    assert(!owner.failed&&owner.runtime_&&owner.found_==unsigned(found));
    assert(owner.runtime_->available==(found!=2));
    assert(owner.runtime_->state.flag_9084==0&&owner.runtime_->state.timer_9088==0);
    assert(peripheral.ram[0x7a]==(found==2?uint8_t(0xa0+visit):0x20));
    assert(peripheral.init_writes==(found==2?0:visit+1));
    assert(peripheral.ram[0x55]==0x71&&peripheral.nvm[0x55]==0x39);
    assert(peripheral.outputs==0xfaff); // owned outputs low; other outputs retained
    assert(owner.load_.amp_initialized&&owner.load_.amp_enabled==0&&owner.load_.led_enabled==0);
   }
  }
 }

 {config={};HatchPlusPower p;p.allow_control_=false;p.setup();assert(!p.failed&&p.bus_&&config.boot_calls==0&&config.reads==0);}
 {config={};HatchPlusPower p;p.light_=nullptr;p.setup();assert(p.failed&&p.bus_&&config.boot_calls==0);}
 {config={};config.adc_ready=false;HatchPlusPower p;p.setup();assert(p.owner_fault_&&!p.failed&&config.boot_calls==1&&p.runtime_&&config.events==std::vector<unsigned>({0x500}));}
 {config={};config.raw=-1;HatchPlusPower p;p.setup();assert(p.owner_fault_&&!p.failed&&config.boot_calls==1&&p.runtime_&&config.events==std::vector<unsigned>({0x500})&&config.calibration_input==0xffffffffU);}
 {config={};config.found=2;HatchPlusPower p;p.setup();assert(p.owner_fault_&&!p.failed&&config.boot_calls==1&&p.runtime_&&!p.runtime_->available&&config.events==std::vector<unsigned>({0x500}));}
 {config={};config.boot_error=-7;HatchPlusPower p;p.setup();assert(p.owner_fault_&&!p.failed&&config.boot_calls==1&&p.runtime_&&config.events==std::vector<unsigned>({0x500}));}
 {config={};config.post_ready=false;HatchPlusPower p;p.setup();assert(p.owner_fault_&&!p.failed&&config.runtime_calls==1&&config.events==std::vector<unsigned>({0x500})&&p.load_.amp_initialized);}
 {config={};config.init_failure=true;HatchPlusPower p;p.setup();assert(p.owner_fault_&&!p.failed&&config.runtime_calls==1&&config.events==std::vector<unsigned>({0x500})&&p.load_.amp_initialized);}
 for(int pin: {0,2}){config={};config.read_failure=pin;HatchPlusPower p;p.setup();assert(p.owner_fault_&&!p.failed&&p.load_.amp_initialized&&config.events==std::vector<unsigned>({0x500}));}
 {config={};HatchPlusPower p;p.setup();assert(!p.failed&&p.load_.amp_initialized&&p.load_.amp_enabled==0&&!p.load_.led_enabled&&p.led_second_tick_==42&&config.runtime_initial_tick==42&&config.events==std::vector<unsigned>({0x500}));}
}
