#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace recovery_io {
template <class Reader>
bool verify(Reader read, uint32_t off, const uint8_t *source, size_t n, uint8_t *scratch,
            size_t capacity) {
  if (!capacity)
    return false;
  for (size_t i = 0; i < n; i += capacity) {
    size_t count = n - i < capacity ? n - i : capacity;
    if (!read(off + uint32_t(i), scratch, count) || memcmp(source + i, scratch, count))
      return false;
  }
  return true;
}
} // namespace recovery_io
