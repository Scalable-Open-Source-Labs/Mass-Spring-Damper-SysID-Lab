// Host stand-ins for the Arduino API the firmware modules use
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <math.h>
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
class SerialStub {
 public:
  bool quiet = false;
  char last[512] = "";  // The latest printf's text, for checks on the output
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list a; va_start(a, fmt); int n = vsnprintf(last, sizeof last, fmt, a); va_end(a);
    if (!quiet) fputs(last, stdout);
    return n;
  }
  void println(const char* s) { if (!quiet) puts(s); }
  void println() { if (!quiet) putchar('\n'); }
  void print(const char* s) { if (!quiet) fputs(s, stdout); }
};
extern SerialStub Serial;
void pinMode(uint8_t, uint8_t);
int digitalRead(uint8_t);
void analogWrite(uint8_t, int);
void analogWriteFreq(uint32_t);
void analogWriteRange(uint32_t);
void analogReadResolution(int);
int analogRead(uint8_t);
void delay(unsigned long);
uint32_t micros();
uint32_t millis();
void noInterrupts();
void interrupts();
