#include "sweep.h"
#include <math.h>

#define SWEEP_SAMPLES 4096     // Sensor readings kept. When full, every other one goes and the stride doubles.
#define SWEEP_CHANGES 1024     // Encoder state changes kept, from the comparators or a simulation
#define SWITCH_READINGS 128    // Switching voltages kept per channel and direction
#define MIN_SWITCH_READINGS 4  // Per direction, before a channel's own switching points are used
#define PERIOD_MM 6.0f         // Strip pitch: one full cycle of the six states
#define WIDEN_MIN_MM 0.01f     // Least gain in the narrowest state that moves a suggested threshold

// The six valid states in Johnson order, one step apart
static const uint8_t JOHNSON_ORDER[6] = { 0b100, 0b110, 0b111, 0b011, 0b001, 0b000 };

// The recording. Times are in us since it began.
static uint32_t sampleUs[SWEEP_SAMPLES];
static uint16_t sampleMv[SWEEP_SAMPLES][NUM_CHANNELS];
static uint16_t sampleCount;
static uint32_t sampleStride, strideCount;  // Loops per kept reading

static uint32_t hwUs[SWEEP_CHANGES];  // Comparator state changes: when each state was first read
static uint8_t hwState[SWEEP_CHANGES];
static uint16_t hwCount;

// BUFF_x each time a comparator switched, as offsets from the nominal rise point at the threshold then in force
static int16_t riseReadings[NUM_CHANNELS][SWITCH_READINGS], fallReadings[NUM_CHANNELS][SWITCH_READINGS];
static uint8_t riseCount[NUM_CHANNELS], fallCount[NUM_CHANNELS];

static uint16_t minMv[NUM_CHANNELS], maxMv[NUM_CHANNELS];
static uint16_t sweepVref[NUM_CHANNELS];  // Thresholds in force when the sweep began

static bool started;
static uint32_t startUs, lastPinUs, lastAdcUs[NUM_CHANNELS];
static uint16_t lastMv[NUM_CHANNELS];
static uint8_t lastState;

// Working space for the analysis
static int16_t riseOffset[NUM_CHANNELS], fallOffset[NUM_CHANNELS];  // Switching points in use, offset as above
static int32_t marginLo[NUM_CHANNELS], marginHi[NUM_CHANNELS];  // Thresholds that keep MIN_MARGIN_MV to the levels
static uint32_t simUs[SWEEP_CHANGES];  // Simulated state changes, as for the hardware
static uint8_t simState[SWEEP_CHANGES];
static uint16_t referenceChanges;            // In simUs/simState while they hold the reference for windowDuty()
static uint16_t windowStart[SWEEP_CHANGES];  // Reference changes that begin a window of steady travel
static uint16_t windowCount;
static uint32_t litAt[SWEEP_CHANGES];  // Light time of the channel being judged, at each reference change
static float stateWidths[8][SWEEP_CHANGES / 6 + 2];
static float dutySamples[SWEEP_CHANGES];


static int compareFloats(const void* a, const void* b) {
  float x = *(const float*)a, y = *(const float*)b;
  return (x > y) - (x < y);
}

static int compareInt16s(const void* a, const void* b) {
  return *(const int16_t*)a - *(const int16_t*)b;
}

// Median of n values, which get reordered
static float medianOf(float v[], uint16_t n) {
  qsort(v, n, sizeof v[0], compareFloats);
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

static int16_t medianOf(int16_t v[], uint8_t n) {
  qsort(v, n, sizeof v[0], compareInt16s);
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}


// Starts a new recording. The thresholds in force now are the ones the sweep measures.
void sweepBegin() {
  sampleCount = 0;
  sampleStride = 1;
  strideCount = 0;
  hwCount = 0;
  started = false;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    riseCount[ch] = fallCount[ch] = 0;
    minMv[ch] = UINT16_MAX;
    maxMv[ch] = 0;
    sweepVref[ch] = getVref(ch);
  }
}

