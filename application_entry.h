#pragma once
#include <Arduino.h>
#include <esp_ota_ops.h>
#include "update_state.h"
#include "arduino_startup.h"
namespace permanent_recovery {
struct Port {
  const esp_partition_t *p;
  bool erase(int s) {
    return esp_partition_erase_range(p, s * recovery_v1::SECTOR, recovery_v1::SECTOR) == ESP_OK;
  }
  bool write(int s, const recovery_v1::Record &r) {
    return esp_partition_write(p, s * recovery_v1::SECTOR, &r, sizeof r) == ESP_OK;
  }
  bool read(int s, recovery_v1::Record &r) {
    return esp_partition_read(p, s * recovery_v1::SECTOR, &r, sizeof r) == ESP_OK;
  }
};
// Call only after application self-test, watchdog, authenticated maintenance
// endpoint and safe actuator state are established. Never call merely on setup.
inline bool healthy() {
  using namespace recovery_v1;
  auto state = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, esp_partition_subtype_t(0x40),
                                        "update_state");
  auto app = esp_ota_get_running_partition();
  auto boot = esp_ota_get_boot_partition();
  if (!state || !app || app->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 || !boot ||
      boot->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY)
    return false;
  Port p{state};
  Record a, b;
  if (!p.read(0, a) || !p.read(1, b))
    return false;
  int slot = newest(a, b, app->size);
  if (slot < 0)
    return false;
  Record r = slot ? b : a;
  if (r.phase == Healthy)
    return true;
  if (r.phase != Trial)
    return false;
  r.phase = Healthy;
  return persist(p, r, slot, app->size);
}
inline bool available() {
  auto p = esp_ota_get_boot_partition();
  return p && p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY && !strcmp(p->label, "rescue");
}
// Caller must stop/quiesce its own actuator before this function. No target
// journal can veto networking or firmware repair once hardware is quiescent.
inline bool restart() {
  if (!available())
    return false;
  esp_restart();
  return true;
}
} // namespace permanent_recovery
