#define VERSION_MAJOR 2
#define VERSION_MINOR 0

// Create version string using preprocessor stringification
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STRING TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR)

#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "ramdisk.h"
#include "gpio.h"
#include "calibration.h"
#include "sweep.h"
#include "autocal.h"
#include "LedControlPatched.h"

// Function Prototypes (plays nice with VSCode IntelliSense, Arduino doesn't care)
static void writeToBlocks(const char* data, uint32_t len, uint32_t& current_block, uint32_t& block_offset);
void handleStandbyMode();
void handleCaptureMode();
void handleMountDriveMode();
void handleTestMode();
void handleAutoCalMode();
void readSensors(uint32_t& pinUs, uint32_t adcUs[], uint16_t mv[]);
void printSweepSummary(const SweepResult& r);
void showTestMessage(const uint8_t word[3], unsigned long durationMs);
void handleSerialCommand();
void enableEncoderInterrupts();
void disableEncoderInterrupts();
void processEncoderChange();
int32_t msc_read_callback(uint32_t lba, void* buffer, uint32_t bufsize);
int32_t msc_write_callback(uint32_t lba, uint8_t* buffer, uint32_t bufsize);
void msc_flush_callback(void);
bool msc_start_stop_callback(uint8_t power_condition, bool start, bool load_eject);
bool msc_ready_callback(void);

LedControl lc = LedControl(19, 18, 20, 1);

#define CAPTURE_TIMEOUT_MS 5000  // maximum possible recording duration
#define MOTION_TIMEOUT_MS 500    // timeout when mass has stopped moving
#define USB_DETACH_DELAY_MS 10
#define SERIAL_WAIT_MS 100
#define POST_CAPTURE_DELAY_MS 1000
#define TEST_STREAM_MS 50        // Test mode: 20 Hz live stream for the Serial Plotter
#define BTN_DEBOUNCE_MS 30
#define CAL_HOLD_MS 2000         // Test mode: hold REC this long to calibrate
#define TEST_STATUS_MS 1500      // Test mode: how long "CAL"/"dEF" shows on entry
#define TEST_RESULT_MS 2000      // Test mode: how long "CAL"/"Err"/"dEF" shows after calibrating or clearing
#define AUTOCAL_RESULT_MS 2000   // First-boot calibration: how long "CAL" or "Err" shows

// Volatile variables shared between ISR and main loop
volatile bool stateChanged = false;
volatile int positionCounter = 0;

// State tracking
uint8_t currentState = 0;
uint8_t previousState = 0;        // The last valid state: 010 and 101 are never kept
unsigned int recoveredSkips = 0;  // Two-state jumps counted as +-2: a state the loop missed, or two edges that crossed
unsigned int lostCounts = 0;      // Jumps to the opposite state, whose direction is unknown, so their count is lost

unsigned long timestamp = 0;
bool testStream = true;  // Test mode live stream, toggled with the S command
unsigned long testMessageMs = 0;          // When the current test-mode status word went up
unsigned long testMessageDurationMs = 0;  // How long it stays before the channel mirror resumes

// Test-mode status words as raw 7-seg segment bytes, left to right
const uint8_t WORD_CAL[3] = { 0x4E, 0x77, 0x0E };  // "CAL": running this unit's saved thresholds
const uint8_t WORD_DEF[3] = { 0x3D, 0x4F, 0x47 };  // "dEF": no saved calibration, running the defaults
const uint8_t WORD_ERR[3] = { 0x4F, 0x05, 0x05 };  // "Err": calibration refused, nothing saved
const uint8_t WORD_SLD[3] = { 0x5B, 0x0E, 0x3D };  // "SLd": first-boot calibration, slide the carriage
const uint8_t WORD_SLO[3] = { 0x5B, 0x0E, 0x1D };  // "SLo": moving too fast to measure
const uint8_t WORD_E_S[3] = { 0x4F, 0x01, 0x5B };  // "E-S": sensor placement can't space the edges (reject)
const uint8_t WORD_E_CH[3][3] = {                  // "E-A", "E-b", "E-C": that channel's sensor is too weak (reject)
  { 0x4F, 0x01, 0x77 }, { 0x4F, 0x01, 0x1F }, { 0x4F, 0x01, 0x4E },
};

