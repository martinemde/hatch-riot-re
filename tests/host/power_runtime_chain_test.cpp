// The Python runner inserts actual owner method definitions at OWNER_METHODS.
#include <cassert>
#include <cmath>
#include "esphome/components/hatch_plus_power/power_status.h"
#include "esphome/components/hatch_plus_power/greenpak_boot_controller.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
using ledc_channel_t=unsigned;
uint32_t mock_duty[2][8]{};
using ledc_mode_t=unsigned;
struct LedcRegisters {struct Group{struct Channel{struct Conf{unsigned sig_out_en=1,idle_lv=0;}conf0;}channel[8];}channel_group[2];} LEDC;
bool pwm_output_busy(uint8_t channel){unsigned mode=channel<8?0:1,index=channel%8;
 auto c=LEDC.channel_group[mode].channel[index].conf0;
 return c.sig_out_en ? mock_duty[mode][index]!=0 : c.idle_lv!=0;
}
#include "esphome/components/hatch_plus_power/load_controller.h"
#include "esphome/components/hatch_plus_power/pwm_output_state.h"
using namespace esphome::hatch_plus_power;
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
namespace media_player {enum{MEDIA_PLAYER_COMMAND_STOP=1,MEDIA_PLAYER_STATE_PLAYING=2,MEDIA_PLAYER_STATE_ANNOUNCING=3};}
struct Light {bool on=true,transformer=false;int stops=0;struct Values{bool on=true;bool is_on(){return on;}}current_values;struct Call{Light *s;void perform(){s->stops++;s->current_values.on=false;}};Call turn_off(){return {this};}bool is_transformer_active(){return transformer;}};
struct Player {int state=0,stops=0;struct Call{Player *s;Call &set_command(int){return *this;}void perform(){s->stops++;s->state=0;}};Call make_call(){return {this};}};
struct Display {int clears=0;void clear(){clears++;}};
struct Speaker {bool running=false;unsigned finishes=0;bool is_running(){return running;}void finish(){finishes++;}};
struct Sensor {float state=0;unsigned publications=0;bool valid=false;
 void publish_state(float v){state=v;valid=true;++publications;}void invalidate_state(){valid=false;}};
struct Expander {bool ok=true;unsigned reads=0;uint8_t address=0,values[3]{0x60,0x12,0x04};
 bool read(uint8_t a,uint8_t reg,uint8_t *data,size_t n){
  ++reads;address=a;assert(reg==0x74&&n==3);if(!ok)return false;
  for(size_t i=0;i<n;i++)data[i]=values[i];return true;
 }};
