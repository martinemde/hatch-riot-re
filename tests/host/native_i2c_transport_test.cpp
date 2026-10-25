#include <cassert>
#include <cstdint>
#include <vector>
#include "esphome/components/hatch_plus_power/native_i2c_transport.h"
using namespace esphome::hatch_plus_power;
struct Owner {
 unsigned calls=0;int result=0;uint8_t address=0;size_t reads=0;std::vector<uint8_t> payload;
 int write_readv(uint8_t a,const uint8_t *w,size_t wn,uint8_t *r,size_t rn){
  ++calls;address=a;reads=rn;payload.clear();if(wn)payload.assign(w,w+wn);
  if(!result)for(size_t i=0;i<rn;i++)r[i]=uint8_t(0x74+i);return result;
 }
};
int main(){
 Owner owner;NativeI2CBus<Owner> bus(owner);uint8_t data[240]{};
 assert(bus.probe(0x10)&&owner.address==0x10&&owner.payload.empty()&&owner.reads==0);
 assert(bus.read(0x10,0x74,data,3)&&owner.payload==std::vector<uint8_t>{0x74}&&owner.reads==3&&data[2]==0x76);
 assert(bus.read(0x60,0x38,data,2)&&owner.address==0x60&&owner.payload==std::vector<uint8_t>{0x38}&&owner.reads==2);
 for(unsigned i=0;i<240;i++)data[i]=uint8_t(i);
 assert(bus.write(0x12,0,data,240)&&owner.address==0x12&&owner.payload.size()==241&&owner.payload[240]==239&&owner.reads==0);
 unsigned calls=owner.calls;assert(!bus.write(0x10,0,data,241)&&owner.calls==calls);
 owner.result=3;assert(!bus.probe(0x60)&&!bus.read(0x10,0x74,data,3)&&!bus.write(0x10,0x7a,data,1));
}
