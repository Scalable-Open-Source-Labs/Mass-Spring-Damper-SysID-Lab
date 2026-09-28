#include "autocal.h"
#include "calibration.h"

static uint16_t lookedAt;                   // Travel at the last analysis
static uint16_t lastSuggest[NUM_CHANNELS];  // The last analysis's suggestion, if haveLast
static bool haveLast;
static uint8_t misplacedLooks;
static uint16_t cleanPeriods;  // Clean periods at the last analysis's suggestion

// Starts a new first-boot recording
void autocalBegin() {
  sweepBegin();
  lookedAt = 0;
  haveLast = false;
  misplacedLooks = 0;
  cleanPeriods = 0;
}

// Progress for the display, 0 to 3: clean periods at the last suggestion, in thirds of AUTOCAL_PERIODS
uint8_t autocalProgress() {
  return cleanPeriods >= AUTOCAL_PERIODS ? 3 : cleanPeriods * 3 / AUTOCAL_PERIODS;
}


// Call once per loop, after sweepAdd(). Analyses when there is new travel and the carriage is resting, or anyway once
// an analysis is well overdue: the recording marks the stall, and loses only what the carriage did during it. Fills
// result. channel names the weak channel for AUTOCAL_WEAK.
AutoCalOutcome autocalUpdate(uint32_t nowUs, SweepResult& r, uint8_t& channel) {
  uint16_t travel = sweepTravel();
  if (sweepFull() || travel >= AUTOCAL_GIVE_UP) return AUTOCAL_START_OVER;
  uint16_t due = lookedAt ? lookedAt + AUTOCAL_LOOK_EVERY : AUTOCAL_FIRST_LOOK;
  if (travel < due) return AUTOCAL_RECORDING;
  if (sweepIdleUs(nowUs) < AUTOCAL_REST_US && travel < due + AUTOCAL_OVERDUE) return AUTOCAL_RECORDING;
  lookedAt = travel;

  analyseSweep(r);

  // A weak channel, judged only once another channel shows a full swing, so a carriage jiggled across less than a
  // stripe isn't taken for a weak sensor
  int32_t widestSpan = 0;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    int32_t span = (int32_t)r.ch[ch].darkMv - r.ch[ch].lightMv;
    if (span > widestSpan) widestSpan = span;
  }
  bool haveSuggestion = true;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    const ChannelResult& c = r.ch[ch];
    bool weak = (int32_t)c.darkMv - c.lightMv < MIN_SPAN_MV || c.suggest == SUGGEST_NO_ROOM;
    if (weak && widestSpan >= 2 * MIN_SPAN_MV) {
      channel = ch;
      return AUTOCAL_WEAK;
    }
    haveSuggestion = haveSuggestion && c.suggestVref;
  }
  if (!haveSuggestion) return AUTOCAL_ANALYSED;

  // Placement: with plenty of clean travel at the suggestion, even the best opposite pair is too narrow. Edges that
  // cross at every threshold never give enough clean travel, and end in a start over instead.
  const Spacing& s = r.simSuggest;
  cleanPeriods = s.periods;
  bool enough = s.periods >= AUTOCAL_PERIODS && s.periodsEachWay[0] >= AUTOCAL_PERIODS_EACH_WAY &&
                s.periodsEachWay[1] >= AUTOCAL_PERIODS_EACH_WAY;
  bool placementFails = enough && s.bestWidth < MIN_STATE_WIDTH_MM;
  misplacedLooks = placementFails ? misplacedLooks + 1 : 0;
  if (misplacedLooks >= AUTOCAL_MISPLACED_LOOKS) return AUTOCAL_MISPLACED;

  // Close to the best the placement allows, unless a margin holds a channel back, which puts that best out of reach
  bool held = false;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) held = held || r.ch[ch].suggest != SUGGEST_OK;
  bool good = enough && !s.invalid && s.minWidth >= MIN_STATE_WIDTH_MM &&
              (held || s.minWidth >= s.bestWidth - AUTOCAL_NEAR_BEST_MM);
  bool settled = false;
  if (haveLast) {
    float lastWidth = sweepNarrowestAt(lastSuggest);  // Overwrites the simulation buffers, which r no longer needs
    settled = lastWidth >= 0 && fabsf(lastWidth - s.minWidth) <= AUTOCAL_SETTLED_MM;
  }
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) lastSuggest[ch] = r.ch[ch].suggestVref;
  haveLast = true;
  return good && settled ? AUTOCAL_SETTLED : AUTOCAL_ANALYSED;
}
