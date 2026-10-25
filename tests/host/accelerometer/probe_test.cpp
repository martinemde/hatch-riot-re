#include "hatch_accelerometer.h"
#include <cassert>
using namespace esphome;
int main() {
 hatch_accelerometer::HatchAccelerometer probe;
 probe.update();
 hatch_expander::HatchExpander bus;
 probe.set_expander(&bus); probe.update(); assert(bus.writes.empty());
 using Row=std::tuple<int,int,int>;
 assert(bus.calls.size()==3);
 assert(bus.calls[0]==Row(0x27,1,1)); assert(bus.calls[1]==Row(0x26,-1,0)); assert(bus.calls[2]==Row(0x62,-1,0));
 bus.calls.clear(); bus.at62=true; probe.update();
 assert(bus.calls.size()==4); assert(bus.calls[3]==Row(0x62,0x11,1));
 // Initialization disabled by default; explicit opt-in still checks ID before writes.
 hatch_accelerometer::HatchAccelerometer sensor;
 sensor::Sensor x,y,z,angle; binary_sensor::BinarySensor upright,tilted;
 sensor.set_expander(&bus); sensor.set_allow_initialization(true);
 sensor.set_acceleration_x(&x); sensor.set_acceleration_y(&y); sensor.set_acceleration_z(&z);
 sensor.set_tilt_angle(&angle); sensor.set_upright(&upright); sensor.set_not_upright(&tilted);
 bus.id=0x12; sensor.update(); assert(bus.writes.empty()); assert(!sensor.valid());
 bus.id=0x13; sensor.update(); assert(bus.writes.size()==2); assert(!sensor.valid());
 using Write=std::tuple<int,int,std::vector<uint8_t>>;
 assert(bus.writes[0]==Write(0x27,0,{0x24}));
 assert(bus.writes[1]==Write(0x27,0xf,{0,3,6}));
 sensor.update(); assert(sensor.valid()); assert(z.state==-1.0f); assert(angle.state==0); assert(upright.state && !tilted.state);
 bus.raw={0,0x40,0,0,0,0}; sensor.update(); assert(angle.state==90); assert(!upright.state && tilted.state);
 bus.raw={0,0,0,0,0,0x40}; sensor.update(); assert(std::fabs(angle.state-180)<0.001f);
 bus.raw={0,0,0,0,0,0}; sensor.update(); assert(std::isnan(angle.state));
 bus.fail=true; sensor.update(); assert(!sensor.valid()); assert(std::isnan(z.state)); assert(!upright.valid && !tilted.valid);
 bus.fail=false;
 // Exact stock threshold and division rounding, including negative partial counts.
 uint8_t boundary[]={0,0,0,0,4,0xe0}; // -8188 /4 = -2047
 auto sample=hatch_accelerometer::decode_da213(boundary); assert(!sample.upright());
 boundary[4]=0; sample=hatch_accelerometer::decode_da213(boundary); assert(sample.upright());
 boundary[4]=0xff; boundary[5]=0xff; sample=hatch_accelerometer::decode_da213(boundary); assert(sample.counts[2]==0);
 bus.calls.clear(); probe.on_shutdown(); probe.update(); assert(bus.calls.empty());
}
