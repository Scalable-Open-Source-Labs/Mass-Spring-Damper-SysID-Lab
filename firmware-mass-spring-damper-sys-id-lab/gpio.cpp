#include "gpio.h"
#include "calibration.h"

#define VREF_PWM_FREQ_HZ 40000  // ~1.5 mV ripple after the 10k/1uF filter. The core's 1 kHz default gives ~60 mV, most of the 100 mV comparator hysteresis.
#define VREF_PWM_RANGE 4095     // 12-bit duty, 0.8 mV per step
#define VREF_SETTLE_MS 100      // 10 time constants of the 10k/1uF filter
#define ADC_OVERSAMPLE 8

// Position of each encoder state in the Johnson sequence 000 100 110 111 011 001, or -1 for the two it never visits
static const int8_t JOHNSON_INDEX[8] = { 0, 5, -1, 4, 1, -1, 2, 3 };

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

// Nominal BUFF_x voltage at which the comparator output switches LOW -> HIGH. The HIGH -> LOW point is
// COMPARATOR_HYST_MV below it.
uint16_t comparatorRiseMillivolts(uint16_t vref) {
  return ((uint32_t)vref * 34 + 16) / 33;
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

// Signed steps along the Johnson sequence from prevState to currState: 0, +-1, +-2, or 3 when currState is the
// opposite state and the direction is unknown. JOHNSON_INVALID when either state is 010 or 101.
int8_t johnsonStep(uint8_t prevState, uint8_t currState) {
  int8_t from = JOHNSON_INDEX[prevState & 0b111], to = JOHNSON_INDEX[currState & 0b111];
  if (from < 0 || to < 0) return JOHNSON_INVALID;
  int8_t step = (to - from + 6) % 6;
  return step > 3 ? step - 6 : step;
}
