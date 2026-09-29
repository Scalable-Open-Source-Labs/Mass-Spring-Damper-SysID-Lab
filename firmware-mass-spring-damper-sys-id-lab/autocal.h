#ifndef AUTOCAL_H
#define AUTOCAL_H
#include <Arduino.h>
#include "sweep.h"

// First-boot calibration. A unit that has never been calibrated boots straight into recording. The technician slides
// the carriage end to end, and the unit decides for itself when the recording is good enough, so there is nothing to
// time and nothing to press.
//
// It analyses when the carriage rests (analyseSweep() blocks the loop for a while), or anyway when an analysis is
// overdue, since the recording marks the stall and ignores what it hid. It stops once the
// suggestion has settled: enough clean periods both ways, a suggestion that one more stretch of sweeping didn't change,
// and a narrowest state close to what the unit's sensor placement allows. A weak or misplaced sensor ends it as a
// fault, since no thresholds can fix that. Motion too fast to measure doesn't count towards progress.

#define AUTOCAL_PERIODS 24          // Clean periods at the suggested thresholds before it may stop
#define AUTOCAL_PERIODS_EACH_WAY 8  // Of those, in each direction
#define AUTOCAL_FIRST_LOOK 12       // Periods of travel before the first analysis
#define AUTOCAL_LOOK_EVERY 4        // More periods of travel before each analysis after that
#define AUTOCAL_REST_US 300000      // No comparator change for this long means the carriage is resting
#define AUTOCAL_GIVE_UP 96          // Periods of travel without settling before the recording starts over
#define AUTOCAL_SETTLED_MM 0.02f    // The last suggestion, judged on the new data, within this of the new one
#define AUTOCAL_NEAR_BEST_MM 0.05f  // The narrowest state within this of the best the placement allows, when no margin binds
#define AUTOCAL_MISPLACED_LOOKS 2   // Analyses in a row that find the placement at fault before it is called
#define AUTOCAL_OVERDUE 8           // Periods of travel past a due analysis before it runs without a rest

enum AutoCalOutcome : uint8_t {
  AUTOCAL_RECORDING,  // Nothing new: keep sweeping
  AUTOCAL_ANALYSED,   // Analysed at a rest and not settled yet: keep sweeping
  AUTOCAL_SETTLED,    // The result holds a calibration to save
  AUTOCAL_WEAK,       // A channel's light and dark levels are too close (the channel is given)
  AUTOCAL_MISPLACED,  // The sensors' placement can't give every state MIN_STATE_WIDTH_MM, whatever the thresholds
  AUTOCAL_START_OVER, // Too much sweeping without settling, or the recording is full: begin again
};

// Function Prototypes
void autocalBegin(void);
AutoCalOutcome autocalUpdate(uint32_t nowUs, SweepResult& result, uint8_t& channel);
uint8_t autocalProgress(void);


#endif
