// Host tests for sweep.cpp and calibration.cpp: a synthetic encoder, swept by a synthetic hand, read by a synthetic
// test-mode loop. Ground truth comes from the physical model, never from the firmware's own maths. Run with run.sh.
#include "Arduino.h"
#include "EEPROM.h"
#include "gpio.h"
#include "sweep.h"
#include "calibration.h"
#include <vector>
#include <random>
#include <algorithm>
#include <string>

SerialStub Serial;
EEPROMClass EEPROM;
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return HIGH; }
void analogWrite(uint8_t, int) {}
void analogWriteFreq(uint32_t) {}
void analogWriteRange(uint32_t) {}
void analogReadResolution(int) {}
int analogRead(uint8_t) { return 0; }
void delay(unsigned long) {}
uint32_t micros() { return 0; }
uint32_t millis() { return 0; }
void noInterrupts() {}
void interrupts() {}

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- Physical model: 3 mm bars at a 6 mm pitch, a blurred spot, a phototransistor on a 10k pull-up that clips ----
static const double PERIOD = 6.0, BAR = 3.0;

static double barCoverage(double x, double sigma) {
  double s = 0;
  int k0 = (int)floor(x / PERIOD);
  for (int k = k0 - 2; k <= k0 + 3; k++) {
    double c = k * PERIOD;
    s += 0.5 * (erf((x - (c - BAR / 2)) / (sigma * M_SQRT2)) - erf((x - (c + BAR / 2)) / (sigma * M_SQRT2)));
  }
  return s;
}

struct Channel {
  double phase;     // mm
  double lightMa;   // Photocurrent on a bar
  double bandMv;    // True hysteresis
  double offsetMv;  // True rise point minus the nominal Vref * 34/33
  double sigma = 0.6;       // Spot blur, mm
  double darkRatio = 0.37;  // Photocurrent between bars, as a fraction of that on a bar
  double floorMv = 220;     // Where the phototransistor saturates
};

static double buffMv(const Channel& c, double x) {
  double i = c.lightMa * (c.darkRatio + (1 - c.darkRatio) * barCoverage(x - c.phase, c.sigma));
  return std::max(3300.0 - 10000.0 * i, c.floorMv);
}
static double trueRise(const Channel& c, uint16_t vref) { return vref * 34.0 / 33.0 + c.offsetMv; }
static double trueFall(const Channel& c, uint16_t vref) { return trueRise(c, vref) - c.bandMv; }

// ---- A hand: smooth moves with a wobble, and pauses ----
struct Move { double to, seconds, pause; };
struct Motion {
  double x0;
  std::vector<Move> moves;
  double wobbleMm = 0.3, wobbleHz = 3;
  double total() const { double t = 0; for (auto& m : moves) t += m.seconds + m.pause; return t; }
  double at(double t) const {
    double x = x0;
    for (auto& m : moves) {
      if (t < m.seconds) {
        double tau = t / m.seconds;
        return x + (m.to - x) * (1 - cos(M_PI * tau)) / 2 + wobbleMm * sin(2 * M_PI * wobbleHz * t) * sin(M_PI * tau);
      }
      t -= m.seconds;
      x = m.to;
      if (t < m.pause) return x;
      t -= m.pause;
    }
    return x;
  }
};

static Motion passes(int n, double from, double to, double seconds, double pause = 0.3) {
  Motion m{ from, {} };
  for (int i = 0; i < n; i++) m.moves.push_back({ i % 2 ? from : to, seconds, pause });
  return m;
}

// ---- The test-mode loop: pin read, then each channel's ADC read a little later ----
static const double ADC_DELAY_US[3] = { 16, 48, 80 };

