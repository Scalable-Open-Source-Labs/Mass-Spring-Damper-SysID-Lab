#include <EEPROM.h>
#include "calibration.h"
#include "gpio.h"
#include "sweep.h"

#define CAL_MAGIC 0x4D534443  // "MSDC"
#define CAL_VERSION 2         // Bump on any change to the struct or the meaning of its fields. Version 1 centred each
                              // threshold between the light and dark levels, which can push two channels' edges together.
#define CAL_EEPROM_BYTES 256
#define CAL_VREF_MIN_MV 200
#define CAL_VREF_MAX_MV 2500

struct Calibration {
  uint32_t magic;
  uint16_t version;
  uint16_t vref_mV[NUM_CHANNELS];
  uint16_t light_mV[NUM_CHANNELS];  // Sweep minimum on BUFF_x the thresholds were derived from
  uint16_t dark_mV[NUM_CHANNELS];   // Sweep maximum on BUFF_x
  uint16_t crc;                     // CRC-16/CCITT-FALSE over every byte before this field
};
static_assert(offsetof(Calibration, crc) == sizeof(uint32_t) + (1 + 3 * NUM_CHANNELS) * sizeof(uint16_t),
              "Calibration must have no padding inside the CRC range");

static Calibration saved;  // The unit's saved calibration, valid while calibrated is true
static bool calibrated = false;


// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
static uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)*data++ << 8;
    for (uint8_t bit = 0; bit < 8; bit++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

static uint16_t calibrationCrc(const Calibration& cal) {
  return crc16((const uint8_t*)&cal, offsetof(Calibration, crc));
}

static bool vrefInRange(uint16_t millivolts) {
  return millivolts >= CAL_VREF_MIN_MV && millivolts <= CAL_VREF_MAX_MV;
}


// Copies the saved thresholds into vref_mV if the unit has a valid calibration, else leaves vref_mV unchanged.
// A blank sector, an older struct version or corrupt data all count as uncalibrated. gpio_initialise() calls this at
// boot, which also starts the EEPROM buffer that the save functions write through.
bool loadCalibration(uint16_t vref_mV[]) {
  EEPROM.begin(CAL_EEPROM_BYTES);
  EEPROM.get(0, saved);
  calibrated = saved.magic == CAL_MAGIC && saved.version == CAL_VERSION && saved.crc == calibrationCrc(saved);
  for (uint8_t ch = 0; calibrated && ch < NUM_CHANNELS; ch++) calibrated = vrefInRange(saved.vref_mV[ch]);
  if (calibrated) memcpy(vref_mV, saved.vref_mV, sizeof(saved.vref_mV));
  return calibrated;
}

// True if this unit has never been calibrated: the calibration sector holds no calibration at all, as on a freshly
// programmed chip or after clearCalibration(). A stale version or corrupt data doesn't count, so a deployed unit never
// falls into the first-boot calibration.
bool calibrationBlank() {
  return saved.magic != CAL_MAGIC;
}

// Analyses the recorded sweep and calibrates from it, as calibrateFrom()
bool calibrateFromSweep() {
  SweepResult r;
  analyseSweep(r);
  return calibrateFrom(r);
}

// Sets the thresholds from an analysed sweep where they space the encoder's edges best: each channel light for half of
// each strip period, with the others giving way when a margin holds one short (sweep.h). Then saves and applies them.
// Saves nothing if a channel's light and dark levels are too close (a weak sensor), the sweep has too few full periods,
// or the comparators, simulated over the sweep at the new thresholds, would leave any state narrower than
// MIN_STATE_WIDTH_MM. The flash write stalls interrupts for tens of ms, so only call this from test mode or first-boot
// calibration.
bool calibrateFrom(const SweepResult& r) {
  Calibration cal = {};
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    const ChannelResult& c = r.ch[ch];
    uint16_t span = c.darkMv > c.lightMv ? c.darkMv - c.lightMv : 0;
    if (span < MIN_SPAN_MV || c.suggest == SUGGEST_NO_ROOM) {  // needs: room for both margins and the hysteresis
      Serial.printf("# calibration=failed reason=span ch=%c span=%u min_span=%u needs=%d\n", 'A' + ch, span,
                    MIN_SPAN_MV, 2 * MIN_MARGIN_MV + c.riseMv - c.fallMv);
      return false;
    }
    if (c.suggest == SUGGEST_SHORT) {
      Serial.printf("# calibration=failed reason=short_sweep ch=%c periods=%u min_periods=%u\n", 'A' + ch,
                    r.hw.periods, SWEEP_MIN_PERIODS);
      return false;
    }
    if (!vrefInRange(c.suggestVref)) {
      Serial.printf("# calibration=failed reason=vref_range ch=%c vref=%u\n", 'A' + ch, c.suggestVref);
      return false;
    }
    cal.vref_mV[ch] = c.suggestVref;
    cal.light_mV[ch] = c.lightMv;
    cal.dark_mV[ch] = c.darkMv;
  }

  const Spacing& spacing = r.simSuggest;
  if (spacing.periods < SWEEP_MIN_PERIODS) {
    Serial.printf("# calibration=failed reason=short_sweep periods=%u min_periods=%u\n", spacing.periods,
                  SWEEP_MIN_PERIODS);
    return false;
  }
  if (spacing.invalid || spacing.minWidth < MIN_STATE_WIDTH_MM) {
    Serial.printf("# calibration=failed reason=spacing invalid=%u min=%.2f min_width=%.2f\n", spacing.invalid,
                  spacing.minWidth, MIN_STATE_WIDTH_MM);
    return false;
  }

  cal.magic = CAL_MAGIC;
  cal.version = CAL_VERSION;
  cal.crc = calibrationCrc(cal);

  EEPROM.put(0, cal);
  if (!EEPROM.commit()) {
    Serial.println("# calibration=failed reason=eeprom_write");
    return false;
  }
  saved = cal;
  calibrated = true;
  applyVrefs(saved.vref_mV);
  printCalibration();
  return true;
}

// Forgets the saved calibration and returns every channel to VREF_DEFAULT_MV. The next boot then calibrates as a
// freshly programmed unit does (calibrationBlank()).
void clearCalibration() {
  Calibration blank = {};
  EEPROM.put(0, blank);
  EEPROM.commit();
  saved = blank;
  calibrated = false;

  const uint16_t defaults[NUM_CHANNELS] = { VREF_DEFAULT_MV, VREF_DEFAULT_MV, VREF_DEFAULT_MV };
  applyVrefs(defaults);
  printCalibration();
}

bool isCalibrated() {
  return calibrated;
}

// The saved calibration as key=value lines, so the Serial Plotter ignores them
void printCalibration() {
  if (!calibrated) {
    Serial.printf("# cal=no VrefA=%u VrefB=%u VrefC=%u mV\n", getVref(CH_A), getVref(CH_B), getVref(CH_C));
    return;
  }
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    Serial.printf("# cal=yes %c vref=%u light=%u dark=%u\n", 'A' + ch, saved.vref_mV[ch], saved.light_mV[ch],
                  saved.dark_mV[ch]);
  }
}