Adafruit_USBD_MSC usb_msc;

// Data collection structure
#define MAX_DATAPOINTS 5000
struct DataPoint {
  uint32_t millisecond;
  int displacement_mm;
};

DataPoint timeseries[MAX_DATAPOINTS];
int datapoint_count = 0;

// Function to add datapoint
void addDataPoint(uint32_t ms, int disp) {
  if (datapoint_count < MAX_DATAPOINTS) {
    timeseries[datapoint_count].millisecond = ms;
    timeseries[datapoint_count].displacement_mm = disp;
    datapoint_count++;
  }
}


// Generate CSV content from timeseries data
void generateCSV() {
  uint32_t current_block = 4;  // Data blocks start at block 4 (Block0: Boot, Block1: FAT1, Block2: FAT2, Block3: Root Dir)
  uint32_t block_offset = 0;
  char line_buffer[32];  // Single line buffer. The header needs 29 bytes, the longest possible data row 24.

  // Write header
  uint32_t len = sprintf(line_buffer, "Time [us],Displacement [mm]\n");
  writeToBlocks(line_buffer, len, current_block, block_offset);

  // Write data rows, stopping at the last whole row that fits on the disk
  for (int i = 0; i < datapoint_count; i++) {
    len = sprintf(line_buffer, "%lu,%d\n",
                  timeseries[i].millisecond,
                  timeseries[i].displacement_mm);
    if (current_block * DISK_BLOCK_SIZE + block_offset + len > DISK_BLOCK_NUM * DISK_BLOCK_SIZE) {
      Serial.printf("CSV truncated at %d of %d rows (drive full)\n", i, datapoint_count);
      break;
    }
    writeToBlocks(line_buffer, len, current_block, block_offset);
  }

  // Calculate file metrics
  uint32_t file_size = (current_block - 4) * DISK_BLOCK_SIZE + block_offset;
  uint32_t blocks_needed = current_block - 4 + (block_offset > 0 ? 1 : 0);

  // Zero remainder of final block. Partial blocks must be zeroed to avoid garbage data.
  if (block_offset > 0) {
    memset(msc_disk[current_block] + block_offset, 0, DISK_BLOCK_SIZE - block_offset);
  }

  // Update BOTH FAT Tables (Windows requires both copies to match)
  for (int fat_copy = 0; fat_copy < 2; fat_copy++) {
    uint8_t* fat = msc_disk[1 + fat_copy];  // Block 1 (FAT1) and Block 2 (FAT2)

    // Chain logic:
    // if not last block: next_cluster = cluster + 1 (points to next block)
    // if last block: next_cluster = 0xFFF (EOF marker)
    for (uint32_t cluster = 2; cluster < 2 + blocks_needed; cluster++) {
      uint16_t next_cluster = (cluster < 2 + blocks_needed - 1) ? (cluster + 1) : 0xFFF;
      uint32_t byte_offset = (cluster * 3) / 2;

      // FAT12 Packing (12 bits per entry). Two entries = 3 bytes.
      if (cluster % 2 == 0) {
        fat[byte_offset] = next_cluster & 0xFF;
        fat[byte_offset + 1] = (fat[byte_offset + 1] & 0xF0) | ((next_cluster >> 8) & 0x0F);
      } else {
        fat[byte_offset] = (fat[byte_offset] & 0x0F) | ((next_cluster << 4) & 0xF0);
        fat[byte_offset + 1] = (next_cluster >> 4) & 0xFF;
      }
    }
  }

// Update Block3 directory entry
// Block3, starting byte 32 defines Data_.csv
// Bytes 37-39: Characters 6-8 of filename (Data_###) - generate three random alphanumeric characters to append to this file (helps avoid file-overwrites when user copies data onto PC)
// Bytes 60-63: 4-bytes little-endian file size
#define ROOT_DIR_BLOCK 3
#define FILENAME_RANDOM_OFFSET 37  // Start of the 3 spaces in "DATA_   "
#define FILE_SIZE_OFFSET 60

  // Generate 3 random alphanumeric characters
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  const int charset_size = sizeof(charset) - 1;  // Exclude null terminator
  randomSeed(micros());
  for (int i = 0; i < 3; i++) {
    msc_disk[ROOT_DIR_BLOCK][FILENAME_RANDOM_OFFSET + i] = charset[random(charset_size)];
  }

  // Update file size
  msc_disk[ROOT_DIR_BLOCK][FILE_SIZE_OFFSET] = file_size & 0xFF;
  msc_disk[ROOT_DIR_BLOCK][FILE_SIZE_OFFSET + 1] = (file_size >> 8) & 0xFF;
  msc_disk[ROOT_DIR_BLOCK][FILE_SIZE_OFFSET + 2] = (file_size >> 16) & 0xFF;
  msc_disk[ROOT_DIR_BLOCK][FILE_SIZE_OFFSET + 3] = (file_size >> 24) & 0xFF;
}

