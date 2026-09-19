#pragma once

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Parse complete AT reply lines only. Registration URCs are not query replies:
// +CEREG: <stat>[,"tac",...] versus +CEREG: <n>,<stat>[,...].
namespace ModemReply {
inline const char* skipSpace(const char* cursor) {
  while (*cursor == ' ' || *cursor == '\t') ++cursor;
  return cursor;
}

inline bool number(const char*& cursor, long& value) {
  cursor = skipSpace(cursor);
  if (!isdigit(static_cast<unsigned char>(*cursor))) return false;
  char* end = nullptr;
  value = strtol(cursor, &end, 10);
  cursor = skipSpace(end);
  return true;
}

inline int scalar(const char* reply, const char* prefix) {
  int result = -1;
  const char* cursor = reply;
  while ((cursor = strstr(cursor, prefix)) != nullptr) {
    cursor += strlen(prefix);
    long value = 0;
    if (number(cursor, value) && (*cursor == '\r' || *cursor == '\n')) {
      result = static_cast<int>(value);
    }
  }
  return result;
}

inline int first(const char* reply, const char* prefix) {
  int result = -1;
  const char* cursor = reply;
  while ((cursor = strstr(cursor, prefix)) != nullptr) {
    cursor += strlen(prefix);
    long value = 0;
    if (number(cursor, value) &&
        (*cursor == '\r' || *cursor == '\n' || *cursor == ',') &&
        strpbrk(cursor, "\r\n") != nullptr) result = value;
  }
  return result;
}

inline int pair(const char* reply, const char* prefix, int firstWanted) {
  int result = -1;
  const char* cursor = reply;
  while ((cursor = strstr(cursor, prefix)) != nullptr) {
    cursor += strlen(prefix);
    long first = 0;
    long second = 0;
    if (number(cursor, first) && *cursor == ',') {
      ++cursor;
      if (number(cursor, second) &&
          (*cursor == '\r' || *cursor == '\n' || *cursor == ',') &&
          strpbrk(cursor, "\r\n") != nullptr &&
          (firstWanted < 0 || first == firstWanted)) {
        result = static_cast<int>(second);
      }
    }
  }
  return result;
}

inline int registration(const char* reply) {
  // Prefer the last complete query-format line; ignore single-value and
  // location-bearing unsolicited notifications even when they arrive later.
  int result = -1;
  const char* cursor = reply;
  while ((cursor = strstr(cursor, "+CEREG:")) != nullptr) {
    cursor += 7;
    long mode = 0, status = 0;
    if (number(cursor, mode) && *cursor == ',') {
      ++cursor;
      if (number(cursor, status) && mode <= 2 && status <= 11 &&
          (*cursor == '\r' || *cursor == '\n' || *cursor == ',') &&
          strpbrk(cursor, "\r\n") != nullptr) result = status;
    }
  }
  return result;
}
}
