#ifndef CALIBRATION_H
#define CALIBRATION_H
#include <Arduino.h>
#include "sweep.h"

// Per-unit comparator thresholds, found in test mode and kept in emulated EEPROM (the last 4 KB flash sector, which
// UF2 flashing leaves alone). Units without a valid calibration run VREF_DEFAULT_MV on every channel.

#define MIN_SPAN_MV 500          // Refuse to calibrate a channel whose light and dark levels are closer than this
#define MIN_STATE_WIDTH_MM 0.5f  // Refuse thresholds that would leave any encoder state narrower than this (ideal: 1)

// Function Prototypes
bool loadCalibration(uint16_t vref_mV[]);
bool calibrateFromSweep(void);
bool calibrateFrom(const SweepResult& result);
bool calibrationBlank(void);
void clearCalibration(void);
bool isCalibrated(void);
void printCalibration(void);


#endif
