#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace recovery_v1 {
constexpr uint32_t MAGIC = 0x31564352, SECTOR = 4096;
enum Phase : uint32_t { Empty = 0, Installing = 1, Ready = 2, Trial = 3, Healthy = 4, Hold = 5 };
struct Record {
  uint32_t magic = MAGIC, version = 1, generation = 0, phase = Empty;
  uint32_t size = 0, committed = 0;
  uint8_t sha[32]{};
  uint32_t crc = 0;
};
static_assert(sizeof(Record) == 60, "persistent ABI");
inline uint32_t checksum(const Record &r) {
  uint32_t c = 0xffffffff;
  auto p = reinterpret_cast<const uint8_t *>(&r);
  for (size_t i = 0; i < offsetof(Record, crc); ++i) {
    c ^= p[i];
    for (unsigned b = 0; b < 8; ++b)
      c = (c >> 1) ^ (0xedb88320u & uint32_t(-int32_t(c & 1)));
  }
  return ~c;
}
inline bool valid(const Record &r, uint32_t capacity) {
  return r.magic == MAGIC && r.version == 1 && r.phase <= Hold && r.crc == checksum(r) &&
         r.size <= capacity && r.committed <= r.size &&
         (r.committed == r.size || r.committed % SECTOR == 0) &&
         (r.phase == Empty ? (r.size == 0 && r.committed == 0) : r.size > 0) &&
         ((r.phase == Ready || r.phase == Trial || r.phase == Healthy || r.phase == Hold)
              ? r.committed == r.size
              : true);
}
inline int newest(const Record &a, const Record &b, uint32_t cap) {
  bool av = valid(a, cap), bv = valid(b, cap);
  if (!av)
    return bv ? 1 : -1;
  if (!bv)
    return 0;
  return int32_t(b.generation - a.generation) > 0 ? 1 : 0;
}
// Port stores two independently erased 4K sectors. The old sector remains valid
// until the entire successor is read back. No application/NVS erase here.
template <class Port> bool persist(Port &p, Record &r, int &slot, uint32_t capacity) {
  Record next = r;
  next.generation++;
  next.crc = checksum(next);
  if (!valid(next, capacity))
    return false;
  int dest = slot == 0 ? 1 : 0;
  if (!p.erase(dest) || !p.write(dest, next))
    return false;
  Record back;
  if (!p.read(dest, back) || memcmp(&next, &back, sizeof next))
    return false;
  r = next;
  slot = dest;
  return true;
}
inline bool chunkAllowed(const Record &r, uint32_t offset, size_t n) {
  return r.phase == Installing && offset == r.committed && offset % SECTOR == 0 &&
         offset < r.size && n == (r.size - offset < SECTOR ? r.size - offset : SECTOR);
}
inline bool sameImage(const Record &r, uint32_t size, const uint8_t sha[32]) {
  return r.size == size && !memcmp(r.sha, sha, 32);
}
} // namespace recovery_v1
