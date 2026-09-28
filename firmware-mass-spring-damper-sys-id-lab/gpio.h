#ifndef GPIO_H
#define GPIO_H
#include <Arduino.h>

// GPIO Pin Definitions (main-board rev 2.0)

// Optical encoder digital inputs: comparator outputs, net PHOTO_x.
// GPIO0/1 are also the default UART0 pins, so never set Tools > Debug Port to Serial1.
#define chA 1
#define chB 2
#define chC 0

// Comparator thresholds: PWM -> 10k/1uF RC filter -> V_refx
#define pwmA 12
#define pwmB 13
#define pwmC 14

// Buffered sensor voltages: net BUFF_x via 220R. The ADC channel order differs from A/B/C.
#define buffA 27  // ADC1
#define buffB 28  // ADC2
#define buffC 26  // ADC0

#define btnRec 21
#define ledRec 24  // Not connected on rev 2.0

#define sevenSegCLK 18
#define sevenSegDAT 19
#define sevenSegCS 20

// Optical encoder channels, for indexing per-channel settings
enum channels {
  CH_A,
  CH_B,
  CH_C,
  NUM_CHANNELS
};

#define VDD_MV 3300          // +3V3 rail: PWM high level (IOVDD) and ADC reference (ADC_AVDD)
#define VREF_DEFAULT_MV 780  // Comparator threshold applied to every channel at startup

// Nominal comparator switching points, referred to BUFF_x: 10k series (R11/R17/R22) with 330k feedback (R6/R13/R18)
// from an output pulled up to 3V3. The output switches HIGH as BUFF_x rises past Vref * 34/33
// (comparatorRiseMillivolts), and back LOW 3300/33 = 100 mV below that. Test mode measures the real points on every
// sweep (sweep.cpp). These give the 34/33 slope, and are the fallback when a channel didn't switch.
#define COMPARATOR_HYST_MV 100

// Each channel's bit in the encoder state, as readEncoderState() packs it: A is bit 2, C is bit 0. A set bit means the
// sensor sees a reflective (light) stripe.
#define CHANNEL_BIT(ch) (0b100 >> (ch))

// johnsonStep() result when either state is 010 or 101, which the Johnson sequence never visits
#define JOHNSON_INVALID 127

// Function Prototypes
void gpio_initialise(void);
uint8_t readEncoderState(void);
int8_t johnsonStep(uint8_t prevState, uint8_t currState);
void setVref(uint8_t channel, uint16_t millivolts);
void applyVrefs(const uint16_t millivolts[]);
uint16_t getVref(uint8_t channel);
uint16_t readSensorMillivolts(uint8_t channel);
uint16_t comparatorRiseMillivolts(uint16_t vref);




#endif