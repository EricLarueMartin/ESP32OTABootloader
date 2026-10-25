#pragma once
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <sdkconfig.h>
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3, 3, 5)
#error "Startup adapter requires Arduino ESP32 3.3.5; review and qualify SDK changes"
#endif
#include <esp_wifi.h>
#if !CONFIG_IDF_TARGET_ESP32C6 && !CONFIG_IDF_TARGET_ESP32
#error                                                                                             \
    "Only ESP32 and ESP32C6 candidate profiles are supported; qualify each device before deployment"
#endif
extern "C" void initVariant(void);
extern "C" void init(void);
// Pinned Arduino 3.3.5 initArduino() automatically erases default NVS on certain
// errors and auto-confirms OTA. Neither behavior belongs in permanent recovery.
// Link with --wrap=initArduino; SDK startup still initializes RTOS/hardware first.
extern "C" void __wrap_initArduino() {
  auto running = esp_ota_get_running_partition();
  auto rescue =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "rescue");
  if (!running || !rescue)
    esp_restart();
  if (running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    if (esp_ota_set_boot_partition(rescue) != ESP_OK || esp_ota_get_boot_partition() != rescue)
      esp_restart();
    if (nvs_flash_init() != ESP_OK)
      esp_restart(); // preserve failed default NVS, never erase
  }
  // Preserve Seeed's on-board antenna enable/select, not a replacement pin map.
  init();
  initVariant();
}

// Arduino persistent(false) only changes storage AFTER esp_wifi_init(). Disable
// Wi-Fi NVS in the initialization configuration too, so rescue does not depend
// on the application's default NVS being healthy or initialized.
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t *);
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *input) {
  if (!input)
    return ESP_ERR_INVALID_ARG;
  wifi_init_config_t config = *input;
  config.nvs_enable = 0;
  return __real_esp_wifi_init(&config);
}
