#pragma once
#include <cstdint>
namespace esphome::hatch_plus_power {
struct StatusState {
 uint8_t accepted=0, count=0, candidate=0;
 uint32_t published=0;
};
// Stock 401d61c4..61f6 and 66b8..6737. io.enabled() returns0/1/-1.
// io.lock() returns true only for stock helper result1; io.unlock() notifies.
template<class IO> void status_update(StatusState &s,uint8_t presentation,int enabled,IO &io) {
 if (enabled != 0 && enabled != 1) return;
 uint8_t value=enabled ? presentation|1 : presentation&uint8_t(~1);
 if (s.candidate != value) {s.count=0;s.candidate=value;return;}
 if (s.accepted == value) {if(s.count)s.count=0;return;}
 uint8_t count=s.count+1;
 if(count<3){s.count=count;return;}
 s.count=0;s.accepted=value;
 if(io.lock()){s.published=s.accepted;io.unlock();}
}
// Use status_update when the caller already read the charger-enable helper.
template<class IO> void status_step(StatusState &s,uint8_t presentation,IO &io) {
 status_update(s,presentation,io.enabled(),io);
}
}
