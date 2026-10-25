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
struct Display {int clears=0;std::string overlay;unsigned duration=0;void clear(){clears++;}
 void show_text_overlay(const char *text,unsigned ms){overlay=text;duration=ms;}};
struct Speaker {bool running=false;unsigned finishes=0;bool is_running(){return running;}void finish(){finishes++;}};
struct Sensor {float state=0;unsigned publications=0;bool valid=false;
 void publish_state(float v){state=v;valid=true;++publications;}void invalidate_state(){valid=false;}};
struct Expander {bool ok=true;unsigned reads=0;uint8_t address=0,values[3]{0x60,0x12,0x04};
 bool read(uint8_t a,uint8_t reg,uint8_t *data,size_t n){
  ++reads;address=a;assert(reg==0x74&&n==3);if(!ok)return false;
  for(size_t i=0;i<n;i++)data[i]=values[i];return true;
 }};
struct Controller {struct State {struct Shutdown{unsigned request=1;}shutdown;struct Battery{int reported=50;}battery;struct Status{int published=0;}status;struct Source{int external_charger=0;}source;}s;const State &task_state(){return s;}};
struct Runtime {struct Result{bool report_accepted=true;}last;bool has_report(){return true;}uint32_t report_age_ms(){return 1234;}const Result &last_result(){return last;}Controller c;bool result=false;bool poll(){return result;}Controller &controller(){return c;}int read(uint16_t){return 0;}};
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
 void publish_hardware_status_();void update();void source_changed(unsigned);
 uint32_t tick(){return now;}bool is_failed(){return failed;}
 bool device(){return true;}bool amp_lock(){return true;}void amp_unlock(){}int led_lock(){return 1;}void led_unlock(){}
 int write(unsigned pin,unsigned value){if(fail_write)return 8;unsigned mask=pin==2?0x400:0x100;outputs=value==2?outputs|mask:outputs&~mask;return 0;}
 int read(unsigned pin,uint8_t &value){value=bool(outputs&(pin==2?0x400:0x100));return 0;}
 void update(unsigned,bool){}void timer_stop(){amp_timer_active_=false;}bool timer_active(){return amp_timer_active_;}
 bool timer_start(uint32_t n){amp_timer_tick_=n;return amp_timer_active_=true;}void periodic(){}
 bool amplifier(){return amplifier_enabled(load_);}bool led_wait(){return leds_enabled(load_);}
 void prepare();void state_shutdown(unsigned);bool led_busy();void loop();
};
// OWNER_METHODS
int main(){
 Light light;Player player;Display display;HatchPlusPower owner;
 owner.light_=&light;owner.player_=&player;owner.display_=&display;owner.runtime_=std::make_unique<Runtime>();
 owner.source_changed(4);assert(display.overlay=="base"&&display.duration==3000);
 owner.source_changed(1);assert(display.overlay=="plug"&&display.duration==3000);
 owner.source_changed(2);assert(display.overlay=="batt"&&display.duration==3000);
 owner.load_={1,1,1,0,0,1};light.transformer=true;mock_duty[0][0]=23;
 owner.prepare();assert(owner.preparing_&&light.stops==1&&player.stops==1);
 assert(owner.amplifier()&&owner.led_wait()&&owner.amp_timer_active_&&owner.load_.led_deferred);
 owner.prepare();assert(light.stops==1&&player.stops==1);
 owner.now=2999;owner.loop();assert(owner.amplifier());
 owner.fail_write=true;owner.now=3000;owner.loop();assert(owner.amplifier()&&!owner.amp_timer_active_);
 // Real prepare retries the failed physical amp off request, not a UI snapshot.
 owner.prepare();assert(owner.amp_timer_active_);owner.fail_write=false;
 owner.now=5999;owner.loop();assert(owner.amplifier());owner.now=6000;owner.loop();assert(!owner.amplifier());
 // Busy preserves the deferred LED physical cache and pauses its idle count.
 assert(owner.led_wait()&&owner.load_.led_idle_seconds==0);
 light.transformer=false;mock_duty[0][0]=0;for(int i=0;i<9;i++){owner.now+=1000;owner.loop();assert(owner.led_wait());}
 owner.now+=1000;owner.loop();assert(!owner.led_wait());
 owner.state_shutdown(5);assert(owner.shutdown_reason_==5&&display.clears==1);
 // The asynchronous quiet interval suppresses load timers too, including wrap.
 owner.load_.amp_enabled=1;owner.amp_timer_active_=true;owner.amp_timer_tick_=0;
 owner.quiet_start_=0xfffffff0U;owner.quiet_ticks_=10000;
 owner.now=owner.quiet_start_+9999U;owner.loop();assert(owner.amplifier()&&owner.quiet_ticks_==10000);
 owner.now=owner.quiet_start_+10000U;owner.loop();assert(!owner.amplifier()&&owner.quiet_ticks_==0);
 // Repeated idle loops must not create extra GPIO off requests or timer restarts.
 owner.preparing_=false;owner.shutdown_reason_=0;owner.runtime_->c.s.shutdown.request=0;
 owner.desired_initialized_=false;owner.load_.amp_enabled=1;owner.amp_timer_active_=false;
 player.state=0;light.current_values.on=false;owner.now+=1;owner.loop();
 uint32_t started=owner.amp_timer_tick_;assert(owner.amp_timer_active_);
 for(int i=0;i<5;i++){owner.now+=100;owner.loop();assert(owner.amp_timer_tick_==started);}
 // Playback waits for actual speaker clocks, then a 30 ms settling interval.
 player.state=media_player::MEDIA_PLAYER_STATE_PLAYING;owner.speaker_->running=true;
 owner.load_.amp_enabled=0;owner.desired_amp_=false;owner.amp_timer_active_=false;
 owner.now+=1;owner.loop();assert(!owner.amplifier());uint32_t ready=owner.now;
 owner.now=ready+29;owner.loop();assert(!owner.amplifier());
 owner.now=ready+30;owner.loop();assert(owner.amplifier());
 // A failed physical enable is retried without a new light UI edge.
 owner.load_.led_enabled=0;owner.desired_led_=false;light.current_values.on=true;
 owner.fail_write=true;owner.now++;owner.loop();assert(!owner.led_wait());
 owner.fail_write=false;owner.now++;owner.loop();assert(owner.led_wait());
 // Canceled external shutdown clears the prepare latch on a completed poll.
 owner.preparing_=true;owner.runtime_->result=true;owner.loop();assert(!owner.preparing_);
 // Control publishes every exposed state after its completed protection poll.
 Sensor battery,status,external,wireless,charge,r74,r75,r76,age,accepted,fault;
 owner.battery_report_age_=&age;owner.battery_report_accepted_=&accepted;owner.owner_fault_sensor_=&fault;
 owner.battery_percent_=&battery;owner.power_status_=&status;owner.external_power_=&external;
 owner.wireless_voltage_=&wireless;owner.charge_enable_=&charge;
 owner.status_74_=&r74;owner.status_75_=&r75;owner.status_76_=&r76;
 owner.loop();assert(battery.valid&&status.valid&&external.state==1&&wireless.state==1&&charge.state==1);
 assert(r74.state==0x60&&r75.state==0x12&&r76.state==4&&owner.expander_->address==0x10);
 assert(accepted.state==1&&age.state==1.234f&&fault.state==0);
 // A failed reporting attempt invalidates percentage but retains the core value.
 owner.runtime_->last.report_accepted=false;owner.loop();
 assert(std::isnan(battery.state)&&accepted.state==0&&owner.runtime_->c.s.battery.reported==50);
 owner.runtime_->last.report_accepted=true;
 // Owner faults survive successful status telemetry reads.
 owner.owner_fault_=true;owner.loop();assert(owner.warning&&fault.state==1);
 owner.owner_fault_=false;
 // A failed extra read invalidates all hardware states, including stale true.
 owner.expander_->ok=false;owner.preparing_=true;owner.loop();
 assert(!owner.preparing_&&battery.publications==4&&status.publications==4);
 assert(!external.valid&&!wireless.valid&&!charge.valid&&std::isnan(r74.state)&&std::isnan(r75.state)&&std::isnan(r76.state));
 assert(owner.warning);
 // Hardware voltage is not the stock cached aggregate external-charger flag.
 owner.expander_->ok=true;owner.expander_->values[0]=0x20;owner.expander_->values[2]=0;
 owner.runtime_->c.s.source.external_charger=1;owner.loop();
 assert(external.valid&&external.state==0&&wireless.state==1&&charge.state==0&&!owner.warning);
 owner.found_=1;owner.loop();assert(owner.expander_->address==0x08);
 auto telemetry_reads=owner.expander_->reads;owner.found_=2;owner.loop();
 assert(owner.expander_->reads==telemetry_reads&&!external.valid&&!wireless.valid&&!charge.valid);
 // Diagnostic update uses the same definitions and configured address.
 owner.allow_control_=false;owner.update();assert(owner.expander_->address==0x10&&wireless.valid);
 owner.expander_->ok=false;owner.update();assert(!external.valid&&!wireless.valid&&!charge.valid);
 owner.allow_control_=true;
 owner.stopped_=true;owner.load_.amp_enabled=1;owner.amp_timer_active_=true;owner.amp_timer_tick_=0;owner.now+=10000;owner.loop();assert(owner.amplifier());
}