static void writeToBlocks(const char* data, uint32_t len,
                          uint32_t& current_block, uint32_t& block_offset) {
  for (uint32_t i = 0; i < len; i++) {
    msc_disk[current_block][block_offset++] = data[i];
    if (block_offset >= DISK_BLOCK_SIZE) {
      current_block++;
      block_offset = 0;
    }
  }
}

void updateBootSectorForDiskSize() {
  // Update total sectors in boot sector (bytes 19-20)
  msc_disk[0][19] = DISK_BLOCK_NUM & 0xFF;
  msc_disk[0][20] = (DISK_BLOCK_NUM >> 8) & 0xFF;

  // Calculate required FAT size (12 bits per entry, 8 entries per 3 bytes)
  uint32_t fat_entries = DISK_BLOCK_NUM + 2;  // +2 for reserved entries
  uint32_t fat_bytes = (fat_entries * 3 + 1) / 2;
  uint32_t sectors_per_fat = (fat_bytes + DISK_BLOCK_SIZE - 1) / DISK_BLOCK_SIZE;

  // Update sectors per FAT (bytes 22-23)
  msc_disk[0][22] = sectors_per_fat & 0xFF;
  msc_disk[0][23] = (sectors_per_fat >> 8) & 0xFF;
}


enum modes {
  INITIALISED,
  STANDBY,
  CAPTURE,
  MOUNT_DRIVE,
  TEST,
  AUTOCAL
};
modes mode = STANDBY;
modes last_mode = INITIALISED;

// the setup function runs once when you press reset or power the board
void setup() {
  // Manual begin() is required on core without built-in support e.g. mbed rp2040
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  Serial.begin(115200);
  static uint8_t serial_initialiser_counter = 0;
  while (!Serial && serial_initialiser_counter < 5) {
    delay(100);
    serial_initialiser_counter++;
  }

  gpio_initialise();
  Serial.printf("# fw=%s VrefA=%u VrefB=%u VrefC=%u mV cal=%s\n", VERSION_STRING, getVref(CH_A), getVref(CH_B),
                getVref(CH_C), isCalibrated() ? "yes" : "no");

  // Enter test mode if record button is held down at power-up/reset. A unit never calibrated calibrates first.
  if (digitalRead(btnRec) == 0) mode = TEST;
  else if (calibrationBlank()) mode = AUTOCAL;

  // Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
  usb_msc.setID("DntPanic", "Mass Storage", VERSION_STRING);

  // Set disk size
  usb_msc.setCapacity(DISK_BLOCK_NUM, DISK_BLOCK_SIZE);
  updateBootSectorForDiskSize();
  // Set callback
  usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback, msc_flush_callback);
  usb_msc.setStartStopCallback(msc_start_stop_callback);
  usb_msc.setReadyCallback(msc_ready_callback);

  // Set Lun ready (RAM disk is always ready)
  usb_msc.setUnitReady(true);

  // 7-segment display
  lc.shutdown(0, false);  // wakeup
  lc.setIntensity(0, 15);  // max brightness (0 to 15)
  lc.clearDisplay(0);

}



