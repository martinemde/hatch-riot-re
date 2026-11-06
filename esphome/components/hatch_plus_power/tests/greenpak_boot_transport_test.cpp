#include "greenpak_boot_transport.h"
#include <cassert>
#include <vector>
using namespace esphome::hatch_plus_power;
struct Event{int op,addr,reg;std::vector<uint8_t> data;};
struct Bus {
 std::vector<Event> events;bool programmed=true;bool fail=false;bool mismatch=false;
 bool probe(uint8_t a){events.push_back({0,a,0,{}});return !fail && (programmed ? a==0x10 : a==8);}
 bool read(uint8_t a,uint8_t r,uint8_t*d,unsigned n){events.push_back({1,a,r,{}});if(fail)return false;for(unsigned j=0;j<n;j++)d[j]=n==240 ? uint8_t(GREENPAK_BASE[j] ^ (mismatch && j==0 ? 1 : 0)) : r==0xc8 ? 0x40 : 0;return true;}
 bool write(uint8_t a,uint8_t r,const uint8_t*d,unsigned n){events.push_back({2,a,r,std::vector<uint8_t>(d,d+n)});return !fail;}
};
struct Clock{std::vector<unsigned> ticks;void delay(unsigned n){ticks.push_back(n);}};
int main(){
 Bus b;Clock c;unsigned found=2;GreenPakBootTransport<Bus,Clock> t(b,c,found,true,true);
 assert(t.find()==0 && found==0 && t.address7()==0x10);assert(t.compare_nvm()==0);assert(b.events.back().addr==0x12);
 assert(t.program(true)==0);unsigned erase=0,pages=0;
 for(auto&e:b.events)if(e.op==2){if(e.reg==0xe3){assert(e.addr==0x10 && e.data[0]==(0x80|erase));erase++;}else{assert(e.addr==0x12 && e.data.size()==16);pages++;}}
 assert(erase==15&&pages==15&&c.ticks.size()==30);for(auto n:c.ticks)assert(n==20);
 b.events.clear();c.ticks.clear();b.programmed=false;
 assert(t.reset()==0 && found==1 && t.address7()==8);assert(b.events[0].op==1&&b.events[0].addr==0x10&&b.events[0].reg==0xc8);
 assert(b.events[1].data==std::vector<uint8_t>{0x42});assert(c.ticks==std::vector<unsigned>{3});assert(b.events[2].addr==0x10&&b.events[3].addr==8);
 b.events.clear();assert(t.program(false)==0&&found==0);assert(b.events[0].addr==8);
 b.events.clear();GreenPakBootTransport<Bus,Clock> blocked(b,c,found,false);assert(blocked.reset()==-100);assert(blocked.program(true)==-100);blocked.find();assert(b.events.empty());
 b.events.clear();c.ticks.clear();b.mismatch=true;
 GreenPakBootTransport<Bus,Clock> unapproved(b,c,found,true);
 greenpak_boot(true,3000,unapproved);
 assert(unapproved.last_error()==-101);
 for(auto &e:b.events)assert(e.op!=2);
 assert(c.ticks.empty());
 b.events.clear();assert(unapproved.program(true)==-101 && b.events.empty());
 b.mismatch=false;
 b.fail=true;assert(t.find()==2&&found==2);assert(t.reset()==-2);assert(t.last_error()==-2);
 found=0;GreenPakBootTransport<Bus,Clock> failed(b,c,found,true);assert(failed.compare_nvm()==-7&&failed.last_error()==-7);assert(failed.reset()==-7&&failed.last_error()==-7);
}
