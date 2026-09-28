// Host tests for the encoder decoder: drives the sketch's real processEncoderChange(), which run.sh copies out of the
// .ino, through the pins it reads
#include "Arduino.h"
#include "EEPROM.h"
#include "gpio.h"
#include <random>
SerialStub Serial;
EEPROMClass EEPROM;
static int pinLevel[32];
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) { return pinLevel[pin]; }
void analogWrite(uint8_t, int) {}
void analogWriteFreq(uint32_t) {}
void analogWriteRange(uint32_t) {}
void analogReadResolution(int) {}
int analogRead(uint8_t) { return 0; }
void delay(unsigned long) {}
uint32_t micros() { return 0; }
uint32_t millis() { return 0; }
void noInterrupts() {}
void interrupts() {}
bool loadCalibration(uint16_t[]) { return false; }

// The sketch's globals the decoder uses
volatile int positionCounter = 0;
uint8_t currentState = 0;
uint8_t previousState = 0;
unsigned int recoveredSkips = 0;
unsigned int lostCounts = 0;
#include "decoder_extracted.inc"

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void show(uint8_t state) {  // Set the pins: a set bit means that comparator output is LOW
  pinLevel[chA] = !(state & 0b100);
  pinLevel[chB] = !(state & 0b010);
  pinLevel[chC] = !(state & 0b001);
  processEncoderChange();
}
static void reset(uint8_t state) {
  show(state);
  positionCounter = 0;
  recoveredSkips = lostCounts = 0;
}
static void run(const char* what, uint8_t start, std::initializer_list<uint8_t> seq, int position, unsigned recovered, unsigned lost) {
  reset(start);
  for (uint8_t s : seq) show(s);
  CHECK(positionCounter == position && recoveredSkips == recovered && lostCounts == lost,
        "%s: position=%d recovered=%u lost=%u (want %d %u %u)", what, positionCounter, recoveredSkips, lostCounts,
        position, recovered, lost);
}

int main() {
  const uint8_t J[6] = { 0b000, 0b100, 0b110, 0b111, 0b011, 0b001 };
  run("forward two periods", 0, { 4, 6, 7, 3, 1, 0, 4, 6, 7, 3, 1, 0 }, 12, 0, 0);
  run("backward two periods", 0, { 1, 3, 7, 6, 4, 0, 1, 3, 7, 6, 4, 0 }, -12, 0, 0);
  run("every other state missed", 0, { 6, 3, 0, 6, 3, 0 }, 12, 6, 0);
  run("C and A crossed, forward", 1, { 5, 4 }, 2, 1, 0);
  run("C and A crossed, backward", 4, { 5, 1 }, -2, 1, 0);
  run("A and C crossed at 111, forward", 6, { 2, 3 }, 2, 1, 0);
  run("B and C crossed, forward", 0b100, { 0b101, 0b111 }, 2, 1, 0);
  run("chatter on a crossing", 1, { 5, 1, 5, 1, 5, 4 }, 2, 1, 0);
  run("opposite state is lost", 0, { 7 }, 0, 0, 1);
  run("counting resumes after a loss", 0, { 7, 3, 1 }, 2, 0, 1);
  run("back and forth", 0, { 4, 0, 4, 6, 4, 0, 1 }, -1, 0, 0);

  // Random walks with skipped states and crossed pairs: the count must follow the true position
  std::mt19937 rng(7);
  for (int trial = 0; trial < 2000; trial++) {
    int pos = 0;
    reset(J[0]);
    for (int i = 0; i < 400; i++) {
      int r = rng() % 100, step = r < 45 ? 1 : r < 90 ? -1 : r < 95 ? 2 : -2;
      int from = ((pos % 6) + 6) % 6;
      pos += step;
      int to = ((pos % 6) + 6) % 6;
      if ((step == 2 || step == -2) && rng() % 2) {
        // The two edges cross: pass through the invalid code between from and to
        int mid = ((from + step / 2) % 6 + 6) % 6;
        uint8_t crossed = J[from] ^ J[to] ^ J[mid];
        show(crossed);
      }
      show(J[to]);
    }
    if (positionCounter != pos || lostCounts) {
      CHECK(false, "random walk %d: counted %d, true %d, lost %u", trial, positionCounter, pos, lostCounts);
      break;
    }
    checks++;
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
