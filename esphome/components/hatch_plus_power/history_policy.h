#pragma once

namespace esphome::hatch_plus_power {
// Keep the recovered instruction behavior available to original-byte comparisons.
// ROLLING_SUM is an intentional production correction; thresholds are unchanged.
enum class HistoryPolicy { STOCK_NEXT_SLOT, ROLLING_SUM };
}  // namespace esphome::hatch_plus_power
