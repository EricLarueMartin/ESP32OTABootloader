#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
namespace recovery_http {
struct Url {
  char host[254]{};
  uint16_t port = 80;
  const char *path = nullptr;
};
inline bool parseUrl(const char *text, Url &out) {
  if (!text || strncmp(text, "http://", 7) || strlen(text) > 512)
    return false;
  const char *start = text + 7;
  const char *slash = strchr(start, '/');
  if (!slash || slash == start)
    return false;
  const char *colon = nullptr;
  for (const char *q = start; q < slash; ++q) {
    char c = *q;
    if (c == ':') {
      if (colon)
        return false;
      colon = q;
    } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '.' || c == '-'))
      return false;
  }
  size_t n = size_t((colon ? colon : slash) - start);
  if (!n || n >= sizeof out.host)
    return false;
  memcpy(out.host, start, n);
  out.host[n] = 0;
  out.port = 80;
  if (colon) {
    unsigned value = 0;
    if (colon + 1 == slash)
      return false;
    for (const char *q = colon + 1; q < slash; ++q) {
      if (*q < '0' || *q > '9')
        return false;
      value = value * 10 + unsigned(*q - '0');
      if (value > 65535)
        return false;
    }
    if (!value)
      return false;
    out.port = uint16_t(value);
  }
  for (const char *q = slash; *q; ++q)
    if ((unsigned char)*q < 33 || (unsigned char)*q > 126 || *q == '#' || *q == '\\')
      return false;
  out.path = slash;
  return true;
}
inline bool equals(const char *a, const char *b) {
  for (; *a && *b; ++a, ++b) {
    char c = *a, d = *b;
    if (c >= 'A' && c <= 'Z')
      c += 32;
    if (d >= 'A' && d <= 'Z')
      d += 32;
    if (c != d)
      return false;
  }
  return *a == *b;
}
struct Headers {
  size_t expected, bytes = 0;
  char range[64]{};
  bool status = false, length = false, contentRange = false, complete = false;
  Headers(uint32_t off, size_t n, uint32_t total) : expected(n) {
    snprintf(range, sizeof range, "bytes %lu-%lu/%lu", (unsigned long)off,
             (unsigned long)(off + n - 1), (unsigned long)total);
  }
  bool consume(char *line) {
    bytes += strlen(line) + 2;
    if (complete || bytes > 2048)
      return false;
    if (!status) {
      status = true;
      return (!strncmp(line, "HTTP/1.1 206", 12) || !strncmp(line, "HTTP/1.0 206", 12)) &&
             (line[12] == 0 || line[12] == ' ');
    }
    if (!*line) {
      complete = length && contentRange;
      return complete;
    }
    char *colon = strchr(line, ':');
    if (!colon || colon == line || line[0] == ' ' || line[0] == '\t')
      return false;
    *colon = 0;
    char *value = colon + 1;
    while (*value == ' ' || *value == '\t')
      ++value;
    size_t count = strlen(value);
    while (count && (value[count - 1] == ' ' || value[count - 1] == '\t'))
      value[--count] = 0;
    if (equals(line, "Content-Length")) {
      if (length || !count)
        return false;
      length = true;
      size_t n = 0;
      for (const char *q = value; *q; ++q) {
        if (*q < '0' || *q > '9')
          return false;
        n = n * 10 + size_t(*q - '0');
        if (n > expected)
          return false;
      }
      return n == expected;
    }
    if (equals(line, "Content-Range")) {
      if (contentRange)
        return false;
      contentRange = true;
      return !strcmp(value, range);
    }
    if (equals(line, "Transfer-Encoding") || equals(line, "Content-Encoding"))
      return false;
    // Reject whitespace in a field name (including disguised framing headers).
    for (const char *q = line; *q; ++q)
      if ((unsigned char)*q <= 32 || (unsigned char)*q >= 127)
        return false;
    return true;
  }
};
} // namespace recovery_http