// Records BUFF_x at the moment a comparator switched. The switch fell somewhere between the last two pin reads, so
// take the midpoint and interpolate the two ADC readings to it. Those readings trail their pin reads, and ignoring that
// would widen the measured hysteresis with sweep speed.
static void recordSwitch(uint8_t ch, bool wentLight, uint32_t pinUs, const uint32_t adcUs[], const uint16_t mv[]) {
  uint32_t switchUs = lastPinUs + (pinUs - lastPinUs) / 2;
  int32_t readingsApartUs = (int32_t)(adcUs[ch] - lastAdcUs[ch]);
  float atSwitch = lastMv[ch];
  if (readingsApartUs > 0)
    atSwitch += (float)((int32_t)mv[ch] - lastMv[ch]) * (int32_t)(switchUs - lastAdcUs[ch]) / readingsApartUs;
  int16_t offset = (int16_t)(lroundf(atSwitch) - comparatorRiseMillivolts(getVref(ch)));
  if (wentLight) {
    if (fallCount[ch] < SWITCH_READINGS) fallReadings[ch][fallCount[ch]++] = offset;
  } else if (riseCount[ch] < SWITCH_READINGS) {
    riseReadings[ch][riseCount[ch]++] = offset;
  }
}

// Adds one test-mode loop to the recording: the encoder state read at pinUs, then each channel's sensor voltage, read
// with its midpoint at adcUs
void sweepAdd(uint32_t pinUs, uint8_t state, const uint32_t adcUs[], const uint16_t mv[]) {
  if (!started) {
    started = true;
    startUs = pinUs;
    hwUs[0] = 0;
    hwState[0] = state;
    hwCount = 1;
  } else if (state != lastState) {
    if (hwCount < SWEEP_CHANGES) {
      hwUs[hwCount] = pinUs - startUs;
      hwState[hwCount++] = state;
    }
    uint8_t changed = state ^ lastState;
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      if (changed & CHANNEL_BIT(ch)) recordSwitch(ch, state & CHANNEL_BIT(ch), pinUs, adcUs, mv);
    }
  }

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    if (mv[ch] < minMv[ch]) minMv[ch] = mv[ch];
    if (mv[ch] > maxMv[ch]) maxMv[ch] = mv[ch];
  }

  if (++strideCount >= sampleStride) {
    strideCount = 0;
    if (sampleCount == SWEEP_SAMPLES) {  // Full: keep every other reading, and take half as many from now on
      for (uint16_t i = 0; i < SWEEP_SAMPLES / 2; i++) {
        sampleUs[i] = sampleUs[2 * i];
        memcpy(sampleMv[i], sampleMv[2 * i], sizeof sampleMv[i]);
      }
      sampleCount = SWEEP_SAMPLES / 2;
      sampleStride *= 2;
    }
    sampleUs[sampleCount] = pinUs - startUs;
    memcpy(sampleMv[sampleCount++], mv, sizeof sampleMv[0]);
  }

  lastPinUs = pinUs;
  memcpy(lastAdcUs, adcUs, sizeof lastAdcUs);
  memcpy(lastMv, mv, sizeof lastMv);
  lastState = state;
}


// Settles each channel's switching points: its own where it switched often enough both ways, otherwise the other
// channels' measured hysteresis (or the nominal one) placed around whatever this channel did
static void resolveSwitchPoints(SweepResult& r) {
  bool measured[NUM_CHANNELS];
  int32_t bandSum = 0;
  uint8_t bands = 0;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    measured[ch] = riseCount[ch] >= MIN_SWITCH_READINGS && fallCount[ch] >= MIN_SWITCH_READINGS;
    if (!measured[ch]) continue;
    riseOffset[ch] = medianOf(riseReadings[ch], riseCount[ch]);
    fallOffset[ch] = medianOf(fallReadings[ch], fallCount[ch]);
    bandSum += riseOffset[ch] - fallOffset[ch];
    bands++;
  }
  int16_t band = bands ? bandSum / bands : COMPARATOR_HYST_MV;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    r.ch[ch].switchReadings = riseCount[ch] < fallCount[ch] ? riseCount[ch] : fallCount[ch];
    if (measured[ch]) {
      r.ch[ch].switchSource = SWITCH_MEASURED;
      continue;
    }
    r.ch[ch].switchSource = bands ? SWITCH_BORROWED : SWITCH_NOMINAL;
    if (riseCount[ch] >= MIN_SWITCH_READINGS) {
      riseOffset[ch] = medianOf(riseReadings[ch], riseCount[ch]);
      fallOffset[ch] = riseOffset[ch] - band;
    } else if (fallCount[ch] >= MIN_SWITCH_READINGS) {
      fallOffset[ch] = medianOf(fallReadings[ch], fallCount[ch]);
      riseOffset[ch] = fallOffset[ch] + band;
    } else {  // Centred where the nominal band is
      riseOffset[ch] = (band - COMPARATOR_HYST_MV) / 2;
      fallOffset[ch] = riseOffset[ch] - band;
    }
  }
}