void loop() {
  switch (mode) {
    case STANDBY:
      handleStandbyMode();
      break;
    case CAPTURE:
      handleCaptureMode();
      break;
    case MOUNT_DRIVE:
      handleMountDriveMode();
      break;
    case TEST:
      handleTestMode();
      break;
    case AUTOCAL:
      handleAutoCalMode();
      break;
  }
}


void handleStandbyMode() {
  if (mode != last_mode) {
    Serial.println("Standing By");

    enableEncoderInterrupts();


    last_mode = mode;

    // throw away first reading - zeros the device and updates encoder-last-state
    processEncoderChange();
    positionCounter = 0;
  }
  if (digitalRead(btnRec) == 0) mode = CAPTURE;

  // Display live displacement
  processEncoderChange();
  long units = abs(positionCounter) % 10;
  long tens = (abs(positionCounter) / 10) % 10;
  lc.setDigit(0, 0, units, false);
  if (tens != 0) lc.setDigit(0, 1, tens, false);
  else lc.setChar(0, 1, ' ', false);

  if (positionCounter < 0) lc.setChar(0, 2, '-', false);
  else lc.setChar(0, 2, ' ', false);
}

void handleCaptureMode() {
  static unsigned long time_begin;
  static unsigned int num_points = 0;

  // Mode entry
  if (mode != last_mode) {
    Serial.println("Capturing");
    digitalWrite(ledRec, HIGH);

    // display "rEC" (recording)
    lc.setRow(0, 2, 0x05);         // r
    lc.setChar(0, 1, 'E', false);  // E
    lc.setRow(0, 0, 0x4E);         // c
    last_mode = mode;

    stateChanged = false;
    recoveredSkips = 0;
    lostCounts = 0;
    time_begin = micros();
  }
  static unsigned long lastMotion = 0;
  // Process state changes
  if (stateChanged) {
    num_points++;

    stateChanged = false;
    processEncoderChange();
    if (num_points == 1) time_begin = timestamp;  // first timestamp starts at zero

    lastMotion = millis();

    if (num_points > MAX_DATAPOINTS) {
      mode = MOUNT_DRIVE;
      return;
    }

    addDataPoint(timestamp - time_begin, positionCounter);
    Serial.printf("%8d, %4d\n", timestamp, positionCounter);
  }


  // Timeout check

  bool motionTimeout = (millis() - lastMotion > MOTION_TIMEOUT_MS) && num_points > 100;

  if (micros() - time_begin > 1000 * CAPTURE_TIMEOUT_MS || motionTimeout) {
    disableEncoderInterrupts();
    Serial.printf("Done, %u skipped states recovered, %u counts lost\n", recoveredSkips, lostCounts);
    digitalWrite(ledRec, LOW);
    mode = MOUNT_DRIVE;
  }
}

void handleMountDriveMode() {
  if (mode != last_mode) {
    last_mode = MOUNT_DRIVE;

    // Display 'U.S.b.' on 7-seg with decimal points
    lc.setRow(0, 2, 0x3E | 0x80);         // U.
    lc.setDigit(0, 1, 5, true);           // S.
    lc.setChar(0, 0, 'b', true);          // b.

    generateCSV();
    usb_msc.begin();

    if (TinyUSBDevice.mounted()) {
      TinyUSBDevice.detach();
      delay(1000);  // Longer delay for Windows to forget the device
      TinyUSBDevice.attach();
    }
  }

#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
}