static SweepResult sweep(const Channel ch[3], const uint16_t vref[3], const Motion& m, uint32_t seed,
                         double loopUs = 250) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> jitter(-30, 30);
  std::normal_distribution<double> noise(0, 3);
  for (int c = 0; c < 3; c++) setVref(c, vref[c]);
  sweepBegin();
  bool light[3];
  for (int c = 0; c < 3; c++) light[c] = buffMv(ch[c], m.at(0)) < (trueRise(ch[c], vref[c]) + trueFall(ch[c], vref[c])) / 2;
  const uint32_t base = 4294000000u;  // Near the wrap, to exercise the unsigned time arithmetic
  double t = 0, lastT = 0, nextStreamUs = 50000, end = m.total() * 1e6;
  while (t < end) {
    for (double tt = lastT; tt < t; tt += 5) {  // The comparators track the signal continuously
      double x = m.at(tt * 1e-6);
      for (int c = 0; c < 3; c++) {
        double v = buffMv(ch[c], x);
        if (!light[c] && v < trueFall(ch[c], vref[c])) light[c] = true;
        else if (light[c] && v > trueRise(ch[c], vref[c])) light[c] = false;
      }
    }
    lastT = t;
    uint8_t state = 0;
    for (int c = 0; c < 3; c++) if (light[c]) state |= CHANNEL_BIT(c);
    uint32_t adcUs[3];
    uint16_t mv[3];
    for (int c = 0; c < 3; c++) {
      double ta = t + ADC_DELAY_US[c];
      adcUs[c] = base + (uint32_t)llround(ta);
      mv[c] = (uint16_t)std::max(0.0, std::round(buffMv(ch[c], m.at(ta * 1e-6)) + noise(rng)));
    }
    sweepAdd(base + (uint32_t)llround(t), state, adcUs, mv);
    t += loopUs + jitter(rng);
    if (t > nextStreamUs) { t += 300; nextStreamUs += 50000; }  // The 20 Hz serial stream
  }
  SweepResult r;
  analyseSweep(r);
  return r;
}

// ---- Ground truth, straight from the model ----
static const uint8_t ORDER[6] = { 0b100, 0b110, 0b111, 0b011, 0b001, 0b000 };

// One channel's light interval around its bar, travelling one way
struct Interval { double start, end; };
static Interval lightInterval(const Channel& c, uint16_t vref, int dir) {
  const double step = 0.002;
  double x = c.phase - 3 * dir;
  bool light = false;
  double enter = x;
  for (int i = 0; i <= (int)(6 / step); i++, x += dir * step) {
    double v = buffMv(c, x);
    if (!light && v < trueFall(c, vref)) { light = true; enter = x; }
    else if (light && v > trueRise(c, vref)) return { std::min(enter, x), std::max(enter, x) };
  }
  return { NAN, NAN };
}

// State widths at these thresholds, averaged over both directions of travel, indexed by state
struct Truth { double w[8]; double lightWidth[3]; double narrowest; };
static Truth trueSpacing(const Channel ch[3], const uint16_t vref[3]) {
  Truth t{};
  for (int dir = -1; dir <= 1; dir += 2) {
    Interval a = lightInterval(ch[0], vref[0], dir), b = lightInterval(ch[1], vref[1], dir),
             c = lightInterval(ch[2], vref[2], dir);
    double w[6] = { b.start - a.start, c.start - b.start, a.end - c.start, b.end - a.end, c.end - b.end,
                    a.start + 6 - c.end };
    for (int k = 0; k < 6; k++) t.w[ORDER[k]] += w[k] / 2;
    t.lightWidth[0] += (a.end - a.start) / 2;
    t.lightWidth[1] += (b.end - b.start) / 2;
    t.lightWidth[2] += (c.end - c.start) / 2;
  }
  t.narrowest = INFINITY;
  for (uint8_t s : ORDER) t.narrowest = std::min(t.narrowest, t.w[s]);
  return t;
}

