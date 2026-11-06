#pragma once
#include <cstdint>
namespace esphome::hatch_plus_power {
struct LoadState {
 uint8_t amp_initialized=0, amp_enabled=0, led_enabled=0, led_deferred=0, led_idle_seconds=0;
 uint32_t amp_volume=0;
};
inline uint8_t amplifier_enabled(const LoadState &s) {return s.amp_enabled;}
inline uint8_t leds_enabled(const LoadState &s) {return s.led_enabled;}
// IO models stock GPIOE/RTOS boundaries; zero GPIO return means success.
template<class IO> int amplifier_write(LoadState &s,uint8_t enabled,IO &io) {
 int result=0;
 for(unsigned attempt=0;attempt<3;attempt++) {
  if(!io.device()){result=0x105;continue;}
  result=io.write(2,enabled+1);
  if(result)continue;
  uint8_t level=0;
  result=io.read(2,level);
  if(result)continue;
  if(bool(level)!=bool(enabled)){result=0x108;continue;}
  io.amp_unlock();s.amp_enabled=enabled;
  if(s.amp_enabled)io.update(1,true);
  return result;
 }
 io.amp_unlock();return result;
}
template<class IO> int amplifier_set(LoadState &s,uint8_t enabled,IO &io) {
 if(!io.amp_lock())return -1;
 return amplifier_write(s,enabled,io);
}
template<class IO> int amplifier_request(LoadState &s,uint8_t enabled,IO &io) {
 if(!s.amp_initialized)return 0x103;
 if(enabled) {
  io.timer_stop();
  if(!s.amp_enabled)return amplifier_set(s,1,io);
 } else if(s.amp_enabled && !io.timer_active()) {
  uint32_t now=io.tick();
  if(!io.timer_start(now))return amplifier_set(s,0,io);
 }
 return 0;
}
template<class IO> void amplifier_timer(LoadState &s,IO &io) {
 if(io.amp_lock())amplifier_write(s,0,io);
}
template<class IO> int amplifier_volume(LoadState &s,uint16_t volume,IO &io) {
 if(!s.amp_initialized)return 0x103;
 if(s.amp_volume==volume)return 0;
 int result=amplifier_request(s,volume!=0,io);
 if(!result)s.amp_volume=volume;
 return result;
}
template<class IO> void led_request(LoadState &s,uint8_t enabled,IO &io) {
 if(!enabled && s.led_deferred)return;
 if(enabled)s.led_deferred=0;
 if(s.led_enabled==enabled)return;
 if(!enabled && io.led_busy()){s.led_deferred=1;return;}
 if(!io.device()){io.update(2,false);return;}
 if(io.led_lock()!=1){io.update(2,false);return;}
 int result=io.write(0,enabled+1);
 if(!result)s.led_enabled=enabled;
 else io.update(2,false);
 // Stock emits a successful update even on GPIO write failure.
 io.update(2,true);io.led_unlock();
}
template<class IO> void led_second(LoadState &s,IO &io) {
 io.periodic();
 if(!s.led_deferred){s.led_idle_seconds=0;return;}
 if(io.led_busy())return;
 s.led_idle_seconds++;
 if(s.led_idle_seconds!=10)return;
 s.led_deferred=0;led_request(s,0,io);s.led_idle_seconds=0;
}
}