void handleTestMode() {
  // Optical channel sanity-check, characterisation and calibration mode. Mirrors the raw pin state of chA/chB/chC
  // onto 7-seg digits 0/1/2: top segment only when HIGH (beam clear), bottom segment only when LOW
  // (beam blocked). Lets the user slide the carriage by hand and watch channels respond live.
  // Tap REC to start recording a sweep (all decimal points lit), and tap again to stop and print what it measured
  // (sweep.h). Hold REC to calibrate from the recorded sweep: "CAL" saved, "Err" refused. Needs no serial connection.
  // Also streams the buffered sensor voltages for the Serial Plotter.
  const uint8_t channelPins[3] = { chA, chB, chC };
  static uint16_t sensorMv[NUM_CHANNELS];
  static unsigned long lastStreamMs = 0;
  static bool btnPressed, pressHandled, recording, haveSweep;
  static unsigned long btnChangedMs;

  // Mode entry
  if (mode != last_mode) {
    last_mode = mode;
    btnPressed = true;    // REC is still held from power-up,
    pressHandled = true;  // and that press is neither a tap nor a hold
    btnChangedMs = millis();
    recording = false;
    haveSweep = false;
    Serial.println("# Test mode. Tap REC to record a sweep, tap again to stop and print stats. Hold REC to calibrate from the sweep.");
    Serial.println("# Commands. A/B/C <mV> sets that channel's Vref (not saved), S toggles the stream, X clears the saved calibration.");
    printCalibration();
    showTestMessage(isCalibrated() ? WORD_CAL : WORD_DEF, TEST_STATUS_MS);
  }

  // REC: a tap starts or stops recording; a hold calibrates from the recorded sweep
  bool btnDown = digitalRead(btnRec) == 0;
  if (btnDown != btnPressed && millis() - btnChangedMs >= BTN_DEBOUNCE_MS) {
    btnPressed = btnDown;
    btnChangedMs = millis();
    if (btnPressed) {
      pressHandled = false;
    } else if (!pressHandled) {  // Released before the hold time: a tap
      if (recording) {
        recording = false;
        haveSweep = true;
        SweepResult r;
        analyseSweep(r);
        printSweepSummary(r);
      } else {
        processEncoderChange();  // Sync previousState to the pins so the reset itself isn't counted
        positionCounter = 0;
        recoveredSkips = 0;
        lostCounts = 0;
        sweepBegin();
        recording = true;
      }
    }
  }
  if (btnPressed && !pressHandled && millis() - btnChangedMs >= CAL_HOLD_MS) {
    pressHandled = true;
    if (recording) {  // A hold also ends the recording
      recording = false;
      haveSweep = true;
      SweepResult r;
      analyseSweep(r);
      printSweepSummary(r);
    }
    if (!haveSweep) Serial.println("# calibration=failed reason=no_sweep");
    bool saved = haveSweep && calibrateFromSweep();
    showTestMessage(saved ? WORD_CAL : WORD_ERR, TEST_RESULT_MS);
  }

  uint32_t pinUs, adcUs[NUM_CHANNELS];
  readSensors(pinUs, adcUs, sensorMv);
  // The recording freezes when it stops, so a later hold calibrates from exactly that sweep
  if (recording) sweepAdd(pinUs, currentState, adcUs, sensorMv);

  // Mirror the channels unless a status word is showing. All decimal points lit = recording.
  if (millis() - testMessageMs >= testMessageDurationMs) {
    uint8_t dp = recording ? 0x80 : 0;
    for (uint8_t i = 0; i < 3; i++) {
      if (digitalRead(channelPins[i]) == HIGH) {
        lc.setRow(0, i, 0x40 | dp);  // top segment only (HIGH = beam clear)
      } else {
        lc.setRow(0, i, 0x08 | dp);  // bottom segment only (LOW = beam blocked)
      }
    }
  }

  if (testStream && millis() - lastStreamMs >= TEST_STREAM_MS) {
    lastStreamMs = millis();
    Serial.printf("A:%u,B:%u,C:%u,VrefA:%u,VrefB:%u,VrefC:%u\n", sensorMv[CH_A], sensorMv[CH_B], sensorMv[CH_C],
                  getVref(CH_A), getVref(CH_B), getVref(CH_C));
  }

  handleSerialCommand();
}

