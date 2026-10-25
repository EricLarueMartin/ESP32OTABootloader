#pragma once
#include <Arduino.h>
#include <SHA2Builder.h>
namespace recovery_auth {
// 256-bit random device passwords; fixed-size constant-time digest comparison.
inline bool verify(String token, const String &expected) {
  if (expected.length() != 64 || token.length() != 71 || !token.startsWith("Bearer "))
    return false;
  token.remove(0, 7);
  SHA256Builder h;
  h.begin();
  h.add((const uint8_t *)token.c_str(), token.length());
  h.calculate();
  String actual = h.toString();
  unsigned diff = 0;
  for (unsigned i = 0; i < 64; ++i)
    diff |= actual[i] ^ expected[i];
  return diff == 0;
}
} // namespace recovery_auth
