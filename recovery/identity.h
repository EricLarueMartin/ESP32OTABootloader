#pragma once
#include <stddef.h>
inline bool validNetid(const char *s) {
  if (!s)
    return false;
  size_t n = 0;
  for (; s[n]; ++n) {
    char c = s[n];
    if (n >= 63 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (n > 0 && c == '-')))
      return false;
  }
  return n > 0 && s[n - 1] != '-';
}

inline bool validImageLabel(const char *s) {
  if (!s)
    return false;
  size_t n = 0;
  for (; s[n]; ++n) {
    char c = s[n];
    if (n >= 63 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '.' || c == '_' || c == '-' || c == '+'))
      return false;
  }
  return n > 0;
}
