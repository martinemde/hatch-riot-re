#pragma once
#include <cstdint>

namespace esphome { namespace hatch_plus_power {
// Offline policy core. Tick values are FreeRTOS ticks, not milliseconds.
struct ShutdownState {
  uint32_t request{0}, start_tick{0};
  uint8_t disabled_warning{0}, external_warning{0}, comparator_index{0};
};
struct ShutdownInput {
  uint8_t source_flags{0};
  bool critical{false}, external_charger{false}, disable_critical{false};
  uint32_t tick{0};
  bool amplifier{false}, led_wait{false};
};
// Adapter returns stock error convention (zero success). No live adapter here.
struct ShutdownTransport {
  virtual ~ShutdownTransport() = default;
  virtual int read(unsigned bit, uint8_t &value) = 0;
  virtual void display(bool reset) = 0;
  virtual void prepare() = 0;
  virtual void state_shutdown(unsigned reason) = 0;
  virtual void drain_logs() = 0;
  virtual int set_virtual(unsigned mask) = 0;
  virtual void delay(unsigned ticks) = 0;
  virtual void external_critical() = 0;
  virtual int reset_greenpak() = 0;
};
inline int shutdown_buttons(ShutdownState &s, const ShutdownInput &i, ShutdownTransport &t) {
  uint8_t two=0, five=0, off=0, enabled=0;
  if (t.read(0x3c1, two)==0 && t.read(0x3c3, five)==0) {
    if (i.source_flags & 2) {
      if (two && five) t.display(true);
      else if (two) {
        t.display(false);
        if (t.read(0x3c9, off)==0 && off) s.request=1;
      }
    } else if (two && five) t.display(true);
  }
  // Status-debounce publication is outside this core; its stock read still occurs.
  const int enabled_error=t.read(0x3b2, enabled);
  return enabled_error ? -1 : int(enabled!=0);
}
template<class Provider>
void shutdown_critical_dynamic(ShutdownState &s, const ShutdownInput &i, ShutdownTransport &t, Provider &provider) {
  if (i.critical) {
    if (i.external_charger) {
      if (!s.external_warning) {
        t.external_critical();
        s.external_warning=1;
        if (t.reset_greenpak()==0) s.comparator_index=255;
      }
    } else if (provider.disable_critical()) s.disabled_warning=1;
    else { t.display(false); s.request=2; }
  }
  if ((i.source_flags & 5) && s.request) s.request=0;
  if (!s.request) return;
  t.prepare();
  uint8_t request=s.request;
  if (!s.start_tick) s.start_tick=provider.tick();
  if ((provider.amplifier() || provider.led_wait()) && uint32_t(provider.tick()-s.start_tick)<=9999) return;
  t.state_shutdown(request==1 ? 4 : 5);
  t.drain_logs();
  if (t.set_virtual(request==1 ? 0x80 : 0x40)==0) t.delay(10000);
}
struct ShutdownSnapshotProvider {
  const ShutdownInput &in;
  bool disable_critical() { return in.disable_critical; }
  uint32_t tick() { return in.tick; }
  bool amplifier() { return in.amplifier; }
  bool led_wait() { return in.led_wait; }
};
inline void shutdown_critical(ShutdownState &s, const ShutdownInput &i, ShutdownTransport &t) {
  ShutdownSnapshotProvider provider{i};
  shutdown_critical_dynamic(s, i, t, provider);
}
inline void shutdown_step(ShutdownState &s, const ShutdownInput &i, ShutdownTransport &t) {
  shutdown_buttons(s,i,t);
  shutdown_critical(s,i,t);
}
}} // namespace esphome::hatch_plus_power
