#include "esphome/components/hatch_plus_power/pwm_output_state.h"
#include <cassert>
struct Hardware {
 bool enabled=false,idle=false;unsigned raw=99,mode=99,index=99,reads=0;
 bool signal_enabled(unsigned m,unsigned i){mode=m;index=i;return enabled;}
 bool idle_high(unsigned,unsigned){return idle;}
 unsigned duty(unsigned,unsigned){reads++;return raw;}
};
int main(){Hardware h;
 // Disabled signal at idle low is physically off despite stale duty.
 assert(!esphome::hatch_plus_power::applied_pwm_busy(3,h)&&h.reads==0&&h.mode==0&&h.index==3);
 h.idle=true;assert(esphome::hatch_plus_power::applied_pwm_busy(8,h)&&h.mode==1&&h.index==0);
 h.enabled=true;h.raw=0;assert(!esphome::hatch_plus_power::applied_pwm_busy(15,h)&&h.mode==1&&h.index==7);
 h.raw=1;assert(esphome::hatch_plus_power::applied_pwm_busy(0,h));
}
