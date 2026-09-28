#ifndef SWEEP_H
#define SWEEP_H
#include <Arduino.h>
#include "gpio.h"

// A test-mode sweep: the sensor voltages and comparator states recorded while the carriage slides end to end, and how
// evenly they space the encoder's edges.
//
// The six valid states should each span 1 mm of the 6 mm strip period. The narrowest one is the robustness figure: at
// 0 mm two channels switch at the same place and a count can be lost. A channel's threshold moves its two edges in
// opposite directions, so thresholds can only trade width between opposite states (100/011, 110/001, 111/000). The
// narrowest state is therefore widest when every channel reads light for half of each period. On a sensor whose light
// level clips, that is not the midpoint of its light and dark levels, and when a margin stops a channel short of half,
// the others do better to give way. So the suggestion starts at half duty and then searches the simulated spacing.

#define SWEEP_MIN_PERIODS 8  // Full strip periods a figure needs before it's trusted
#define MIN_MARGIN_MV 200    // Closest a suggested switching point may come to the sweep's light or dark level

// Widths of the encoder states in mm of carriage travel
struct Spacing {
  float width[8];            // Median width of each valid state, indexed by state (010 and 101 unused). 0 if unmeasured
  float minWidth;            // Narrowest of the six
  float bestWidth;           // Narrowest the thresholds alone could reach: the smallest average of an opposite pair
  float duty[NUM_CHANNELS];  // Fraction of each period the channel reads light, NAN if no period was measured
  uint16_t periods;          // Full periods measured
  uint16_t invalid;          // State changes that weren't a single step
};

// Where a channel's switching points came from
enum SwitchSource : uint8_t {
  SWITCH_MEASURED,  // The comparator switched often enough both ways during the sweep
  SWITCH_BORROWED,  // The other channels' measured hysteresis, around whatever this one did
  SWITCH_NOMINAL,   // No channel switched enough: the resistor values
};

enum SuggestStatus : uint8_t {
  SUGGEST_OK,
  SUGGEST_LIGHT_BOUND,  // Light for over half of each period even at the lowest threshold the margins allow
  SUGGEST_DARK_BOUND,   // Light for under half even at the highest
  SUGGEST_NO_ROOM,      // Light and dark levels too close together for the margins
  SUGGEST_SHORT,        // Too few full periods to judge
};

struct ChannelResult {
  uint16_t lightMv, darkMv;  // Sweep minimum and maximum on BUFF_x
  uint16_t vref;             // Threshold in force when the sweep began
  int16_t riseMv, fallMv;    // BUFF_x where the comparator switches at that threshold
  uint16_t switchReadings;   // Switches behind riseMv/fallMv, the fewer of the two directions
  SwitchSource switchSource;
  float duty;                // Fraction of each period the channel read light, NAN with too few periods
  uint16_t halfVref;         // Threshold for half light and half dark (held at a margin bound if out of reach), 0 if none
  SuggestStatus suggest;     // How halfVref came out
  uint16_t suggestVref;      // Threshold to calibrate to: where the three together space the edges best. 0 if none
};

struct SweepResult {
  ChannelResult ch[NUM_CHANNELS];
  Spacing hw;          // From the comparators during the sweep
  Spacing simActive;   // Simulated at the thresholds in force
  Spacing simHalf;     // Simulated at the half-duty thresholds
  Spacing simSuggest;  // Simulated at the suggested thresholds. periods is 0 if a channel has no suggestion
};

// Function Prototypes
void sweepBegin(void);
void sweepAdd(uint32_t pinUs, uint8_t state, const uint32_t adcUs[], const uint16_t mv[]);
void analyseSweep(SweepResult& result);
void printSpacing(const char* label, const uint16_t vref[], const Spacing& spacing);
void printDuty(const char* key, float duty);
const char* switchSourceName(SwitchSource source);


#endif
