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

// Function Prototypes
void gpio_initialise(void);
uint8_t readEncoderState(void);
void setVref(uint8_t channel, uint16_t millivolts);
void applyVrefs(const uint16_t millivolts[]);
uint16_t getVref(uint8_t channel);
uint16_t readSensorMillivolts(uint8_t channel);
uint16_t comparatorRiseMillivolts(uint16_t vref);
uint16_t comparatorFallMillivolts(uint16_t vref);
uint16_t vrefForMidpoint(uint16_t midMillivolts);




#endif