// BUFF_x at which a channel's comparator switches LOW -> HIGH (going dark) at this threshold
static int32_t riseAt(uint8_t ch, uint16_t vref) {
  return (int32_t)comparatorRiseMillivolts(vref) + riseOffset[ch];
}

// BUFF_x at which it switches HIGH -> LOW (going light)
static int32_t fallAt(uint8_t ch, uint16_t vref) {
  return (int32_t)comparatorRiseMillivolts(vref) + fallOffset[ch];
}


// When the recorded voltage crossed mv, between readings i - 1 and i
static uint32_t crossingUs(uint16_t i, uint8_t ch, int32_t mv) {
  int32_t before = sampleMv[i - 1][ch], after = sampleMv[i][ch];
  return sampleUs[i - 1] + (uint32_t)((float)(sampleUs[i] - sampleUs[i - 1]) * (before - mv) / (before - after));
}

// Moves a simulated comparator on to reading i. Returns true, with the time in when, if it switched.
static bool comparatorStep(bool& light, uint16_t i, uint8_t ch, int32_t riseMv, int32_t fallMv, uint32_t& when) {
  int32_t mv = sampleMv[i][ch];
  if (light ? mv <= riseMv : mv >= fallMv) return false;
  when = crossingUs(i, ch, light ? riseMv : fallMv);
  light = !light;
  return true;
}

// The encoder states the comparators would pass through at these thresholds, like the hardware log. Returns how many.
static uint16_t simulateStates(const uint16_t vref[]) {
  int32_t riseMv[NUM_CHANNELS], fallMv[NUM_CHANNELS];
  bool light[NUM_CHANNELS];
  uint8_t state = 0;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    riseMv[ch] = riseAt(ch, vref[ch]);
    fallMv[ch] = fallAt(ch, vref[ch]);
    light[ch] = sampleMv[0][ch] < (riseMv[ch] + fallMv[ch]) / 2;
    if (light[ch]) state |= CHANNEL_BIT(ch);
  }
  simUs[0] = sampleUs[0];
  simState[0] = state;
  uint16_t n = 1;

  for (uint16_t i = 1; i < sampleCount; i++) {
    uint32_t when[NUM_CHANNELS];
    uint8_t which[NUM_CHANNELS], switched = 0;
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      if (comparatorStep(light[ch], i, ch, riseMv[ch], fallMv[ch], when[switched])) which[switched++] = ch;
    }
    for (uint8_t a = 1; a < switched; a++) {  // Two or three switches between readings: take them in time order
      for (uint8_t b = a; b > 0 && when[b] < when[b - 1]; b--) {
        uint32_t t = when[b];
        when[b] = when[b - 1];
        when[b - 1] = t;
        uint8_t ch = which[b];
        which[b] = which[b - 1];
        which[b - 1] = ch;
      }
    }
    for (uint8_t k = 0; k < switched; k++) {
      if (n == SWEEP_CHANGES) return n;
      state ^= CHANNEL_BIT(which[k]);
      simUs[n] = when[k];
      simState[n++] = state;
    }
  }
  return n;
}

// Median fraction of each period a channel reads light at this threshold, judged from its own dwells: each light dwell
// against the mean of the dark dwells either side, which cancels a steady change of speed. A slow hand wobbles, and the
// channel then bounces across its own edge, which this can't tell from a stripe. So it only seeds the reference for
// windowDuty(). NAN with fewer than SWEEP_MIN_PERIODS to judge.
static float ownDuty(uint8_t ch, uint16_t vref) {
  int32_t riseMv = riseAt(ch, vref), fallMv = fallAt(ch, vref);
  bool light = sampleMv[0][ch] < (riseMv + fallMv) / 2;
  uint32_t edgeUs[4];  // The last four switches, oldest first
  uint8_t edges = 0;
  uint16_t n = 0;
  for (uint16_t i = 1; i < sampleCount; i++) {
    uint32_t when;
    if (!comparatorStep(light, i, ch, riseMv, fallMv, when)) continue;
    if (edges == 4) {
      memmove(edgeUs, edgeUs + 1, 3 * sizeof edgeUs[0]);
      edges = 3;
    }
    edgeUs[edges++] = when;
    if (edges == 4 && light && n < SWEEP_CHANGES) {  // Dark, light, dark, now light again
      float darkBefore = edgeUs[1] - edgeUs[0], lit = edgeUs[2] - edgeUs[1], darkAfter = edgeUs[3] - edgeUs[2];
      dutySamples[n++] = lit / (lit + (darkBefore + darkAfter) / 2);
    }
  }
  return n >= SWEEP_MIN_PERIODS ? medianOf(dutySamples, n) : NAN;
}