// The best narrowest state any thresholds inside the true 200 mV margins could give: a 5 mV grid search
struct Optimum { uint16_t vref[3]; double narrowest; };
static Optimum trueOptimum(const Channel ch[3]) {
  std::vector<uint16_t> cand[3];
  std::vector<Interval> fwd[3], bwd[3];
  for (int c = 0; c < 3; c++) {
    double dark = 0, light = INFINITY;  // The levels the margins are measured from, as a sweep would see them
    for (int i = -50; i <= 50; i++) {
      dark = std::max(dark, buffMv(ch[c], ch[c].phase + 3 + i * 0.01));
      light = std::min(light, buffMv(ch[c], ch[c].phase + i * 0.01));
    }
    for (uint16_t v = 100; v < 3000; v += 5) {
      if (trueFall(ch[c], v) < light + MIN_MARGIN_MV || trueRise(ch[c], v) > dark - MIN_MARGIN_MV) continue;
      Interval f = lightInterval(ch[c], v, 1), b = lightInterval(ch[c], v, -1);
      if (isnan(f.start) || isnan(b.start)) continue;  // Doesn't switch there
      cand[c].push_back(v);
      fwd[c].push_back(f);
      bwd[c].push_back(b);
    }
  }
  Optimum best{ { 0, 0, 0 }, -INFINITY };
  for (size_t i = 0; i < cand[0].size(); i++)
    for (size_t j = 0; j < cand[1].size(); j++)
      for (size_t k = 0; k < cand[2].size(); k++) {
        double narrowest = INFINITY;
        const Interval* sets[2][3] = { { &fwd[0][i], &fwd[1][j], &fwd[2][k] }, { &bwd[0][i], &bwd[1][j], &bwd[2][k] } };
        double w[6] = {};
        for (auto& s : sets) {
          const Interval &a = *s[0], &b = *s[1], &c = *s[2];
          w[0] += (b.start - a.start) / 2;
          w[1] += (c.start - b.start) / 2;
          w[2] += (a.end - c.start) / 2;
          w[3] += (b.end - a.end) / 2;
          w[4] += (c.end - b.end) / 2;
          w[5] += (a.start + 6 - c.end) / 2;
        }
        for (double x : w) narrowest = std::min(narrowest, x);
        if (narrowest > best.narrowest) best = { { cand[0][i], cand[1][j], cand[2][k] }, narrowest };
      }
  return best;
}

static void printTruth(const char* label, const Truth& t) {
  printf("  %-12s", label);
  for (uint8_t s : ORDER) printf(" w%d%d%d=%.2f", s >> 2 & 1, s >> 1 & 1, s & 1, t.w[s]);
  printf("  min=%.2f light=%.2f/%.2f/%.2f\n", t.narrowest, t.lightWidth[0], t.lightWidth[1], t.lightWidth[2]);
}

// Every measured state width within tol of the truth
static void checkWidths(const char* what, const Spacing& s, const Truth& t, double tol) {
  for (uint8_t st : ORDER)
    CHECK(fabs(s.width[st] - t.w[st]) <= tol, "%s w%d%d%d=%.3f, true %.3f", what, st >> 2 & 1, st >> 1 & 1, st & 1,
          s.width[st], t.w[st]);
}

static void checkSwitchPoints(const char* what, const SweepResult& r, const Channel ch[3], double tol) {
  for (int c = 0; c < 3; c++) {
    const ChannelResult& cr = r.ch[c];
    double rise = trueRise(ch[c], cr.vref), fall = trueFall(ch[c], cr.vref);
    printf("  %s %c: rise=%d (true %.1f) fall=%d (true %.1f) hyst=%d (true %.0f) readings=%u src=%s\n", what, 'A' + c,
           cr.riseMv, rise, cr.fallMv, fall, cr.riseMv - cr.fallMv, ch[c].bandMv, cr.switchReadings,
           switchSourceName(cr.switchSource));
    CHECK(cr.switchSource == SWITCH_MEASURED, "%s %c switching points not measured", what, 'A' + c);
    CHECK(fabs(cr.riseMv - rise) <= tol, "%s %c rise off by %.1f", what, 'A' + c, cr.riseMv - rise);
    CHECK(fabs(cr.fallMv - fall) <= tol, "%s %c fall off by %.1f", what, 'A' + c, cr.fallMv - fall);
  }
}

// Each half-duty threshold should make its channel light for 3.00 mm of every 6, unless held at a margin bound
static void checkHalfDuty(const char* what, const Channel ch[3], const SweepResult& r, double tol = 0.05) {
  uint16_t half[3];
  for (int c = 0; c < 3; c++) half[c] = r.ch[c].halfVref;
  Truth t = trueSpacing(ch, half);
  for (int c = 0; c < 3; c++) {
    if (r.ch[c].suggest != SUGGEST_OK) continue;
    CHECK(fabs(t.lightWidth[c] - 3.0) <= tol, "%s %c light width %.3f at half-duty %u", what, 'A' + c,
          t.lightWidth[c], half[c]);
  }
}

