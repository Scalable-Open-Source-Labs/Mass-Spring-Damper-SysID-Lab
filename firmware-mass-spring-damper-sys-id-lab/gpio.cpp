#include "gpio.h"

#define VDD_MV 3300             // +3V3 rail: PWM high level (IOVDD) and ADC reference (ADC_AVDD)
#define VREF_PWM_FREQ_HZ 40000  // ~1.5 mV ripple after the 10k/1uF filter. The core's 1 kHz default gives ~60 mV, most of the 100 mV comparator hysteresis.
#define VREF_PWM_RANGE 4095     // 12-bit duty, 0.8 mV per step
#define VREF_SETTLE_MS 100      // 10 time constants of the 10k/1uF filter

static const uint8_t pwmPins[NUM_CHANNELS] = { pwmA, pwmB, pwmC };
static uint16_t vrefMillivolts[NUM_CHANNELS];

void gpio_initialise() {
  pinMode(chA, INPUT);  // Comparator outputs have external 10k pull-ups. INPUT keeps the internal pull resistors disabled.
  pinMode(chB, INPUT);
  pinMode(chC, INPUT);

  pinMode(btnRec, INPUT_PULLUP);
  pinMode(ledRec, OUTPUT);

  analogWriteFreq(VREF_PWM_FREQ_HZ);
  analogWriteRange(VREF_PWM_RANGE);
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) setVref(ch, VREF_DEFAULT_MV);
  delay(VREF_SETTLE_MS);  // Thresholds ramp up from 0 V. Reading the encoder before they settle registers phantom counts.
}


// Set a comparator threshold. The filtered PWM voltage is duty * VDD.
void setVref(uint8_t channel, uint16_t millivolts) {
  vrefMillivolts[channel] = millivolts;
  analogWrite(pwmPins[channel], ((uint32_t)millivolts * VREF_PWM_RANGE + VDD_MV / 2) / VDD_MV);
}

uint16_t getVref(uint8_t channel) {
  return vrefMillivolts[channel];
}


// Read current encoder state (3-bit value)
uint8_t readEncoderState() {
  uint8_t state = 0;

  // Read pins and construct 3-bit state (A=bit2, B=bit1, C=bit0)
  if (!digitalRead(chA)) state |= 0b100;  // PHOTO_x is LOW when its sensor sees a reflective surface
  if (!digitalRead(chB)) state |= 0b010;
  if (!digitalRead(chC)) state |= 0b001;

  return state;
}