struct Controller {struct State {struct Shutdown{unsigned request=1;}shutdown;struct Battery{int reported=50;}battery;struct Status{int published=0;}status;struct Source{int external_charger=0;}source;}s;const State &task_state(){return s;}};
#include "esphome/components/hatch_plus_power/runtime_adapter.h"
struct Bus {
 unsigned reads=0,writes=0,off_attempts=0;uint8_t source=0;bool fail_off=false,fail_reads=false;
 bool read(uint8_t,uint8_t reg,uint8_t *data,size_t n){
  ++reads;if(fail_reads)return false;for(size_t i=0;i<n;++i)data[i]=reg+i==0x74?source:0;return true;
 }
 bool write(uint8_t,uint8_t reg,const uint8_t *data,size_t){
  ++writes;
  if(reg==0x7a && (data[0]&0xc0)){++off_attempts;if(fail_off)return false;}
  return true;
 }
 bool probe(uint8_t){return false;}
};
struct Adc {
 uint32_t calibrated=3000;
 void adc_width(unsigned){} void characterize(unsigned,unsigned,unsigned,unsigned){}
 void adc_attenuation(unsigned,unsigned){} uint32_t raw(unsigned){return 1;}
 uint32_t calibrate(uint32_t){return calibrated;}
};
class HatchPlusPower;
using Runtime=PowerRuntime<Bus,Adc,HatchPlusPower>;
class HatchPlusPower {
public:
 std::vector<uint8_t> pwm_channels_{0,1,2,3};
 bool desired_amp_=false,desired_led_=false,desired_initialized_=false,speaker_ready_=false;uint32_t speaker_ready_tick_=0;Speaker speaker_object;Speaker *speaker_=&speaker_object;
 LoadState load_;Light *light_;Player *player_;Display *display_;std::unique_ptr<Runtime>runtime_;
 bool owner_fault_=false;
 bool preparing_=false,allow_control_=true,stopped_=false,amp_timer_active_=false,failed=false;
 uint32_t now=0,amp_timer_tick_=0,led_second_tick_=0,quiet_ticks_=0,quiet_start_=0;unsigned shutdown_reason_=0;uint16_t outputs=0x500;bool fail_write=false;
 Sensor *battery_report_age_=nullptr,*battery_report_accepted_=nullptr,*owner_fault_sensor_=nullptr;
 Sensor *battery_percent_=nullptr,*power_status_=nullptr,*external_power_=nullptr,*charge_enable_=nullptr;
 Sensor *wireless_voltage_=nullptr,*status_74_=nullptr,*status_75_=nullptr,*status_76_=nullptr;
 Expander expander_object;Expander *expander_=&expander_object;Expander *bus_=&expander_object;unsigned found_=0;
 bool warning=false;void status_set_warning(){warning=true;}void status_clear_warning(){warning=false;}
 void publish_hardware_status_();
 uint32_t tick(){return now;}bool is_failed(){return failed;}
 bool device(){return true;}bool amp_lock(){return true;}void amp_unlock(){}int led_lock(){return 1;}void led_unlock(){}
 int write(unsigned pin,unsigned value){if(fail_write)return 8;unsigned mask=pin==2?0x400:0x100;outputs=value==2?outputs|mask:outputs&~mask;return 0;}
 int read(unsigned pin,uint8_t &value){value=bool(outputs&(pin==2?0x400:0x100));return 0;}
 void update(unsigned,bool){}void timer_stop(){amp_timer_active_=false;}bool timer_active(){return amp_timer_active_;}
 bool timer_start(uint32_t n){amp_timer_tick_=n;return amp_timer_active_=true;}void periodic(){}
 bool amplifier(){return amplifier_enabled(load_);}bool led_wait(){return leds_enabled(load_);}
 unsigned visits=0,nonpower=0;int variant(){++visits;return 2;}
 int battery_report_lock(){return 1;}void battery_report_unlock(){}bool disable_critical(){return false;}
 int lock(){return 1;}void unlock(){}void configure_virtual(unsigned,unsigned,unsigned,unsigned){}
 void source_changed(unsigned){}int reset_greenpak(){return 0;}
 void display(bool){}void drain_logs(){}void external_critical(){}
 void service(uint32_t index,uint32_t){assert(index!=3);++nonpower;}
 void delay(unsigned ticks);
 void prepare();void state_shutdown(unsigned);bool led_busy();void loop();
};
// OWNER_METHODS
int main(){
 // Production runtime explicitly uses rolling sums, independent of the stock
 // oracle default. Compare retained accumulator to the actual 64 entries.
 {
  Light l;Player p;Display d;HatchPlusPower o;Bus b;Adc a;
  o.light_=&l;o.player_=&p;o.display_=&d;
  o.runtime_=std::make_unique<Runtime>(b,0x10,a,o,0);
  for(unsigned i=0;i<130;++i) {
   a.calibrated=1650+(i*7)%300;o.now+=1000;o.runtime_->step();
   const auto &battery=o.runtime_->controller().task_state().battery;
   uint32_t independent=0;for(auto value:battery.samples)independent+=value;
   assert(battery.total==independent);
  }
 }
 // Real report acceptance distinguishes successful stock report from a later
 // comparator error and sentinel. Age remains time since last accepted report.
 {
  Light l;Player p;Display d;HatchPlusPower o;Bus b;Adc a;
  o.light_=&l;o.player_=&p;o.display_=&d;
  o.runtime_=std::make_unique<Runtime>(b,0x10,a,o,0);
  assert(!o.runtime_->has_report());
  o.now=100;o.runtime_->step();
  assert(o.runtime_->last_result().report_accepted&&o.runtime_->has_report());
  assert(o.runtime_->report_age_ms()==0);
  b.source=0x40;b.fail_reads=true;o.now=150;
  // Establish external source first, then fault its retained comparator path.
  o.runtime_->controller().task_state().source.flags=1;
  o.runtime_->step();
  assert(!o.runtime_->last_result().report_accepted&&o.runtime_->report_age_ms()==50);
  b.fail_reads=false;o.now=200;o.runtime_->step();
  assert(o.runtime_->last_result().sampled&&!o.runtime_->last_result().report_accepted);
  assert(o.runtime_->report_age_ms()==100);
 }
 Light light;Player player;Display display;HatchPlusPower owner;Bus bus;Adc adc;
 owner.light_=&light;owner.player_=&player;owner.display_=&display;
 owner.now=1234;owner.runtime_=std::make_unique<Runtime>(bus,0x10,adc,owner,owner.tick());
 owner.load_.amp_initialized=1;
 owner.runtime_->initialize();owner.visits=0;
 owner.loop();assert(owner.visits==0&&bus.reads==0);
 owner.loop();assert(owner.visits==0&&bus.reads==0);
 owner.now++;owner.loop();assert(owner.visits>0&&bus.reads>0&&owner.nonpower==65);
 auto visits=owner.visits;auto reads=bus.reads;
 owner.now=0xfffffff0U;owner.delay(10000);
 assert(owner.quiet_start_==0xfffffff0U&&owner.quiet_ticks_==10000);
 owner.now=owner.quiet_start_+9999U;owner.loop();assert(owner.visits==visits&&bus.reads==reads);
 owner.now=owner.quiet_start_+10000U;owner.loop();assert(owner.quiet_ticks_==0);
 // First post-quiet poll arms a delay. Later ticks dispatch real controller again.
 owner.now+=2000;owner.loop();assert(owner.visits>visits&&bus.reads>reads);
 // Telemetry failure happens after the real stock runtime/protection dispatch.
 Sensor external,wireless,charge;owner.external_power_=&external;owner.wireless_voltage_=&wireless;owner.charge_enable_=&charge;
 owner.expander_->ok=true;owner.now+=2000;owner.loop();owner.now+=2000;owner.loop();
 assert(external.valid&&external.state==1&&wireless.state==1&&charge.state==1);
 visits=owner.visits;reads=bus.reads;owner.expander_->ok=false;owner.now+=2000;owner.loop();owner.now+=2000;owner.loop();
 assert(owner.visits>visits&&bus.reads>reads&&!external.valid&&!wireless.valid&&!charge.valid);
 owner.stopped_=true;visits=owner.visits;owner.now+=2000;owner.loop();assert(owner.visits==visits);
 // An off transaction is not proof that the processor lost power. Exercise both
 // failed writes and accepted writes with power retained, for both off reasons.
 for(unsigned request:{1u,2u})for(bool fail_off:{false,true}) {
  Light l;Player p;Display d;HatchPlusPower o;Bus b;Adc a;
  o.light_=&l;o.player_=&p;o.display_=&d;o.load_.amp_initialized=1;
  o.now=100;o.runtime_=std::make_unique<Runtime>(b,0x10,a,o,o.tick());
  b.fail_off=fail_off;o.runtime_->controller().task_state().shutdown.request=request;
  o.loop();o.now+=2000;o.loop();
  assert(b.off_attempts==1&&o.preparing_&&o.shutdown_reason_==(request==1?4:5));
  assert(o.runtime_->controller().task_state().shutdown.request==request);
  assert(l.stops==1&&p.stops==1&&!o.amplifier()&&!o.led_wait());
  // Dock/plug arrival must cancel in the real source/shutdown cores. Successful
  // off retains its existing quiet interval before polling resumes.
  b.source=0x40;o.now+=10000;o.loop();o.now+=2000;o.loop();
  assert(o.runtime_->controller().task_state().source.flags&1);
  assert(!o.runtime_->controller().task_state().shutdown.request);
  assert(!o.preparing_&&!o.shutdown_reason_&&b.off_attempts==1);
  // New independent UI/playback requests can enable both real load-controller
  // caches and the mocked physical outputs after speaker-clock settling.
  l.current_values.on=true;p.state=media_player::MEDIA_PLAYER_STATE_PLAYING;o.speaker_->running=true;
  o.now++;o.loop();assert(o.led_wait()&&!o.amplifier());
  o.now+=30;o.loop();assert(o.amplifier()&&o.led_wait()&&(o.outputs&0x500)==0x500);
  assert(l.stops==1&&p.stops==1&&o.speaker_->finishes==1);
 }
}