// The suggestion should come within tol of the best narrowest state the margins allow, and never below half duty's
static void checkSuggestion(const char* what, const Channel ch[3], const SweepResult& r, const Optimum& opt,
                            double tol = 0.03) {
  uint16_t half[3], sug[3];
  for (int c = 0; c < 3; c++) {
    half[c] = r.ch[c].halfVref;
    sug[c] = r.ch[c].suggestVref;
  }
  Truth th = trueSpacing(ch, half), ts = trueSpacing(ch, sug);
  printf("  %s: true narrowest %.3f at half %u/%u/%u, %.3f at suggest %u/%u/%u, optimum %.3f at %u/%u/%u\n", what,
         th.narrowest, half[0], half[1], half[2], ts.narrowest, sug[0], sug[1], sug[2], opt.narrowest, opt.vref[0],
         opt.vref[1], opt.vref[2]);
  CHECK(ts.narrowest >= opt.narrowest - tol, "%s suggestion %.3f short of the optimum %.3f", what, ts.narrowest,
        opt.narrowest);
  CHECK(ts.narrowest >= th.narrowest - 0.01, "%s suggestion %.3f worse than half duty %.3f", what, ts.narrowest,
        th.narrowest);
}

static void printResult(const SweepResult& r) {
  uint16_t active[3], half[3], suggested[3];
  for (int c = 0; c < 3; c++) {
    const ChannelResult& cr = r.ch[c];
    active[c] = cr.vref;
    half[c] = cr.halfVref;
    suggested[c] = cr.suggestVref;
    printf("  %c min=%u max=%u vref=%u rise=%d fall=%d", 'A' + c, cr.lightMv, cr.darkMv, cr.vref, cr.riseMv,
           cr.fallMv);
    printDuty("duty", cr.duty);
    printf(" half=%u status=%d suggest=%u\n", cr.halfVref, cr.suggest, cr.suggestVref);
  }
  printf(" ");
  printSpacing("hw", active, r.hw);
  printf(" ");
  printSpacing("sim", active, r.simActive);
  if (r.simSuggest.periods) {
    printf(" ");
    printSpacing("half", half, r.simHalf);
    printf(" ");
    printSpacing("suggest", suggested, r.simSuggest);
  }
}

static uint16_t oldMidpointVref(uint16_t mid) { return ((uint32_t)(mid + 50) * 33 + 17) / 34; }  // Calibration v1

