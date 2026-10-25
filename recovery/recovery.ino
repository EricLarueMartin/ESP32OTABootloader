// Permanent network recovery candidate. No motor GPIO or target-MCU operations.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "http_range.h"
#include <Preferences.h>
#include <SHA2Builder.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <nvs_flash.h>
#include <esp_mac.h>
#include <mbedtls/base64.h>
#include "update_state.h"
#include "identity.h"
#include "readback.h"
#include "authentication.h"
#include "arduino_startup.h"
#include "boot_window.h"
#include "update_page.h"
using namespace recovery_v1;
WebServer api(80);
Preferences cfg;
const esp_partition_t *mainPart = nullptr, *statePart = nullptr, *factoryPart = nullptr;
Record record;
int recordSlot = -1;
bool storageOK = false, downloading = false;
String netid, expectedAuth, url, lastError = "unconfigured", usbLine;
bool apiStarted = false, cfgOpened = false;
uint8_t buffer[SECTOR], verifyBuffer[256];
uint32_t restartAt = 0, lastWifiRetry = 0, sourceRetryAt = 0, sourceRetryCount = 0;
uint8_t consecutiveSourceFailures = 0;
recovery_boot::Window bootWindow;
bool setBootMarker(uint8_t marker) {
  if (cfg.putUChar("boot_window", marker) == 1 && cfg.getUChar("boot_window", 255) == marker)
    return true;
  bootWindow.cancel();
  lastError = "boot_marker_write_failed";
  return false;
}
struct Port {
  bool erase(int s) { return esp_partition_erase_range(statePart, s * SECTOR, SECTOR) == ESP_OK; }
  bool write(int s, const Record &r) {
    return esp_partition_write(statePart, s * SECTOR, &r, sizeof r) == ESP_OK;
  }
  bool read(int s, Record &r) {
    return esp_partition_read(statePart, s * SECTOR, &r, sizeof r) == ESP_OK;
  }
} port;
bool save(Record next) {
  if (!storageOK || !persist(port, next, recordSlot, mainPart->size)) {
    storageOK = false;
    lastError = "state_write_failed";
    return false;
  }
  record = next;
  return true;
}
bool hex(const String &s, uint8_t *out, size_t n) {
  if (s.length() != n * 2)
    return false;
  for (size_t i = 0; i < n; ++i) {
    int v = 0;
    for (unsigned j = 0; j < 2; ++j) {
      char c = s[2 * i + j];
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
      if (d < 0)
        return false;
      v = (v << 4) | d;
    }
    out[i] = uint8_t(v);
  }
  return true;
}
bool auth() { return recovery_auth::verify(api.header("Authorization"), expectedAuth); }
bool allowed() {
  if (!auth()) {
    api.send(401, "text/plain", "unauthorized");
    return false;
  }
  return true;
}
bool recoverDefault() {
  return factoryPart && esp_ota_set_boot_partition(factoryPart) == ESP_OK &&
         esp_ota_get_boot_partition() == factoryPart;
}
bool verifyWrite(uint32_t off, size_t n) {
  return recovery_io::verify(
      [](uint32_t at, uint8_t *out, size_t count) {
        return esp_partition_read(mainPart, at, out, count) == ESP_OK;
      },
      off, buffer, n, verifyBuffer, sizeof verifyBuffer);
}
bool imageVerified() {
  SHA256Builder h;
  h.begin();
  for (uint32_t off = 0; off < record.size;) {
    size_t n = min(uint32_t(SECTOR), record.size - off);
    if (esp_partition_read(mainPart, off, buffer, n) != ESP_OK)
      return false;
    h.add(buffer, n);
    off += n;
    delay(1);
  }
  h.calculate();
  uint8_t digest[32];
  h.getBytes(digest);
  if (memcmp(digest, record.sha, 32))
    return false;
  esp_partition_pos_t pos = {mainPart->address, mainPart->size};
  esp_image_metadata_t info{};
  return esp_image_verify(ESP_IMAGE_VERIFY, &pos, &info) == ESP_OK;
}
String digestHex(const uint8_t *bytes, size_t n) {
  static const char alphabet[] = "0123456789abcdef";
  String result;
  result.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    result += alphabet[bytes[i] >> 4];
    result += alphabet[bytes[i] & 15];
  }
  return result;
}
String jsonText(const char *text, size_t limit) {
  String out = "\"";
  for (size_t i = 0; i < limit && text[i]; ++i) {
    unsigned char c = text[i];
    if (c == '"' || c == '\\')
      out += '\\';
    if (c >= 32 && c < 127)
      out += char(c);
    else
      out += '?';
  }
  return out + "\"";
}
String releaseMetadata() {
  if (record.phase == Empty)
    return "null";
  String saved = cfg.getString("image_meta", "");
  if (saved.length() < 68 || saved.substring(0, 64) != digestHex(record.sha, 32) ||
      saved[64] != '|')
    return "null";
  int split = saved.indexOf('|', 65);
  if (split < 0)
    return "null";
  String name = saved.substring(65, split), version = saved.substring(split + 1);
  if (!validImageLabel(name.c_str()) || !validImageLabel(version.c_str()))
    return "null";
  return "{\"name\":\"" + name + "\",\"version\":\"" + version + "\"}";
}
String firmwareMetadata() {
  // The descriptor is part of the checksum-bound image, not a second mutable record.
  if (record.phase == Empty || record.phase == Installing)
    return "null";
  esp_app_desc_t d{};
  if (esp_ota_get_partition_description(mainPart, &d) != ESP_OK)
    return "null";
  return "{\"project\":" + jsonText(d.project_name, sizeof d.project_name) +
         ",\"version\":" + jsonText(d.version, sizeof d.version) +
         ",\"idf\":" + jsonText(d.idf_ver, sizeof d.idf_ver) +
         ",\"build_date\":" + jsonText(d.date, sizeof d.date) +
         ",\"build_time\":" + jsonText(d.time, sizeof d.time) + ",\"elf_sha256\":\"" +
         digestHex(d.app_elf_sha256, 32) + "\"}";
}
void fail(const char *s) {
  downloading = false;
  url = "";
  lastError = s;
}
void retrySource(const char *reason) {
  ++sourceRetryCount;
  if (++consecutiveSourceFailures <= 3) {
    sourceRetryAt = millis() + 500;
    lastError = String("retry_") + reason;
  } else
    fail(reason);
}
void downloadChunk() {
  if (!storageOK || record.phase != Installing) {
    fail("not_installing");
    return;
  }
  if (record.committed == record.size) {
    if (!imageVerified()) {
      fail("image_verification_failed");
      return;
    }
    Record next = record;
    next.phase = Ready;
    if (save(next))
      lastError = "ready";
    downloading = false;
    url = "";
    return;
  }
  uint32_t off = record.committed;
  size_t n = min(uint32_t(SECTOR), record.size - off);
  recovery_http::Url source;
  if (!recovery_http::parseUrl(url.c_str(), source)) {
    fail("source_url_invalid");
    return;
  }
  NetworkClient client;
  if (!client.connect(source.host, source.port, 1500)) {
    retrySource("source_connect_failed");
    return;
  }
  sourceRetryAt = 0;
  lastError = "downloading";
  client.printf(
      "GET %s HTTP/1.1\r\nHost: %s:%u\r\nRange: bytes=%lu-%lu\r\nConnection: close\r\n\r\n",
      source.path, source.host, unsigned(source.port), (unsigned long)off,
      (unsigned long)(off + n - 1));
  recovery_http::Headers headers(off, n, record.size);
  char line[256];
  size_t used = 0, received = 0;
  bool cr = false, ok = true;
  uint32_t started = millis();
  // One bounded deadline for all headers/body; slow peers cannot starve the watchdog.
  while (millis() - started < 2500 && received < n) {
    if (!client.available()) {
      if (!client.connected())
        break;
      delay(1);
      continue;
    }
    if (!headers.complete) {
      int value = client.read();
      if (value < 0)
        continue;
      char c = char(value);
      if (c == '\r') {
        if (cr) {
          ok = false;
          break;
        }
        cr = true;
        continue;
      }
      if (c == '\n') {
        if (!cr) {
          ok = false;
          break;
        }
        line[used] = 0;
        cr = false;
        used = 0;
        if (!headers.consume(line)) {
          ok = false;
          break;
        }
      } else {
        if (cr || used >= sizeof line - 1 || ((unsigned char)c < 32 && c != '\t') ||
            (unsigned char)c >= 127) {
          ok = false;
          break;
        }
        line[used++] = c;
      }
    } else {
      int count = client.read(buffer + received, n - received);
      if (count < 0) {
        ok = false;
        break;
      }
      received += size_t(count);
    }
  }
  client.stop();
  if (!ok) {
    fail("source_headers_invalid");
    return;
  }
  // Re-requesting an incomplete range is safe: nothing has been erased/written.
  if (!headers.complete) {
    retrySource("source_headers_incomplete");
    return;
  }
  if (received != n) {
    retrySource("source_body_incomplete");
    return;
  }
  if (!chunkAllowed(record, off, n)) {
    fail("invalid_chunk_state");
    return;
  }
  consecutiveSourceFailures = 0;
  // Persistent boot selection was verified before begin. Never erase recovery.
  if (esp_ota_get_boot_partition() != factoryPart ||
      esp_partition_erase_range(mainPart, off, SECTOR) != ESP_OK ||
      esp_partition_write(mainPart, off, buffer, n) != ESP_OK || !verifyWrite(off, n)) {
    fail("chunk_flash_failed");
    return;
  }
  Record next = record;
  next.committed += n;
  if (!save(next))
    fail("chunk_checkpoint_failed");
}
void usbService() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r')
      continue;
    if (c != '\n') {
      if (usbLine.length() < 512)
        usbLine += c;
      else
        usbLine = "";
      continue;
    }
    String line = usbLine;
    usbLine = "";
    if (line == "netstatus") {
      uint8_t mac[6];
      esp_read_mac(mac, ESP_MAC_WIFI_STA);
      char text[18];
      snprintf(text, sizeof text, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
               mac[4], mac[5]);
      Serial.println(String("{\"role\":\"recovery\",\"protocol\":1,\"mac\":\"") + text +
                     "\",\"ip\":\"" + WiFi.localIP().toString() +
                     "\",\"configured\":" + (expectedAuth.length() == 64 ? "true" : "false") +
                     ",\"wifi_status\":" + String((int)WiFi.status()) + "}");
    } else if (line.startsWith("RECOVERYCFG ") && cfgOpened && expectedAuth.isEmpty()) {
      int a = line.indexOf(' ', 12), b = a < 0 ? -1 : line.indexOf(' ', a + 1),
          d = b < 0 ? -1 : line.indexOf(' ', b + 1);
      if (a < 0 || b < 0 || d < 0) {
        Serial.println("CFG_INVALID");
        continue;
      }
      String ss = line.substring(12, a), pw = line.substring(a + 1, b),
             hash = line.substring(b + 1, d), id = line.substring(d + 1);
      uint8_t ignored[32], ssid[33]{}, pass[65]{};
      size_t ns = 0, np = 0;
      bool ok =
          validNetid(id.c_str()) && hex(hash, ignored, 32) &&
          mbedtls_base64_decode(ssid, 32, &ns, (const uint8_t *)ss.c_str(), ss.length()) == 0 &&
          ns > 0 &&
          mbedtls_base64_decode(pass, 64, &np, (const uint8_t *)pw.c_str(), pw.length()) == 0;
      if (!ok) {
        Serial.println("CFG_INVALID");
        continue;
      }
      ok = cfg.putString("pass", String((char *)pass, np)) == np &&
           cfg.putString("ssid", String((char *)ssid, ns)) == ns;
      ok = ok && cfg.getString("ssid", "") == String((char *)ssid, ns) &&
           cfg.getString("pass", "") == String((char *)pass, np);
      ok = ok && cfg.putString("netid", id) == id.length() && cfg.getString("netid", "") == id;
      ok = ok && cfg.putString("auth_sha", hash) == 64 && cfg.getString("auth_sha", "") == hash;
      memset(pass, 0, sizeof pass);
      line = "";
      pw = "";
      if (ok) {
        Serial.println("CFG_STORED_RESTARTING");
        Serial.flush();
        esp_restart();
      } else
        Serial.println("CFG_STORAGE_FAILED");
    } else
      Serial.println("UNSUPPORTED_OR_ALREADY_CONFIGURED");
  }
}
void setup() {
  Serial.begin(115200);
  enableLoopWDT();
  // Refuse to execute the updater from the writable main slot.
  factoryPart =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "rescue");
  mainPart =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "main");
  statePart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, esp_partition_subtype_t(0x40),
                                       "update_state");
  if (!factoryPart || !mainPart || !statePart || esp_ota_get_running_partition() != factoryPart)
    return;
  if (nvs_flash_init_partition("recovery_cfg") != ESP_OK ||
      !cfg.begin("loader", false, "recovery_cfg"))
    return;
  cfgOpened = true;
  netid = cfg.getString("netid", "");
  expectedAuth = cfg.getString("auth_sha", "");
  String ssid = cfg.getString("ssid", ""), pass = cfg.getString("pass", "");
  if (ssid.isEmpty() || expectedAuth.length() != 64 || !validNetid(netid.c_str()))
    return;
  Record a, b;
  if (port.read(0, a) && port.read(1, b)) {
    recordSlot = newest(a, b, mainPart->size);
    if (recordSlot >= 0) {
      record = recordSlot ? b : a;
      storageOK = true;
    } else {
      bool blank = true;
      for (auto v : reinterpret_cast<const uint8_t(&)[sizeof(Record)]>(a))
        blank &= v == 255;
      for (auto v : reinterpret_cast<const uint8_t(&)[sizeof(Record)]>(b))
        blank &= v == 255;
      storageOK = blank;
    }
  }
  // Always stay available after reboot; no automatic motion or application retry.
  if (!recoverDefault())
    storageOK = false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(netid.c_str());
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  const char *headers[] = {"Authorization", "X-Image-SHA256", "X-Image-Size", "X-Image-Name",
                           "X-Image-Version"};
  api.collectHeaders(headers, 5);
  registerUpdatePage(api);
  api.on("/", HTTP_GET,
         [] { api.send(200, "text/html", "<a href='/update'>Firmware update</a>"); });
  api.on("/v1/maintenance", HTTP_POST, [] {
    if (!allowed())
      return;
    bootWindow.cancel();
    if (!storageOK || restartAt || !setBootMarker(0)) {
      api.send(409, "text/plain", "busy_or_storage_invalid");
      return;
    }
    if (record.phase == Healthy) {
      Record next = record;
      next.phase = Hold;
      if (!save(next)) {
        api.send(500, "text/plain", "state_write_failed");
        return;
      }
    }
    api.send(200, "text/plain", "recovery_session_open");
  });
  api.on("/v1/status", HTTP_GET, [] {
    if (!allowed())
      return;
    api.send(200, "application/json",
             "{\"protocol\":1,\"role\":\"recovery\",\"storage_ok\":" +
                 String(storageOK ? "true" : "false") +
                 ",\"boot_window_ms\":" + String(bootWindow.remaining(millis())) +
                 ",\"boot_window_mode\":" + String(int(bootWindow.mode)) + ",\"phase\":" +
                 String(record.phase) + ",\"committed\":" + String(record.committed) +
                 ",\"size\":" + String(record.size) + ",\"capacity\":" + String(mainPart->size) +
                 ",\"downloading\":" + String(downloading ? "true" : "false") + ",\"result\":\"" +
                 lastError + "\",\"netid\":\"" + netid + "\",\"image_sha256\":\"" +
                 digestHex(record.sha, 32) + "\",\"source_retries\":" + String(sourceRetryCount) +
                 ",\"firmware\":" + firmwareMetadata() + ",\"release\":" + releaseMetadata() + "}");
  });
  api.on("/v1/begin", HTTP_POST, [] {
    if (!allowed())
      return;
    uint8_t sha[32];
    String s = api.header("X-Image-Size");
    uint64_t size = 0;
    bool digits = s.length() > 0 && s.length() < 9;
    for (char c : s) {
      digits &= c >= '0' && c <= '9';
      size = size * 10 + uint32_t(c - '0');
    }
    if (!storageOK || downloading || restartAt || !digits || size == 0 || size > mainPart->size ||
        !hex(api.header("X-Image-SHA256"), sha, 32)) {
      api.send(409, "text/plain", "bad_manifest_or_busy");
      return;
    }
    // Reattaching the same installation never erases already committed sectors.
    if (record.phase == Installing) {
      api.send(sameImage(record, uint32_t(size), sha) ? 200 : 409, "text/plain",
               sameImage(record, uint32_t(size), sha) ? "resume_existing"
                                                      : "different_image_in_progress");
      return;
    }
    if (!recoverDefault()) {
      api.send(500, "text/plain", "cannot_select_recovery");
      return;
    }
    String imageName = api.header("X-Image-Name"), imageVersion = api.header("X-Image-Version");
    if (imageName.isEmpty())
      imageName = "firmware";
    if (imageVersion.isEmpty())
      imageVersion = digestHex(sha, 32).substring(0, 16);
    if (!validImageLabel(imageName.c_str()) || !validImageLabel(imageVersion.c_str())) {
      api.send(400, "text/plain", "invalid_release_label");
      return;
    }
    // Commit one atomic NVS value before the journal. A reset between commits
    // yields missing metadata rather than falsely labeling the previous image.
    String metadata = digestHex(sha, 32) + "|" + imageName + "|" + imageVersion;
    if (cfg.putString("image_meta", metadata) != metadata.length() ||
        cfg.getString("image_meta", "") != metadata) {
      api.send(500, "text/plain", "metadata_write_failed");
      return;
    }
    bootWindow.cancel();
    Record next;
    next.generation = record.generation;
    next.phase = Installing;
    next.size = uint32_t(size);
    memcpy(next.sha, sha, 32);
    api.send(save(next) ? 200 : 500, "text/plain",
             storageOK ? "manifest_committed" : "state_write_failed");
  });
  api.on("/v1/abandon", HTTP_POST, [] {
    if (!allowed())
      return;
    if (!storageOK || downloading || restartAt || !recoverDefault()) {
      api.send(409, "text/plain", "busy_or_storage_invalid");
      return;
    }
    bootWindow.cancel();
    Record next;
    next.generation = record.generation;
    api.send(save(next) ? 200 : 500, "text/plain",
             storageOK ? "update_abandoned_recovery_retained" : "state_write_failed");
  });
  api.on("/v1/download", HTTP_POST, [] {
    if (!allowed())
      return;
    String source = api.arg("plain");
    if (!storageOK || downloading || restartAt || record.phase != Installing ||
        !source.startsWith("http://") || source.length() > 512 || source.indexOf('\n') >= 0 ||
        source.indexOf('\r') >= 0 || source.indexOf('@') >= 0) {
      api.send(409, "text/plain", "invalid_source_or_state");
      return;
    }
    url = source;
    consecutiveSourceFailures = 0;
    sourceRetryAt = 0;
    downloading = true;
    lastError = "downloading";
    api.send(202, "text/plain", "started");
  });
  api.on("/v1/restart", HTTP_POST, [] {
    if (!allowed())
      return;
    downloading = false;
    url = "";
    bootWindow.cancel();
    api.send(200, "text/plain", "restart_to_recovery");
    restartAt = millis() + 300;
  });
  api.on("/v1/hold", HTTP_POST, [] {
    if (!allowed())
      return;
    if (!storageOK || downloading || restartAt || record.committed != record.size || !record.size) {
      api.send(409, "text/plain", "busy_or_incomplete");
      return;
    }
    bootWindow.cancel();
    Record next = record;
    next.phase = Hold;
    api.send(save(next) ? 200 : 500, "text/plain", storageOK ? "held_in_recovery" : "state_failed");
  });
  api.on("/v1/launch", HTTP_POST, [] {
    if (!allowed())
      return;
    if (!storageOK || downloading || restartAt ||
        !(record.phase == Ready || record.phase == Healthy || record.phase == Hold) ||
        !imageVerified()) {
      api.send(409, "text/plain", "unverified_or_busy");
      return;
    }
    Record next = record;
    next.phase = Trial;
    if (!setBootMarker(0) || !save(next) || esp_ota_set_boot_partition(mainPart) != ESP_OK) {
      api.send(500, "text/plain", "launch_failed");
      return;
    }
    api.send(200, "text/plain", "software_restart_scheduled");
    restartAt = millis() + 500;
  });
  api.begin();
  apiStarted = true;
  lastError = storageOK ? "idle" : "state_invalid";
  auto mode =
      recovery_boot::choose(esp_reset_reason() == ESP_RST_POWERON,
                            storageOK && record.phase == Healthy, cfg.getUChar("boot_window", 0));
  if (mode != recovery_boot::Mode::Stay &&
      setBootMarker(mode == recovery_boot::Mode::Gesture ? 1 : 2))
    bootWindow.begin(mode, millis());
}
void loop() {
  usbService();
  if (apiStarted) {
    api.handleClient();
    if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetry >= 30000) {
      lastWifiRetry = millis();
      WiFi.reconnect();
    }
  }
  if (downloading && (!sourceRetryAt || int32_t(millis() - sourceRetryAt) >= 0))
    downloadChunk();
  bootWindow.network(WiFi.status() == WL_CONNECTED, millis());
  if (bootWindow.expired(millis())) {
    bootWindow.cancel();
    if (storageOK && record.phase == Healthy && !downloading && imageVerified()) {
      Record next = record;
      next.phase = Trial;
      if (setBootMarker(0) && save(next) && esp_ota_set_boot_partition(mainPart) == ESP_OK)
        restartAt = millis() + 500;
    }
  }
  if (restartAt && int32_t(millis() - restartAt) >= 0)
    esp_restart();
  delay(2);
}
