#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <vector>
namespace esphome::hatch_plus_power {
struct SchedulerTimer { uint32_t origin=0, current=0; };
// Stock policies1/2 only, the two policies used by subtask_scheduler.
inline uint32_t scheduler_timer(unsigned policy,uint32_t minimum,uint32_t period,
                                SchedulerTimer &t,uint32_t now) {
  t.current=now;
  if (!t.origin) t.origin=now;
  uint32_t result=minimum;
  if (period && t.origin!=now) {
    if (now<t.origin) result=std::max(minimum,t.origin-now+period);
    else if (now-t.origin<period) result=std::max(minimum,period-(now-t.origin));
  }
  if (policy==1) t.origin=now+result;
  else t.origin+=period;
  return std::min(uint32_t(2000),std::max(uint32_t(1),result));
}
struct SchedulerPlan {
  uint32_t lcm=0,spacing=0;
  std::vector<uint32_t> slots;
};
inline SchedulerPlan scheduler_plan() {
  const std::array<uint32_t,6> periods{30,30,900,900,900,300};
  SchedulerPlan p;
  p.lcm=30;
  for (unsigned i=1;i<6;++i) p.lcm=std::lcm(p.lcm,periods[i]);
  const uint32_t n=3*(p.lcm/900)+p.lcm/300+2*(p.lcm/30);
  p.spacing=p.lcm/n;
  p.slots.assign(n,UINT32_MAX);
  std::array<unsigned,6> order{0,1,2,3,4,5};
  // Bubble sort preserves equal-period input order, matching stock.
  for(unsigned end=5;end>0;--end)
    for(unsigned i=0;i<end;++i)
      if(periods[order[i]]>periods[order[i+1]]) std::swap(order[i],order[i+1]);
  for(auto task:order) {
    const uint32_t visits=p.lcm/periods[task];
    const float distance=float(n)/float(visits);
    for(uint32_t visit=0;visit<visits;++visit) {
      uint32_t slot=uint32_t(0.5f+distance*float(visit));
      for(uint32_t tries=0;tries<n;++tries,++slot)
        if(p.slots[slot%n]==UINT32_MAX) { p.slots[slot%n]=task;break; }
    }
  }
  const uint32_t unused=p.lcm-p.spacing*n;
  if(unused>p.spacing) p.lcm=p.spacing*n+p.spacing;
  return p;
}
template<class IO> void scheduler_initialize(IO &io) {
  // Non-null callbacks from the fixed six-entry stock table.
  for(unsigned task: {1u,2u,3u}) { io.delay(1); io.initialize(task); }
  io.create_scheduler(10);
}
struct SchedulerCursor {
  SchedulerPlan plan;
  SchedulerTimer housekeeping,slot_timer;
  int marker=-1;
  explicit SchedulerCursor(uint32_t now):plan(scheduler_plan()) {
    housekeeping.origin=slot_timer.origin=now;
  }
  bool waiting=false;
  uint32_t wait_start=0,wait_ticks=0;
  template<class IO> void run_slots(IO &io) {
    for(unsigned slot=0;slot<plan.slots.size();++slot) {
      const uint32_t delta=scheduler_timer(2,plan.spacing/3,plan.spacing,slot_timer,io.tick());
      if(delta>=plan.spacing/2) marker=-1;
      else if(marker==-1) marker=int(slot);
      else if(marker==int(slot)) slot_timer.origin=io.tick();
      io.service(plan.slots[slot],delta);
    }
  }
  // One pending RTOS-style elapsed delay; call from the owning application loop.
  // Returns true after one complete serial slot pass, never sleeps.
  template<class IO> bool poll(IO &io) {
    if(!waiting) {
      wait_start=io.tick();
      wait_ticks=scheduler_timer(1,0,plan.lcm,housekeeping,wait_start);
      waiting=true;
      return false;
    }
    if(uint32_t(io.tick()-wait_start)<wait_ticks) return false;
    waiting=false;
    slot_timer.origin+=wait_ticks;
    run_slots(io);
    return true;
  }
  template<class IO> void cycle(IO &io) {
    const uint32_t delay=scheduler_timer(1,0,plan.lcm,housekeeping,io.tick());
    io.delay(delay);
    slot_timer.origin+=delay;
    run_slots(io);
  }
};
} // namespace esphome::hatch_plus_power