// The comparator states (into currentState) at pinUs, then each sensor voltage, read back to back. adcUs is the middle
// of each channel's oversampled read.
void readSensors(uint32_t& pinUs, uint32_t adcUs[], uint16_t mv[]) {
  pinUs = micros();
  processEncoderChange();
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    uint32_t readStartUs = micros();
    mv[ch] = readSensorMillivolts(ch);
    adcUs[ch] = readStartUs + (micros() - readStartUs) / 2;
  }
}

// First-boot calibration (autocal.h): records from power-up while the technician slides the carriage, and saves the
// thresholds once the analysis settles, then carries on in normal mode. The display shows "SLd" (slide) throughout,
// its decimal points filling left to right as good data builds up, and "SLo" while the carriage moves too fast to
// measure. It ends with "CAL", or with a fault code that stays: "E-A", "E-b" or "E-C" for a weak sensor, "E-S" when
// the sensors' placement can't space the edges. "Err" means it is starting over, and the technician keeps sliding.
void handleAutoCalMode() {
  static uint16_t sensorMv[NUM_CHANNELS];
  static SweepResult r;
  static bool faulted, saved;

  if (mode != last_mode) {
    last_mode = mode;
    faulted = saved = false;
    Serial.println("# autocal: not calibrated yet. Slide the carriage end to end at a steady pace until the display shows CAL.");
    Serial.println("# Commands. K skips calibrating for this boot, X clears the saved calibration.");
    processEncoderChange();  // Sync previousState to the pins
    autocalBegin();
    showTestMessage(WORD_SLD, 0);
  }

  if (saved) {  // "CAL" is showing: then normal operation
    if (millis() - testMessageMs >= AUTOCAL_RESULT_MS) mode = STANDBY;
    return;
  }
  handleSerialCommand();
  if (faulted || mode != AUTOCAL) return;

  uint32_t pinUs, adcUs[NUM_CHANNELS];
  readSensors(pinUs, adcUs, sensorMv);
  sweepAdd(pinUs, currentState, adcUs, sensorMv);

  uint8_t channel = 0;
  unsigned long startMs = millis();
  AutoCalOutcome outcome = autocalUpdate(micros(), r, channel);
  if (outcome != AUTOCAL_RECORDING) {  // Each analysis, for a bench PC to log
    Serial.printf("# autocal travel=%u analysis_ms=%lu\n", sweepTravel(), millis() - startMs);
    if (outcome != AUTOCAL_START_OVER) {
      uint16_t suggested[NUM_CHANNELS];
      for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) suggested[ch] = r.ch[ch].suggestVref;
      printSpacing("autocal_suggest", suggested, r.simSuggest);
    }
  }
  switch (outcome) {
    case AUTOCAL_SETTLED:
      printSweepSummary(r);
      saved = calibrateFrom(r);
      if (saved) {
        Serial.println("# autocal=done");
        showTestMessage(WORD_CAL, AUTOCAL_RESULT_MS);
        return;
      }
      Serial.println("# autocal=start_over reason=refused");
      autocalBegin();
      showTestMessage(WORD_ERR, AUTOCAL_RESULT_MS);
      return;
    case AUTOCAL_WEAK:
    case AUTOCAL_MISPLACED:
      printSweepSummary(r);
      faulted = true;
      if (outcome == AUTOCAL_WEAK) {
        Serial.printf("# autocal=fault reason=weak_sensor ch=%c span=%d min_span=%u\n", 'A' + channel,
                      r.ch[channel].darkMv - r.ch[channel].lightMv, MIN_SPAN_MV);
        showTestMessage(WORD_E_CH[channel], 0);
      } else {
        Serial.printf("# autocal=fault reason=placement best=%.2f min_width=%.2f\n", r.simSuggest.bestWidth,
                      MIN_STATE_WIDTH_MM);
        showTestMessage(WORD_E_S, 0);
      }
      testMessageDurationMs = UINT32_MAX;  // A fault stays on the display
      return;
    case AUTOCAL_START_OVER:
      Serial.println("# autocal=start_over reason=not_settled");
      autocalBegin();
      showTestMessage(WORD_ERR, AUTOCAL_RESULT_MS);
      return;
    default:
      break;
  }

  // "SLd", with a decimal point lit per third of the clean periods needed, or "SLo" while too fast. Nothing that
  // counts up, so it can't be read as a displacement. "Err" shows out its time first.
  if (millis() - testMessageMs < testMessageDurationMs) return;
  if (sweepTooFast(micros())) {
    showTestMessage(WORD_SLO, 0);
  } else {
    uint8_t filled = autocalProgress();
    for (uint8_t i = 0; i < 3; i++) lc.setRow(0, 2 - i, WORD_SLD[i] | (i < filled ? 0x80 : 0));
  }
}

