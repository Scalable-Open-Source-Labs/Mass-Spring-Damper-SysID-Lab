#include "gpio.h"
#include "calibration.h"

#define VREF_PWM_FREQ_HZ 40000  // ~1.5 mV ripple after the 10k/1uF filter. The core's 1 kHz default gives ~60 mV, most of the 100 mV comparator hysteresis.
#define VREF_PWM_RANGE 4095     // 12-bit duty, 0.8 mV per step
#define VREF_SETTLE_MS 100      // 10 time constants of the 10k/1uF filter
#define ADC_OVERSAMPLE 8

// Comparator hysteresis, referred to BUFF_x: 10k series (R11/R17/R22) with 330k feedback (R6/R13/R18) from an output
// pulled up to 3V3. The output switches HIGH as BUFF_x rises past Vref * 34/33, and back LOW 3300/33 = 100 mV below that.
#define COMPARATOR_HYST_MV 100

static const uint8_t pwmPins[NUM_CHANNELS] = { pwmA, pwmB, pwmC };
static const uint8_t buffPins[NUM_CHANNELS] = { buffA, buffB, buffC };
static uint16_t vrefMillivolts[NUM_CHANNELS];

void gpio_initialise() {
  pinMode(chA, INPUT);  // Comparator outputs have external 10k pull-ups. INPUT keeps the internal pull resistors disabled.
  pinMode(chB, INPUT);
  pinMode(chC, INPUT);

  pinMode(btnRec, INPUT_PULLUP);
  pinMode(ledRec, OUTPUT);

  analogReadResolution(12);  // The core defaults to 10-bit
  analogWriteFreq(VREF_PWM_FREQ_HZ);
  analogWriteRange(VREF_PWM_RANGE);
  uint16_t vrefs[NUM_CHANNELS] = { VREF_DEFAULT_MV, VREF_DEFAULT_MV, VREF_DEFAULT_MV };
  loadCalibration(vrefs);  // Replaces the defaults with this unit's saved thresholds, if it has been calibrated
  applyVrefs(vrefs);
}


// Set a comparator threshold. The filtered PWM voltage is duty * VDD.
void setVref(uint8_t channel, uint16_t millivolts) {
  vrefMillivolts[channel] = millivolts;
  analogWrite(pwmPins[channel], ((uint32_t)millivolts * VREF_PWM_RANGE + VDD_MV / 2) / VDD_MV);
}

// Set every comparator threshold, then wait for the RC filters to settle. Thresholds ramp from their previous level
// (0 V at power-up), and reading the encoder before they settle registers phantom counts.
void applyVrefs(const uint16_t millivolts[]) {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) setVref(ch, millivolts[ch]);
  delay(VREF_SETTLE_MS);
}

uint16_t getVref(uint8_t channel) {
  return vrefMillivolts[channel];
}

// Buffered sensor voltage, averaged to reduce ADC noise. Ratiometric with the thresholds: both are referenced to +3V3.
uint16_t readSensorMillivolts(uint8_t channel) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_OVERSAMPLE; i++) sum += analogRead(buffPins[channel]);
  return sum * VDD_MV / (4096UL * ADC_OVERSAMPLE);
}

// BUFF_x voltage at which the comparator output switches LOW -> HIGH
uint16_t comparatorRiseMillivolts(uint16_t vref) {
  return ((uint32_t)vref * 34 + 16) / 33;
}

// BUFF_x voltage at which the comparator output switches HIGH -> LOW
uint16_t comparatorFallMillivolts(uint16_t vref) {
  uint16_t rise = comparatorRiseMillivolts(vref);
  return rise > COMPARATOR_HYST_MV ? rise - COMPARATOR_HYST_MV : 0;
}

// Vref that centres the hysteresis band on the midpoint between a channel's light and dark levels
uint16_t vrefForMidpoint(uint16_t midMillivolts) {
  return ((uint32_t)(midMillivolts + COMPARATOR_HYST_MV / 2) * 33 + 17) / 34;
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
