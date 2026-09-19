#pragma once
// Host-only UART/time/String substitutes. The production modem state machine
// is compiled unchanged; no physical hardware or network requests are used.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <vector>

class String {
public:
  std::string value;
  String() = default;
  String(const char* text) : value(text ? text : "") {}
  String(std::string text) : value(std::move(text)) {}
  String(char character) : value(1, character) {}
  template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  String(T number) : value(std::to_string(number)) {}
  String(double number, unsigned int decimals) {
    char text[80];
    snprintf(text, sizeof(text), "%.*f", static_cast<int>(decimals), number);
    value = text;
  }
  const char* c_str() const { return value.c_str(); }
  unsigned int length() const { return value.size(); }
  void reserve(unsigned int capacity) { value.reserve(capacity); }
  void remove(unsigned int start, size_t count = std::string::npos) {
    if (start < value.size()) value.erase(start, count);
  }
  int indexOf(const String& text, unsigned int start = 0) const {
    auto position = value.find(text.value, start);
    return position == std::string::npos ? -1 : static_cast<int>(position);
  }
  int indexOf(char character, unsigned int start = 0) const {
    auto position = value.find(character, start);
    return position == std::string::npos ? -1 : static_cast<int>(position);
  }
  bool startsWith(const String& text) const { return value.rfind(text.value, 0) == 0; }
  bool endsWith(const String& text) const {
    return value.size() >= text.value.size() &&
        value.compare(value.size() - text.value.size(), text.value.size(), text.value) == 0;
  }
  String substring(size_t start, size_t end = std::string::npos) const {
    if (start > end) std::swap(start, end);
    if (start >= value.size()) return "";
    return value.substr(start, end == std::string::npos ? end : end - start);
  }
  long toInt() const { return strtol(c_str(), nullptr, 10); }
  float toFloat() const { return strtof(c_str(), nullptr); }
  double toDouble() const { return strtod(c_str(), nullptr); }
  void trim() {
    auto first = value.find_first_not_of(" \t\r\n");
    auto last = value.find_last_not_of(" \t\r\n");
    value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
  }
  void replace(const String& oldText, const String& newText) {
    size_t position = 0;
    while ((position = value.find(oldText.value, position)) != std::string::npos) {
      value.replace(position, oldText.length(), newText.value);
      position += newText.length();
    }
  }
  char operator[](unsigned int index) const { return value[index]; }
  String& operator+=(const String& other) { value += other.value; return *this; }
  friend String operator+(const String& left, const String& right) {
    return left.value + right.value;
  }
  friend bool operator==(const String& left, const String& right) { return left.value == right.value; }
  friend bool operator!=(const String& left, const String& right) { return !(left == right); }
};

inline uint32_t fakeMillis = 0;
inline uint32_t millis() { return fakeMillis; }
constexpr int INPUT_PULLUP = 2;
constexpr int INPUT = 0;
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int HEX = 16;
inline int digitalRead(int) { return LOW; }
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define F(x) x
struct FakeConsole { void println(const String&) {} };
inline FakeConsole Serial;
constexpr int SERIAL_8N1 = 0;
inline void pinMode(int, int) {}
class HardwareSerial {
public:
  static inline std::deque<char> input;
  static inline std::string output;
  explicit HardwareSerial(uint8_t) {}
  void begin(uint32_t, int, int8_t, int8_t) {}
  int available() const { return input.size(); }
  int read() { char result = input.front(); input.pop_front(); return result; }
  void print(const String& text) { output += text.value; }
  static void reply(const std::string& text) { for (char c : text) input.push_back(c); }
};
