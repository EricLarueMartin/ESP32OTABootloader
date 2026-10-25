#include "recovery/update_state.h"
#include <array>
#include "recovery/identity.h"
#include "recovery/readback.h"
#include "recovery/http_range.h"
#include <stdexcept>
#include <cstdio>
using namespace recovery_v1;
unsigned checks = 0;
void check(bool b) {
  ++checks;
  if (!b)
    throw std::runtime_error("assertion");
}
struct Port {
  std::array<std::array<uint8_t, 4096>, 2> flash;
  int cut = -1, steps = 0;
  Port() {
    for (auto &s : flash)
      s.fill(255);
  }
  bool step() { return steps++ != cut; }
  bool erase(int s) {
    if (!step())
      return false;
    flash[s].fill(255);
    return step();
  }
  bool write(int s, const Record &r) {
    const auto *b = reinterpret_cast<const uint8_t *>(&r);
    for (size_t i = 0; i < sizeof r; ++i) {
      if (!step())
        return false;
      flash[s][i] &= b[i];
    }
    return step();
  }
  bool read(int s, Record &r) {
    if (!step())
      return false;
    memcpy(&r, flash[s].data(), sizeof r);
    return true;
  }
  Record raw(int s) {
    Record r;
    memcpy(&r, flash[s].data(), sizeof r);
    return r;
  }
};
bool header(recovery_http::Headers &h, const char *text) {
  char line[256];
  snprintf(line, sizeof line, "%s", text);
  return h.consume(line);
}
int main() {
  recovery_http::Url parsed;
  check(recovery_http::parseUrl("http://host.local:49196/image/test.bin", parsed));
  check(parsed.port == 49196);
  check(!strcmp(parsed.host, "host.local"));
  for (const char *value :
       {"https://host/x", "http://user@host/x", "http://host:0/x", "http://host:65536/x",
        "http://host:123456789012345/x", "http://host/x#fragment", "http://host/ bad",
        "http://host", "http://[::1]/x"})
    check(!recovery_http::parseUrl(value, parsed));
  recovery_http::Headers good(4096, 4096, 9000);
  check(header(good, "HTTP/1.1 206 Partial Content"));
  check(header(good, "content-length: 4096"));
  check(header(good, "Content-Range: bytes 4096-8191/9000"));
  check(header(good, ""));
  check(good.complete);
  for (const char *value :
       {"HTTP/1.1 200 OK", "HTTP/1.1 302 Redirect", "HTTP/1.1 2060 Bad", "garbage"}) {
    recovery_http::Headers h(0, 4096, 9000);
    check(!header(h, value));
  }
  for (const char *value :
       {"Content-Length: -1", "Content-Length: 4097", "Content-Length: 99999999999999999999999",
        "Content-Range: bytes 1-4096/9000", "Transfer-Encoding: chunked", "Content-Encoding: gzip",
        "Content-Length : 4096", " content-length: 4096", "MissingColon"}) {
    recovery_http::Headers h(0, 4096, 9000);
    check(header(h, "HTTP/1.1 206 Partial Content"));
    check(!header(h, value));
  }
  recovery_http::Headers duplicate(0, 1, 1);
  check(header(duplicate, "HTTP/1.0 206 Partial Content"));
  check(header(duplicate, "Content-Length: 1"));
  check(!header(duplicate, "Content-Length: 1"));
  recovery_http::Headers missing(0, 1, 1);
  check(header(missing, "HTTP/1.1 206 Partial Content"));
  check(!header(missing, ""));

  check(validImageLabel("esp32-ota-selftest"));
  check(validImageLabel("0.1.0+abc"));
  for (const char *label : {"", "bad|value", "bad\nvalue", "bad\"value"})
    check(!validImageLabel(label));
  check(validNetid("bedroom-side-blinds"));
  check(validNetid("huzzah32-test"));
  for (const char *id : {"", "-bad", "bad-", "bad_name", "UPPER", "a.b"})
    check(!validNetid(id));
  std::array<uint8_t, 4096> data{};
  uint8_t scratch[256];
  for (size_t n : {size_t(1), size_t(255), size_t(256), size_t(257), size_t(4096)}) {
    auto reader = [&](uint32_t off, uint8_t *out, size_t count) {
      memcpy(out, data.data() + off, count);
      return true;
    };
    check(recovery_io::verify(reader, 0, data.data(), n, scratch, sizeof scratch));
    for (size_t corrupt = 0; corrupt < n; ++corrupt) {
      auto bad = [&](uint32_t off, uint8_t *out, size_t count) {
        memcpy(out, data.data() + off, count);
        if (corrupt >= off && corrupt < off + count)
          out[corrupt - off] ^= 1;
        return true;
      };
      check(!recovery_io::verify(bad, 0, data.data(), n, scratch, sizeof scratch));
    }
  }
  const uint32_t cap = 0x2c0000;
  Record old;
  old.phase = Installing;
  old.size = 9000;
  old.committed = 4096;
  old.generation = 9;
  old.sha[0] = 7;
  old.crc = checksum(old);
  check(valid(old, cap));
  for (int slot = 0; slot < 2; ++slot)
    for (int fail = 0; fail < 67; ++fail) {
      Port p;
      memcpy(p.flash[slot].data(), &old, sizeof old);
      p.cut = fail;
      Record r = old;
      r.committed = 8192;
      int current = slot;
      bool saved = persist(p, r, current, cap);
      Record a = p.raw(0), b = p.raw(1);
      int chosen = newest(a, b, cap);
      check(chosen >= 0);
      Record recovered = chosen ? b : a;
      check(valid(recovered, cap));
      check(recovered.committed == 4096 || recovered.committed == 8192);
      check(!memcmp(p.flash[slot].data(), &old, sizeof old));
      if (saved)
        check(recovered.committed == 8192 && current != slot);
    }
  for (size_t byte = 0; byte < sizeof old; ++byte)
    for (unsigned bit = 0; bit < 8; ++bit) {
      Record r = old;
      reinterpret_cast<uint8_t *>(&r)[byte] ^= uint8_t(1 << bit);
      check(!valid(r, cap));
    }
  check(chunkAllowed(old, 4096, 4096));
  check(!chunkAllowed(old, 0, 4096));
  check(!chunkAllowed(old, 4097, 4096));
  check(!chunkAllowed(old, 4096, 4095));
  old.committed = 8192;
  check(chunkAllowed(old, 8192, 808));
  check(!chunkAllowed(old, 8192, 4096));
  old.phase = Ready;
  check(!chunkAllowed(old, 8192, 808));
  Record a, b;
  a.generation = 0xffffffff;
  a.crc = checksum(a);
  b.generation = 0;
  b.crc = checksum(b);
  check(newest(a, b, cap) == 1);
  // Power loss after writing a chunk but before its record requires replay of that
  // sector. No partially committed offset is accepted by the actual validator.
  for (uint32_t pos = 1; pos < 4096; ++pos) {
    Record r;
    r.phase = Installing;
    r.size = 9000;
    r.committed = pos;
    r.crc = checksum(r);
    check(!valid(r, cap));
  }
  std::printf("%u actual-source recovery journal and chunk checks passed\n", checks);
}
