#pragma once
#include <stdint.h>
namespace recovery_boot {
constexpr uint32_t GestureMs = 10000, MaintenanceMs = 15 * 60 * 1000;
enum class Mode { Stay, Gesture, Maintenance };
// Marker survives power loss. No RTC: off-time is deliberately not measured.
inline Mode choose(bool cold, bool healthy, uint8_t marker) {
  if (!cold || !healthy)
    return Mode::Stay;
  return marker ? Mode::Maintenance : Mode::Gesture;
}
struct Window {
  Mode mode = Mode::Stay;
  bool ticking = false;
  uint32_t started = 0;
  void begin(Mode m, uint32_t now) {
    mode = m;
    started = now;
    ticking = m == Mode::Gesture;
  }
  void cancel() {
    mode = Mode::Stay;
    ticking = false;
  }
  void network(bool connected, uint32_t now) {
    if (mode != Mode::Maintenance)
      return;
    if (!connected)
      ticking = false;
    else if (!ticking) {
      started = now;
      ticking = true;
    }
  }
  uint32_t remaining(uint32_t now) const {
    uint32_t duration = mode == Mode::Gesture ? GestureMs : MaintenanceMs;
    if (mode == Mode::Stay)
      return 0;
    if (!ticking)
      return duration;
    uint32_t elapsed = now - started;
    return elapsed >= duration ? 0 : duration - elapsed;
  }
  bool expired(uint32_t now) const { return ticking && remaining(now) == 0; }
};
} // namespace recovery_boot
