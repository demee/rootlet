#pragma once

constexpr int moisturePercent(int reading) {
  return reading <= 1500 ? 100 : reading >= 3000 ? 0
      : (3000 - reading) * 100 / 1500;
}

static_assert(moisturePercent(1000) == 100, "Wet readings must be clamped");
static_assert(moisturePercent(1500) == 100, "Wet calibration");
static_assert(moisturePercent(2250) == 50, "Midpoint calibration");
static_assert(moisturePercent(3000) == 0, "Dry calibration");
static_assert(moisturePercent(3500) == 0, "Dry readings must be clamped");