int main() {
  const Motion threePasses = passes(3, -40, 40, 1.5);
  const uint16_t v780[3] = { 780, 780, 780 };

  // 1. Moderately clipped sensors. Non-nominal comparators on every channel.
  printf("== 1. 0.33 mA sensors at the 780 mV default, three passes\n");
  Channel ch1[3] = { { 0, 0.33, 70, +12 }, { 1, 0.33, 95, -8 }, { 2, 0.33, 130, +15 } };
  Optimum opt1 = trueOptimum(ch1);
  SweepResult r1 = sweep(ch1, v780, threePasses, 1);
  printResult(r1);
  checkSwitchPoints("780", r1, ch1, 4);
  Truth t780 = trueSpacing(ch1, v780);
  printTruth("truth@780", t780);
  checkWidths("hw@780", r1.hw, t780, 0.05);
  checkWidths("sim@780", r1.simActive, t780, 0.05);
  CHECK(r1.hw.invalid == 0, "hw invalid=%u at 780", r1.hw.invalid);
  for (int c = 0; c < 3; c++) CHECK(r1.ch[c].suggest == SUGGEST_OK, "%c half-duty status %d", 'A' + c, r1.ch[c].suggest);
  checkHalfDuty("780", ch1, r1);
  checkSuggestion("780", ch1, r1, opt1);
  uint16_t sug1[3];
  for (int c = 0; c < 3; c++) sug1[c] = r1.ch[c].suggestVref;
  Truth ts1 = trueSpacing(ch1, sug1);
  printTruth("truth@sug", ts1);
  checkWidths("sim@sug", r1.simSuggest, ts1, 0.05);

  // 2. The same unit at the old midpoint thresholds: the all-dark state collapses, and the suggestion doesn't depend
  // on which thresholds were in force during the sweep
  printf("== 2. Same unit at the version-1 midpoint thresholds\n");
  uint16_t vmid[3];
  for (int c = 0; c < 3; c++) vmid[c] = oldMidpointVref((r1.ch[c].lightMv + r1.ch[c].darkMv) / 2);
  SweepResult r2 = sweep(ch1, vmid, threePasses, 2);
  printResult(r2);
  checkSwitchPoints("mid", r2, ch1, 4);
  Truth tmid = trueSpacing(ch1, vmid);
  printTruth("truth@mid", tmid);
  checkWidths("hw@mid", r2.hw, tmid, 0.05);
  CHECK(r2.hw.width[0] < r1.hw.width[0] - 0.1, "000 didn't narrow at the midpoint: %.2f vs %.2f", r2.hw.width[0],
        r1.hw.width[0]);
  checkHalfDuty("from midpoint", ch1, r2);
  checkSuggestion("from midpoint", ch1, r2, opt1);

  // 3. Deeply clipped: half duty lies below the margin floor, so it is held at the light bound
  printf("== 3. 0.43 mA sensors (deep clipping)\n");
  Channel ch3[3] = { { 0, 0.43, 95, 0 }, { 1, 0.43, 95, 0 }, { 2, 0.43, 95, 0 } };
  SweepResult r3 = sweep(ch3, v780, threePasses, 3);
  printResult(r3);
  for (int c = 0; c < 3; c++)
    CHECK(r3.ch[c].suggest == SUGGEST_LIGHT_BOUND, "%c not held at the light bound (status %d)", 'A' + c, r3.ch[c].suggest);
  for (int c = 0; c < 3; c++) {
    double fall = trueFall(ch3[c], r3.ch[c].suggestVref);
    CHECK(fall - r3.ch[c].lightMv >= MIN_MARGIN_MV - 5, "%c light margin %.0f below the floor", 'A' + c,
          fall - r3.ch[c].lightMv);
  }
  checkSuggestion("deep clipping", ch3, r3, trueOptimum(ch3));

  // 4. Unclipped ("lifted") sensors, the v1.2 failure: 780 is far too low, and half duty is the midpoint
  printf("== 4. 0.29 mA sensors (unclipped)\n");
  Channel ch4[3] = { { 0, 0.29, 95, 0 }, { 1, 0.29, 95, 0 }, { 2, 0.29, 95, 0 } };
  SweepResult r4 = sweep(ch4, v780, threePasses, 4);
  printResult(r4);
  checkHalfDuty("unclipped", ch4, r4);
  checkSuggestion("unclipped", ch4, r4, trueOptimum(ch4));
  for (int c = 0; c < 3; c++) {
    uint16_t mid = oldMidpointVref((r4.ch[c].lightMv + r4.ch[c].darkMv) / 2);
    CHECK(abs((int)r4.ch[c].halfVref - (int)mid) <= 20, "%c half duty %u far from the midpoint %u on an unclipped sensor",
          'A' + c, r4.ch[c].halfVref, mid);
  }

  // 5. Slow, jerky and fast hands
  printf("== 5. Slow and fast sweeps\n");
  Motion slow = passes(3, -40, 40, 6.0);
  SweepResult r5s = sweep(ch1, v780, slow, 5);
  printResult(r5s);
  checkSwitchPoints("slow", r5s, ch1, 4);
  checkHalfDuty("slow 3 Hz wobble", ch1, r5s, 0.15);  // Speed modulation at the stripe frequency: the worst case
  checkSuggestion("slow 3 Hz wobble", ch1, r5s, opt1, 0.08);
  slow.wobbleMm = 0;
  SweepResult r5n = sweep(ch1, v780, slow, 5);
  checkHalfDuty("slow no wobble", ch1, r5n);
  checkSuggestion("slow no wobble", ch1, r5n, opt1);
  Motion jerky = passes(3, -40, 40, 6.0);
  jerky.wobbleMm = 0.8;
  jerky.wobbleHz = 2;
  SweepResult r5j = sweep(ch1, v780, jerky, 15);
  checkHalfDuty("slow jerky", ch1, r5j, 0.15);
  checkSuggestion("slow jerky", ch1, r5j, opt1, 0.08);
  SweepResult r5m = sweep(ch1, v780, passes(4, -40, 40, 2.0), 16);  // What the procedure asks for
  checkHalfDuty("steady 2 s passes", ch1, r5m);
  checkSuggestion("steady 2 s passes", ch1, r5m, opt1);
  SweepResult r5f = sweep(ch1, v780, passes(3, -40, 40, 0.5), 6);
  printResult(r5f);
  checkSwitchPoints("fast", r5f, ch1, 12);
  checkHalfDuty("fast", ch1, r5f);
  checkSuggestion("fast", ch1, r5f, opt1);

  // 6. Long sweep: the recording halves itself several times
  printf("== 6. Ten passes (heavy decimation)\n");
  SweepResult r6 = sweep(ch1, v780, passes(10, -40, 40, 2.0), 7);
  printResult(r6);
  checkWidths("hw@780 long", r6.hw, t780, 0.05);
  checkHalfDuty("long", ch1, r6);
  checkSuggestion("long", ch1, r6, opt1);

  // 7. Mixed sensor strengths on one unit: C is held at its bound, so A and B should give way
  printf("== 7. Mixed sensors 0.30 / 0.38 / 0.43 mA\n");
  Channel ch7[3] = { { 0, 0.30, 90, 5 }, { 1, 0.38, 100, -5 }, { 2, 0.43, 110, 10 } };
  SweepResult r7 = sweep(ch7, v780, threePasses, 8);
  printResult(r7);
  checkHalfDuty("mixed", ch7, r7);
  checkSuggestion("mixed", ch7, r7, trueOptimum(ch7));

  // 8. Fallbacks: a channel that never switches borrows the others' band; none switching falls back to nominal
  printf("== 8. Switching-point fallbacks\n");
  uint16_t vDeadA[3] = { 150, 780, 780 };
  SweepResult r8 = sweep(ch1, vDeadA, threePasses, 9);
  CHECK(r8.ch[0].switchSource == SWITCH_BORROWED, "A source %s", switchSourceName(r8.ch[0].switchSource));
  CHECK(r8.ch[0].riseMv - r8.ch[0].fallMv == ((r8.ch[1].riseMv - r8.ch[1].fallMv) + (r8.ch[2].riseMv - r8.ch[2].fallMv)) / 2,
        "A band isn't the others' mean");
  uint16_t vDead[3] = { 150, 150, 150 };
  SweepResult r8b = sweep(ch1, vDead, threePasses, 10);
  for (int c = 0; c < 3; c++) {
    CHECK(r8b.ch[c].switchSource == SWITCH_NOMINAL, "%c source %s", 'A' + c, switchSourceName(r8b.ch[c].switchSource));
    CHECK(r8b.ch[c].riseMv - r8b.ch[c].fallMv == COMPARATOR_HYST_MV, "%c nominal band %d", 'A' + c,
          r8b.ch[c].riseMv - r8b.ch[c].fallMv);
  }

  // 9. Board 1, fitted to its sweeps of 2026-09-28: A and B clip so hard that half duty is out of reach, and C should
  // give way
  printf("== 9. Board 1\n");
  Channel b1[3] = { { 0.00, 0.461, 93, -7, 0.65, 0.28, 215 }, { 1.04, 0.442, 94, -6, 0.55, 0.30, 215 },
                    { 2.08, 0.417, 99, -3, 0.65, 0.28, 215 } };
  Optimum optB1 = trueOptimum(b1);
  Motion procedure = passes(4, -40, 40, 2.0);
  const uint16_t vPoint1[3] = { 494, 498, 633 }, vOldMid[3] = { 1090, 1103, 1157 };
  struct { const char* name; const uint16_t* vref; uint32_t seed; } b1runs[] = {
    { "board 1 at 780", v780, 21 }, { "board 1 at 494/498/633", vPoint1, 22 }, { "board 1 at the old midpoint", vOldMid, 23 },
  };
  for (auto& run : b1runs) {
    printf("  -- %s\n", run.name);
    SweepResult r = sweep(b1, run.vref, procedure, run.seed);
    printResult(r);
    Truth t = trueSpacing(b1, run.vref);
    printTruth("truth@vref", t);
    checkWidths(run.name, r.hw, t, 0.06);
    CHECK(r.ch[0].suggest == SUGGEST_LIGHT_BOUND, "%s: A not held at the light bound (%d)", run.name, r.ch[0].suggest);
    CHECK(r.ch[2].suggestVref + 40 < r.ch[2].halfVref, "%s: C didn't give way (half %u, suggest %u)", run.name,
          r.ch[2].halfVref, r.ch[2].suggestVref);
    checkSuggestion(run.name, b1, r, optB1);
  }

  // 10. Calibration end to end
  printf("== 10. calibrateFromSweep\n");
  sweep(b1, vPoint1, procedure, 31);
  bool saved = calibrateFromSweep();
  CHECK(saved, "calibration refused a good sweep");
  uint16_t loaded[3] = { 0, 0, 0 };
  CHECK(loadCalibration(loaded), "saved calibration didn't load");
  Truth tl = trueSpacing(b1, loaded);
  printf("  saved %u/%u/%u: true narrowest %.3f, optimum %.3f\n", loaded[0], loaded[1], loaded[2], tl.narrowest,
         optB1.narrowest);
  CHECK(tl.narrowest >= optB1.narrowest - 0.03, "saved thresholds give %.3f, optimum %.3f", tl.narrowest, optB1.narrowest);

  printf("   short sweep (one 20 mm move):\n");
  Motion shortMove{ -10, { { 10, 1.0, 0.2 } } };
  SweepResult rShort = sweep(ch1, v780, shortMove, 12);
  printResult(rShort);
  int commits = EEPROM.commits;
  CHECK(!calibrateFromSweep(), "calibration accepted a short sweep");
  CHECK(EEPROM.commits == commits, "a refused calibration wrote the EEPROM");
  char expect[32];
  snprintf(expect, sizeof expect, " periods=%u ", rShort.hw.periods);
  CHECK(strstr(Serial.last, "reason=short_sweep") && strstr(Serial.last, expect) && rShort.hw.periods > 0,
        "short-sweep refusal should give the sweep's %u periods: %s", rShort.hw.periods, Serial.last);
  CHECK(isnan(rShort.ch[0].duty), "a short sweep measured a duty");
  printDuty("duty", rShort.ch[0].duty);
  printf("\n");
  CHECK(!strcmp(Serial.last, " duty=none"), "an unmeasured duty printed as '%s'", Serial.last);

  printf("   weak sensor (0.06 mA):\n");
  Channel weak[3] = { { 0, 0.33, 95, 0 }, { 1, 0.06, 95, 0 }, { 2, 0.33, 95, 0 } };
  uint16_t vWeak[3] = { 780, 2900, 780 };
  sweep(weak, vWeak, threePasses, 13);
  CHECK(!calibrateFromSweep(), "calibration accepted a weak sensor");

  printf("   misplaced sensor (C 0.6 mm late):\n");
  Channel skew[3] = { { 0, 0.33, 95, 0 }, { 1, 0.33, 95, 0 }, { 2.6, 0.33, 95, 0 } };
  SweepResult rs = sweep(skew, v780, threePasses, 14);
  printResult(rs);
  CHECK(!calibrateFromSweep(), "calibration accepted a 0.4 mm state");

  // 11. johnsonStep against the old transition table
  printf("== 11. johnsonStep vs the old table\n");
  const int8_t OLD[8][8] = {
    { 0, -1, 0, 0, 1, 0, 0, 0 }, { 1, 0, 0, -1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0, 0, -1 },
    { -1, 0, 0, 0, 0, 0, 1, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, -1, 0, 0, 1 }, { 0, 0, 0, 1, 0, 0, -1, 0 },
  };
  for (int a = 0; a < 8; a++)
    for (int b = 0; b < 8; b++) {
      if (a == b) continue;
      int8_t step = johnsonStep(a, b);
      int8_t strict = (step == 1 || step == -1) ? step : 0;
      CHECK(strict == OLD[a][b], "johnsonStep(%d,%d)=%d, old table %d", a, b, step, OLD[a][b]);
    }

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
