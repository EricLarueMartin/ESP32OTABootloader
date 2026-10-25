// No actuator control. Qualification image for software-only recovery return.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SHA2Builder.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include "application_entry.h"
#include "authentication.h"
#include "identity.h"
#ifdef SELFTEST_FAIL_EARLY
__attribute__((constructor)) static void failBeforeApplicationEntry() { esp_restart(); }
#endif
WebServer api(80);
Preferences cfg;
String authHash;
bool configured = false, healthy = false;
uint32_t restartAt = 0;
bool auth() { return recovery_auth::verify(api.header("Authorization"), authHash); }
void setup() {
  enableLoopWDT();
  if (nvs_flash_init_partition("recovery_cfg") != ESP_OK ||
      !cfg.begin("loader", true, "recovery_cfg"))
    return;
  String netid = cfg.getString("netid", "");
  if (!validNetid(netid.c_str()))
    return;
  String ssid = cfg.getString("ssid", ""), pass = cfg.getString("pass", "");
  authHash = cfg.getString("auth_sha", "");
  if (ssid.isEmpty() || authHash.length() != 64)
    return;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(netid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  const char *headers[] = {"Authorization"};
  api.collectHeaders(headers, 1);
  api.on("/v1/status", HTTP_GET, [] {
    if (!auth()) {
      api.send(401, "text/plain", "unauthorized");
      return;
    }
    auto p = esp_ota_get_boot_partition();
    api.send(200, "application/json",
             String("{\"role\":\"main_selftest\",\"no_actuators\":true,\"boot_partition\":\"") +
                 (p ? p->label : "unknown") + "\",\"heap\":" + String(ESP.getFreeHeap()) + "}");
  });
  api.on("/v1/recover", HTTP_POST, [] {
    if (!auth()) {
      api.send(401, "text/plain", "unauthorized");
      return;
    }
    auto p = esp_ota_get_boot_partition();
    if (!p || strcmp(p->label, "rescue")) {
      api.send(409, "text/plain", "recovery_not_selected");
      return;
    }
    api.send(200, "text/plain", "software_restart_to_recovery");
    restartAt = millis() + 300;
  });
  api.begin();
  configured = true;
}
void loop() {
  if (configured && !healthy && WiFi.status() == WL_CONNECTED)
    healthy = permanent_recovery::healthy();
#ifdef SELFTEST_HANG_AFTER_HEALTH
  if (healthy) {
    for (;;)
      delay(1);
  } // Deliberately withhold loop-task watchdog feeding.
#endif
  if (configured)
    api.handleClient();
  if (restartAt && int32_t(millis() - restartAt) >= 0)
    esp_restart();
  delay(2);
}