// What one sweep measured. Per channel, in mV: min/max are the light/dark levels on BUFF_x; rise/fall are where the
// comparator switched at the Vref in force (hyst_src says whether they were measured), and the margins are how far the
// dark and light levels cleared them. duty is the fraction of each period it read light; half is the Vref that makes
// that half, and suggest is where the three Vrefs together space the edges best. Then the widths of the six encoder
// states in mm (ideally 1 each): from the comparators (hw), and simulated at the current (sim), half-duty (half) and
// suggested (suggest) Vrefs. Everything is key=value, so the Serial Plotter ignores it.
void printSweepSummary(const SweepResult& r) {
  uint16_t active[NUM_CHANNELS], half[NUM_CHANNELS], suggested[NUM_CHANNELS];
  bool haveSuggestion = true;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    const ChannelResult& c = r.ch[ch];
    active[ch] = c.vref;
    half[ch] = c.halfVref;
    suggested[ch] = c.suggestVref;
    haveSuggestion = haveSuggestion && c.suggestVref;
    Serial.printf("# %c min=%u max=%u span=%d mid=%u vref=%u rise=%d fall=%d hyst=%d edges=%u hyst_src=%s "
                  "dark_margin=%d light_margin=%d",
                  'A' + ch, c.lightMv, c.darkMv, c.darkMv - c.lightMv, (c.lightMv + c.darkMv) / 2, c.vref, c.riseMv,
                  c.fallMv, c.riseMv - c.fallMv, c.switchReadings, switchSourceName(c.switchSource),
                  c.darkMv - c.riseMv, c.fallMv - c.lightMv);
    printDuty("duty", c.duty);
    if (!c.halfVref) {
      Serial.printf(" suggest=none reason=%s\n", c.suggest == SUGGEST_NO_ROOM ? "no_room" : "short_sweep");
      continue;
    }
    Serial.printf(" half=%u%s", c.halfVref,
                  c.suggest == SUGGEST_LIGHT_BOUND  ? " clamped=light"
                  : c.suggest == SUGGEST_DARK_BOUND ? " clamped=dark"
                                                    : "");
    if (c.suggestVref) Serial.printf(" suggest=%u", c.suggestVref);
    Serial.println();
  }
  printSpacing("hw", active, r.hw);
  printSpacing("sim", active, r.simActive);
  if (haveSuggestion) {
    printSpacing("half", half, r.simHalf);
    printSpacing("suggest", suggested, r.simSuggest);
  }
  // First-boot calibration's analyses stall the loop mid-motion, so its decoder counts say nothing about the unit
  if (mode == TEST) Serial.printf("# recovered=%u lost=%u position=%d\n", recoveredSkips, lostCounts, positionCounter);
}

