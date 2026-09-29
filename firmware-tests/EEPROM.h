#pragma once
#include <string.h>
#include <stdint.h>
class EEPROMClass {
 public:
  uint8_t data[4096] = {};
  int commits = 0;
  void begin(size_t) {}
  template <typename T> T& get(int addr, T& t) { memcpy(&t, data + addr, sizeof(T)); return t; }
  template <typename T> const T& put(int addr, const T& t) { memcpy(data + addr, &t, sizeof(T)); return t; }
  bool commit() { commits++; return true; }
};
extern EEPROMClass EEPROM;