// Finds the windows of steady travel in the reference simulation (the states just simulated): runs of seven single
// steps the same way, so that simUs[k] to simUs[k + 6] is exactly one strip period
static void findWindows(uint16_t changes) {
  referenceChanges = changes;
  windowCount = 0;
  for (uint16_t k = 1; k + 6 < changes; k++) {
    int8_t direction = johnsonStep(simState[k - 1], simState[k]);
    bool clean = direction == 1 || direction == -1;
    for (uint16_t j = k + 1; clean && j <= k + 6; j++) clean = johnsonStep(simState[j - 1], simState[j]) == direction;
    if (clean) windowStart[windowCount++] = k;
  }
}

// Median fraction of each reference window a channel spends light at this threshold. A window is one full period of
// steady travel, so it holds exactly one light interval whatever the channel's phase, and a bounce or a reversal never
// makes one. NAN with fewer than SWEEP_MIN_PERIODS periods of windows.
static float windowDuty(uint8_t ch, uint16_t vref) {
  if (windowCount < 6 * SWEEP_MIN_PERIODS) return NAN;
  int32_t riseMv = riseAt(ch, vref), fallMv = fallAt(ch, vref);
  bool light = sampleMv[0][ch] < (riseMv + fallMv) / 2;

  // The channel's light time since the recording began, at each reference change
  uint32_t lit = 0, t = sampleUs[0];
  uint16_t k = 0;
  for (uint16_t i = 1; i < sampleCount; i++) {
    uint32_t when;
    bool wasLight = light;
    if (!comparatorStep(light, i, ch, riseMv, fallMv, when)) continue;
    for (; k < referenceChanges && simUs[k] <= when; k++) {
      if (wasLight) lit += simUs[k] - t;
      t = simUs[k];
      litAt[k] = lit;
    }
    if (wasLight) lit += when - t;
    t = when;
  }
  for (; k < referenceChanges; k++) {
    if (light) lit += simUs[k] - t;
    t = simUs[k];
    litAt[k] = lit;
  }

  for (uint16_t w = 0; w < windowCount; w++) {
    uint16_t start = windowStart[w];
    dutySamples[w] = (float)(litAt[start + 6] - litAt[start]) / (simUs[start + 6] - simUs[start]);
  }
  return medianOf(dutySamples, windowCount);
}

// Lowest threshold whose nominal rise point reaches mv
static int32_t vrefForRise(int32_t mv) {
  int32_t vref = mv * 33 / 34;
  if (vref < 0) vref = 0;
  while (vref > 0 && comparatorRiseMillivolts(vref - 1) >= mv) vref--;
  while (vref < VDD_MV && comparatorRiseMillivolts(vref) < mv) vref++;
  return vref;
}

// The threshold at which a channel reads light for half of each period, by the given measure of duty, keeping both
// switching points MIN_MARGIN_MV inside the sweep's light and dark levels. Duty rises with the threshold, so bisect.
static void findHalfDuty(uint8_t ch, ChannelResult& c, float (*dutyAt)(uint8_t ch, uint16_t vref)) {
  c.halfVref = 0;
  int32_t lo = marginLo[ch] = vrefForRise(minMv[ch] + MIN_MARGIN_MV - fallOffset[ch]);
  int32_t hi = marginHi[ch] = vrefForRise(maxMv[ch] - MIN_MARGIN_MV - riseOffset[ch] + 1) - 1;
  if (maxMv[ch] < minMv[ch] || lo > hi) {
    c.suggest = SUGGEST_NO_ROOM;
    return;
  }
  float dutyLo = dutyAt(ch, lo), dutyHi = dutyAt(ch, hi);
  if (isnan(dutyLo) || isnan(dutyHi)) {
    c.suggest = SUGGEST_SHORT;
    return;
  }
  if (dutyLo >= 0.5f || dutyHi <= 0.5f) {
    c.halfVref = dutyLo >= 0.5f ? lo : hi;
    c.suggest = dutyLo >= 0.5f ? SUGGEST_LIGHT_BOUND : SUGGEST_DARK_BOUND;
    return;
  }
  while (hi - lo > 1) {
    int32_t mid = (lo + hi) / 2;
    float duty = dutyAt(ch, mid);
    if (isnan(duty)) {
      c.suggest = SUGGEST_SHORT;
      return;
    }
    if (duty < 0.5f) {
      lo = mid;
      dutyLo = duty;
    } else {
      hi = mid;
      dutyHi = duty;
    }
  }
  c.halfVref = 0.5f - dutyLo < dutyHi - 0.5f ? lo : hi;
  c.suggest = SUGGEST_OK;
}


