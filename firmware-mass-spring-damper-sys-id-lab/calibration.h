#ifndef CALIBRATION_H
#define CALIBRATION_H
#include <Arduino.h>

// Per-unit comparator thresholds, found in test mode and kept in emulated EEPROM (the last 4 KB flash sector, which
// UF2 flashing leaves alone). Units without a valid calibration run VREF_DEFAULT_MV on every channel.

#define MIN_SPAN_MV 500  // Refuse to calibrate a channel whose light and dark levels are closer than this

// Function Prototypes
bool loadCalibration(uint16_t vref_mV[]);
bool calibrateFromSweep(const uint16_t minMv[], const uint16_t maxMv[]);
void clearCalibration(void);
bool isCalibrated(void);
void printCalibration(void);


#endif
