"""Compile the shared-bus methods against a recording bit-level transport."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_expander_shared_transactions(tmp_path):
    text = (ROOT / "esphome/components/hatch_expander/hatch_expander.cpp").read_text()
    public = text[text.index("bool HatchExpander::read_device_registers("):text.index("void HatchExpander::mark_txn_")]
    low = text[text.index("bool HatchExpander::bb_write_regs_("):text.index("void HatchExpander::bb_fail_(")]
    latch = text[text.index("int HatchExpander::read_output_pin_status("):text.index("int HatchExpander::write_output_pin_status(")]
    source = tmp_path / "expander.cpp"
    source.write_text(r'''#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#define ESP_LOGW(...) ((void)0)
constexpr unsigned MAX_SHARED_PAYLOAD = 240;
constexpr uint8_t REG_OUT = 0x02, STOCK_MASK[] = {0x3F, 0x0F};
constexpr uint32_t MIN_GAP_MS = 5, RETRY_GAP_MS = 10, SETTLE_MS = 20;
constexpr int READ_ATTEMPTS = 3, WRITE_ATTEMPTS = 3;
class HatchExpander {
 public:
  bool ready_{true};
  uint8_t address_{0x58};
  uint8_t out_[2]{};
  int nacks{0};
  std::vector<int> wire;
  std::vector<uint32_t> gaps;
  void settle_() {}
  void mark_txn_(uint32_t gap) { gaps.push_back(gap); }
  bool bb_start_() { wire.push_back(-1); return true; }
  bool bb_stop_() { wire.push_back(-2); return true; }
  bool bb_write_byte_(uint8_t b) {
    wire.push_back(b);
    if (nacks == 0) return true;
    --nacks;
    return false;
  }
  bool bb_read_byte_(uint8_t *b, bool ack) {
    wire.push_back(ack ? -3 : -4);
    *b = 0x04;
    return true;
  }
  void bb_fail_(const char *) { bb_stop_(); }
  bool bb_read_regs_(uint8_t reg, uint8_t *data, size_t len) {
    return bb_read_regs_(address_, reg, data, len);
  }
  int read_output_pin_status(uint8_t, bool &);
  bool read_device_registers(uint8_t, uint8_t, uint8_t *, size_t);
  bool write_device_registers(uint8_t, uint8_t, const uint8_t *, size_t);
  bool write_reg_(uint8_t, uint8_t);
  bool read_regs_(uint8_t, uint8_t, uint8_t *, size_t);
  bool write_regs_(uint8_t, uint8_t, const uint8_t *, size_t);
  bool bb_read_regs_(uint8_t, uint8_t, uint8_t *, size_t);
  bool bb_write_regs_(uint8_t, uint8_t, const uint8_t *, size_t);
};
''' + public + low + latch + r'''
int main() {
  HatchExpander bus;
  uint8_t config[]{0x00, 0x03, 0x06};
  assert(bus.write_device_registers(0x27, 0x0F, config, sizeof(config)));
  assert((bus.wire == std::vector<int>{-1, 0x4E, 0x0F, 0x00, 0x03, 0x06, -2}));
  assert((bus.gaps == std::vector<uint32_t>{20}));
  bus.wire.clear(); bus.gaps.clear(); bus.nacks = 1;
  uint8_t data[241]{};
  assert(bus.read_device_registers(0x27, 0x02, data, 2));
  assert((bus.wire == std::vector<int>{-1, 0x4E, -2, -1, 0x4E, 0x02, -1, 0x4F, -3, -4, -2}));
  assert((bus.gaps == std::vector<uint32_t>{10, 5}));
  assert(data[0] == 0x04 && data[1] == 0x04);

  bus.wire.clear(); bus.gaps.clear(); bus.nacks = 3;
  assert(!bus.write_device_registers(0x27, 0x0F, config, sizeof(config)));
  assert((bus.wire == std::vector<int>{-1, 0x4E, -2, -1, 0x4E, -2, -1, 0x4E, -2}));
  assert((bus.gaps == std::vector<uint32_t>{10, 10, 10}));

  // The existing latch reader still talks to the expander after foreign transactions.
  bus.wire.clear();
  bool latch_value = false;
  assert(bus.read_output_pin_status(10, latch_value) == 0 && latch_value);
  assert((bus.wire == std::vector<int>{-1, 0xB0, 0x02, -1, 0xB1, -3, -4, -2}));

  bus.wire.clear();
  for (uint8_t address : {0x07, 0x58, 0x78}) {
    assert(!bus.read_device_registers(address, 0, data, 1));
    assert(!bus.write_device_registers(address, 0, data, 1));
  }
  for (size_t len : {0, 241}) {
    assert(!bus.read_device_registers(0x27, 0, data, len));
    assert(!bus.write_device_registers(0x27, 0, data, len));
  }
  assert(!bus.read_device_registers(0x27, 0, nullptr, 1));
  assert(!bus.write_device_registers(0x27, 0, nullptr, 1));
  bus.ready_ = false;
  assert(!bus.read_device_registers(0x27, 0, data, 1));
  assert(!bus.write_device_registers(0x27, 0, data, 1));
  assert(bus.wire.empty());
}
''')
    compiler = shutil.which("clang++") or shutil.which("g++")
    executable = tmp_path / "expander"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