// Widths of the encoder states from a list of state changes (us[k] is when state[k] began). A dwell counts only when
// the seven changes around it are single steps the same way, so that it sits inside one exact strip period. Its width
// is its share of that period's time, which cancels hand speed. Reversals, pauses at the ends and skipped states break
// the run and drop out.
static void analyseStates(const uint32_t us[], const uint8_t state[], uint16_t n, Spacing& s) {
  memset(&s, 0, sizeof s);
  uint16_t counts[8] = {};
  uint16_t dwells = 0;
  for (uint16_t k = 1; k < n; k++) {
    int8_t step = johnsonStep(state[k - 1], state[k]);
    if (step != 1 && step != -1) s.invalid++;
  }
  for (uint16_t k = 3; k + 4 < n; k++) {  // Dwell k is the third of the six dwells from us[k - 2] to us[k + 4]
    int8_t direction = johnsonStep(state[k - 3], state[k - 2]);
    bool clean = direction == 1 || direction == -1;
    for (uint16_t j = k - 1; clean && j <= k + 4; j++) clean = johnsonStep(state[j - 1], state[j]) == direction;
    if (!clean) continue;
    uint8_t st = state[k];
    if (counts[st] < sizeof stateWidths[0] / sizeof stateWidths[0][0]) {
      stateWidths[st][counts[st]++] = PERIOD_MM * (us[k + 1] - us[k]) / (float)(us[k + 4] - us[k - 2]);
      dwells++;
    }
  }
  s.periods = dwells / 6;

  float total = 0;
  s.minWidth = INFINITY;
  for (uint8_t st : JOHNSON_ORDER) {
    s.width[st] = counts[st] ? medianOf(stateWidths[st], counts[st]) : 0;
    total += s.width[st];
    if (s.width[st] < s.minWidth) s.minWidth = s.width[st];
  }
  s.bestWidth = INFINITY;
  for (uint8_t k = 0; k < 3; k++) {  // Opposite states are complements: 100/011, 110/001, 111/000
    uint8_t st = JOHNSON_ORDER[k];
    float pair = (s.width[st] + s.width[st ^ 0b111]) / 2;
    if (pair < s.bestWidth) s.bestWidth = pair;
  }
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    float lit = 0;
    for (uint8_t st : JOHNSON_ORDER) {
      if (st & CHANNEL_BIT(ch)) lit += s.width[st];
    }
    s.duty[ch] = total > 0 ? lit / total : NAN;
  }
}


// The narrowest state the comparators would leave at these thresholds over the recorded sweep, or -1 if they wouldn't
// decode it cleanly or it has too few periods to judge
static float narrowestAt(const uint16_t vref[]) {
  Spacing s;
  analyseStates(simUs, simState, simulateStates(vref), s);
  return s.invalid || s.periods < SWEEP_MIN_PERIODS ? -1 : s.minWidth;
}

// A threshold kept within the channel's margins
static uint16_t withinMargins(uint8_t ch, int32_t vref) {
  return vref < marginLo[ch] ? marginLo[ch] : vref > marginHi[ch] ? marginHi[ch] : vref;
}

// Half duty on every channel spaces the edges best only when every channel can reach it. When a margin holds one wide,
// its neighbours do better to give way: C a little under half, say, so that 000 (C going dark to A going light) gets
// its room back. So step each threshold within its margins while the simulated narrowest state keeps widening.
static void widenNarrowest(uint16_t vref[]) {
  float best = narrowestAt(vref);
  if (best < 0) return;
  for (int32_t step = 64; step >= 4; step /= 2) {
    bool moved;
    do {
      moved = false;
      for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
        for (int8_t sign = -1; sign <= 1; sign += 2) {
          uint16_t trial[NUM_CHANNELS];
          memcpy(trial, vref, sizeof trial);
          trial[ch] = withinMargins(ch, vref[ch] + sign * step);
          if (trial[ch] == vref[ch]) continue;
          float width = narrowestAt(trial);
          if (width > best + WIDEN_MIN_MM) {
            best = width;
            memcpy(vref, trial, sizeof trial);
            moved = true;
          }
        }
      }
    } while (moved);
  }
}


