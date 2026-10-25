#pragma once
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>
namespace esphome::hatch_expander {
class HatchExpander { public:
 bool at62{false}, fail{false}; uint8_t id=0x13;
 std::vector<std::tuple<int,int,std::vector<uint8_t>>> writes;
 std::vector<uint8_t> raw{0,0,0,0,0,0xc0};
 bool write_device_register(uint8_t a,uint8_t r,uint8_t v) { return write_device_registers(a,r,&v,1); }
 bool write_device_registers(uint8_t a,uint8_t r,const uint8_t *v,size_t n) { writes.emplace_back(a,r,std::vector<uint8_t>(v,v+n)); return !fail; }
 std::vector<std::tuple<int,int,int>> calls;
 bool read_device_registers(uint8_t a,uint8_t r,uint8_t *v,size_t n) { calls.emplace_back(a,r,n); if(fail) return false; if(r==2) { for(size_t i=0;i<n;i++) v[i]=raw[i]; } else *v=id; return true; }
 bool probe_device(uint8_t a) { calls.emplace_back(a,-1,0); return a==0x62 && at62; }
}; }