// Shows a 3-letter status word on the 7-seg, pausing the test-mode channel mirror for durationMs
void showTestMessage(const uint8_t word[3], unsigned long durationMs) {
  lc.setRow(0, 2, word[0]);
  lc.setRow(0, 1, word[1]);
  lc.setRow(0, 0, word[2]);
  testMessageMs = millis();
  testMessageDurationMs = durationMs;
}

// Test-mode commands, one per line. "A 820" sets channel A's Vref to 820 mV until reset (not saved). "S" toggles the
// stream. "X" clears the saved calibration, so the next boot calibrates as a new unit. "K" skips first-boot calibration
// for this boot.
void handleSerialCommand() {
  static char line[16];
  static uint8_t len = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (len < sizeof(line) - 1) line[len++] = c;
      continue;
    }
    line[len] = '\0';
    len = 0;
    if (line[0] == '\0') continue;  // Blank line, or the second half of \r\n

    char cmd = toupper(line[0]);
    char* end;
    long mv = strtol(line + 1, &end, 10);
    if (cmd == 'S') {
      testStream = !testStream;
      Serial.printf("# stream=%s\n", testStream ? "on" : "off");
    } else if (cmd == 'X') {
      clearCalibration();
      Serial.println("# cal=cleared next_boot=autocal");
      if (mode == TEST) showTestMessage(WORD_DEF, TEST_RESULT_MS);
    } else if (cmd == 'K' && mode == AUTOCAL) {
      Serial.println("# autocal=skipped (defaults until the next boot)");
      mode = STANDBY;
    } else if (cmd >= 'A' && cmd <= 'C' && end != line + 1 && mv >= 0 && mv <= VDD_MV) {
      setVref(cmd - 'A', mv);
      Serial.printf("# Vref%c=%u mV (not saved)\n", cmd, getVref(cmd - 'A'));
    } else {
      Serial.println("# Commands. A/B/C <mV> sets that channel's Vref (not saved), S toggles the stream, X clears the saved calibration, K skips first-boot calibration.");
    }
  }
}



void encoderISR() {
  timestamp = micros();
  stateChanged = true;
}

// Process encoder state change and update position, counting from the last valid state. A jump of two states is
// unambiguous in the Johnson sequence, so it counts instead of being lost: the loop missed a state, or two channels'
// edges crossed (passing through 010 or 101, which are held until the next valid state). Only a jump to the opposite
// state, whose direction is unknown, loses its count.
void processEncoderChange() {
  currentState = readEncoderState();
  if (currentState == previousState) return;

  int8_t step = johnsonStep(previousState, currentState);
  if (step == JOHNSON_INVALID) return;
  if (step == 3) {
    lostCounts++;
  } else {
    if (step == 2 || step == -2) recoveredSkips++;
    // Disable interrupts briefly while updating position
    noInterrupts();
    positionCounter += step;
    interrupts();
  }
  previousState = currentState;
}


// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_callback(uint32_t lba, void* buffer, uint32_t bufsize) {
  uint8_t const* addr = msc_disk[lba];
  memcpy(buffer, addr, bufsize);

  return bufsize;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_callback(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  uint8_t* addr = msc_disk[lba];
  memcpy(addr, buffer, bufsize);

  return bufsize;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_callback(void) {
  // nothing to do
}

bool msc_start_stop_callback(uint8_t power_condition, bool start, bool load_eject) {
  Serial.printf("Start/Stop callback: power condition %u, start %u, load_eject %u\n", power_condition, start, load_eject);
  return true;
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool msc_ready_callback(void) {
#ifdef BTN_EJECT
  // button not active --> medium ready
  return digitalRead(BTN_EJECT) != activeState;
#else
  return true;
#endif
}


void enableEncoderInterrupts() {
  attachInterrupt(digitalPinToInterrupt(chA), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(chB), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(chC), encoderISR, CHANGE);
}

void disableEncoderInterrupts() {
  detachInterrupt(digitalPinToInterrupt(chA));
  detachInterrupt(digitalPinToInterrupt(chB));
  detachInterrupt(digitalPinToInterrupt(chC));
}