// Everything the recording says: each channel's levels, switching points, duty and suggested threshold, and the state
// widths measured from the comparators and simulated at the current, half-duty and suggested thresholds.
// Half duty takes two passes. Each channel's own dwells give a first threshold; a simulation at those finds the
// windows of steady travel; then each threshold is found again from its duty within those windows. The suggestion then
// starts from whichever spaces the edges better, half duty or the thresholds in force, and lets the channels give way.
void analyseSweep(SweepResult& r) {
  memset(&r, 0, sizeof r);
  resolveSwitchPoints(r);

  uint16_t active[NUM_CHANNELS], half[NUM_CHANNELS], suggested[NUM_CHANNELS];
  bool haveHalf = sampleCount > 1;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    ChannelResult& c = r.ch[ch];
    c.lightMv = minMv[ch];
    c.darkMv = maxMv[ch];
    c.vref = active[ch] = sweepVref[ch];
    c.riseMv = riseAt(ch, c.vref);
    c.fallMv = fallAt(ch, c.vref);
    c.duty = NAN;
    c.suggest = SUGGEST_SHORT;
    if (sampleCount > 1) findHalfDuty(ch, c, ownDuty);
    half[ch] = c.halfVref;
    haveHalf = haveHalf && c.halfVref;
  }

  windowCount = 0;
  if (haveHalf) findWindows(simulateStates(half));
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    ChannelResult& c = r.ch[ch];
    if (windowCount >= 6 * SWEEP_MIN_PERIODS) {
      findHalfDuty(ch, c, windowDuty);
      c.duty = windowDuty(ch, c.vref);
    } else if (sampleCount > 1) {
      c.duty = ownDuty(ch, c.vref);
    }
    half[ch] = c.halfVref;
    haveHalf = haveHalf && c.halfVref;
  }

  if (haveHalf) {
    uint16_t current[NUM_CHANNELS];
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) current[ch] = withinMargins(ch, active[ch]);
    memcpy(suggested, narrowestAt(current) > narrowestAt(half) ? current : half, sizeof suggested);
    widenNarrowest(suggested);
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) r.ch[ch].suggestVref = suggested[ch];
  }

  analyseStates(hwUs, hwState, hwCount, r.hw);
  if (sampleCount > 1) analyseStates(simUs, simState, simulateStates(active), r.simActive);
  if (haveHalf) {
    analyseStates(simUs, simState, simulateStates(half), r.simHalf);
    analyseStates(simUs, simState, simulateStates(suggested), r.simSuggest);
  }
}

// One Spacing as a key=value line, which the Serial Plotter ignores. Widths in mm, states with A as the left bit.
void printSpacing(const char* label, const uint16_t vref[], const Spacing& s) {
  Serial.printf("# %s vref=%u/%u/%u invalid=%u periods=%u", label, vref[CH_A], vref[CH_B], vref[CH_C], s.invalid,
                s.periods);
  for (uint8_t st : JOHNSON_ORDER) Serial.printf(" w%u%u%u=%.2f", (st >> 2) & 1, (st >> 1) & 1, st & 1, s.width[st]);
  Serial.printf(" min=%.2f best=%.2f", s.minWidth, s.bestWidth);
  printDuty("dutyA", s.duty[CH_A]);
  printDuty("dutyB", s.duty[CH_B]);
  printDuty("dutyC", s.duty[CH_C]);
  Serial.println();
}

// A duty as a key=value token, or key=none if it couldn't be measured. Printing the NAN itself shows inf: on the RP2040
// the ROM's float-to-double conversion, which printf's %f goes through, turns NaN into infinity.
void printDuty(const char* key, float duty) {
  if (isnan(duty)) Serial.printf(" %s=none", key);
  else Serial.printf(" %s=%.2f", key, duty);
}

const char* switchSourceName(SwitchSource source) {
  switch (source) {
    case SWITCH_MEASURED: return "meas";
    case SWITCH_BORROWED: return "borrowed";
    default: return "nominal";
  }
}
