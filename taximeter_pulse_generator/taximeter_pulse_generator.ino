// ============================================================
//  TAXIMETER PULSE GENERATOR
//  Desktop Speed Pulse Simulator - CANM8 CANNECT PULSE Compatible
// ============================================================
//
//  Generates a square-wave speed pulse identical to the output
//  of a CANM8 CANNECT PULSE CAN-bus interface.
//
//  Standard calibration: 1 Hz = 1 MPH  (k = 2237 pulses/km)
//  Also supports 4x and 10x CANM8 variants via config.h
//
//  Hardware:
//    MCU:     ESP32 (any variant with enough free GPIO)
//    Display: MSP4021 4.0" ST7796 SPI TFT 320x480 w/ touch
//    Input:   Potentiometer for speed, two push-buttons
//    Output:  Square wave pulse (3.3V or 12V via NPN)
//
//  Display shows (landscape 480x320):
//    Left  - Large 7-segment speed readout (km/h)
//    Right - Trip (km), Total (km), Pulse frequency (Hz)
//    Bar   - Colour-coded speed bar graph
//
// ============================================================

#include "config.h"
#include <TFT_eSPI.h>
#include <Preferences.h>

// ---- Display ----
TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite speedSpr = TFT_eSprite(&tft);  // Flicker-free speed

// ---- NVS persistent storage ----
Preferences prefs;

// ---- Pulse generation (hardware timer) ----
hw_timer_t *pulseTimer   = NULL;
volatile uint32_t pulseCount  = 0;
volatile bool     pulseState  = false;
bool              pulseActive = true;
bool              timerRunning = false;

// ---- Speed & distance ----
float    currentSpeed   = 0;
float    smoothedSpeed  = 0;
float    tripDistance    = 0;
float    totalDistance   = 0;
uint32_t lastPulseCount = 0;

// ---- Timing ----
unsigned long lastDisplayUpdate = 0;
unsigned long lastDistSave      = 0;

// ---- Previous display values (skip redraws when unchanged) ----
int   prevSpeedWhole = -1;
int   prevSpeedFrac  = -1;
float prevTrip       = -999;
float prevTotal      = -999;
float prevFreq       = -999;
int   prevBarWidth   = -1;
bool  prevActive     = !pulseActive;

// ---- Button debounce ----
bool          lastResetState     = HIGH;
bool          lastStartStopState = HIGH;
unsigned long lastResetTime      = 0;
unsigned long lastStartStopTime  = 0;

// ================================================================
//  TIMER ISR - toggles the pulse output pin
// ================================================================
void IRAM_ATTR onPulseTimer() {
  pulseState = !pulseState;
  bool level = INVERT_OUTPUT ? !pulseState : pulseState;
  if (level)
    GPIO.out_w1ts = ((uint32_t)1 << PULSE_PIN);
  else
    GPIO.out_w1tc = ((uint32_t)1 << PULSE_PIN);
  if (pulseState) pulseCount++;
}

// ================================================================
//  TIMER HELPERS  (ESP32 Arduino Core v2 / v3 compatible)
// ================================================================
void setupPulseTimer() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  pulseTimer = timerBegin(1000000);
  timerAttachInterrupt(pulseTimer, &onPulseTimer);
#else
  pulseTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(pulseTimer, &onPulseTimer, true);
#endif
}

void startPulseTimer(uint64_t halfPeriodUs) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  timerAlarm(pulseTimer, halfPeriodUs, true, 0);
#else
  timerAlarmWrite(pulseTimer, halfPeriodUs, true);
  timerAlarmEnable(pulseTimer);
#endif
  timerRunning = true;
}

void stopPulseTimer() {
  if (!timerRunning) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  timerStop(pulseTimer);
  timerStart(pulseTimer);
#else
  timerAlarmDisable(pulseTimer);
#endif
  digitalWrite(PULSE_PIN, INVERT_OUTPUT ? HIGH : LOW);
  pulseState   = false;
  timerRunning = false;
}

// ================================================================
//  UPDATE PULSE FREQUENCY
// ================================================================
void updatePulseFrequency(float speedKmh) {
  if (speedKmh < MIN_PULSE_SPEED || !pulseActive) {
    stopPulseTimer();
    return;
  }
  float freqHz = (speedKmh / 1.609344) * PULSE_MODE;
  uint64_t halfPeriodUs = (uint64_t)(500000.0 / freqHz);
  if (halfPeriodUs < 10) halfPeriodUs = 10;
  startPulseTimer(halfPeriodUs);
}

// ================================================================
//  READ SPEED FROM POTENTIOMETER
// ================================================================
float readSpeed() {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) sum += analogRead(POT_PIN);
  int raw = sum / ADC_SAMPLES;
  if (raw < ADC_DEADZONE) return 0;
  float spd = (float)(raw - ADC_DEADZONE) / (4095.0 - ADC_DEADZONE) * MAX_SPEED_KMH;
  return constrain(spd, 0, MAX_SPEED_KMH);
}

// ================================================================
//  UPDATE DISTANCE COUNTERS
// ================================================================
void updateDistance() {
  uint32_t cur = pulseCount;
  uint32_t delta = cur - lastPulseCount;
  lastPulseCount = cur;
  if (delta > 0) {
    float km = (float)delta / PULSES_PER_KM;
    tripDistance  += km;
    totalDistance += km;
  }
}

// ================================================================
//  DISPLAY - draw static layout (called once)
// ================================================================
void drawLayout() {
  tft.fillScreen(COLOR_BG);

  // ---- Header ----
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER_BG);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_HEADER_TEXT, COLOR_HEADER_BG);
  tft.drawString("TAXIMETER PULSE GENERATOR", 10, HEADER_H / 2, 4);

  // ---- Vertical divider ----
  tft.fillRect(PANEL_SPLIT_X - 2, CONTENT_TOP, 2, CONTENT_BOT - CONTENT_TOP, COLOR_DIVIDER);

  // ---- Left panel: "km/h" label ----
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(COLOR_LABEL, COLOR_BG);
  tft.drawString("km/h", PANEL_SPLIT_X / 2, CONTENT_BOT - 4, 4);

  // ---- Right panel section backgrounds ----
  // Trip
  tft.fillRoundRect(RIGHT_X, SECT_TRIP_Y, RIGHT_W, SECT_TRIP_H, 4, COLOR_PANEL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
  tft.drawString("TRIP", RIGHT_X + 8, SECT_TRIP_Y + 6, 2);

  // Total
  tft.fillRoundRect(RIGHT_X, SECT_TOTAL_Y, RIGHT_W, SECT_TOTAL_H, 4, COLOR_PANEL_BG);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
  tft.drawString("TOTAL", RIGHT_X + 8, SECT_TOTAL_Y + 6, 2);

  // Output
  tft.fillRoundRect(RIGHT_X, SECT_OUTPUT_Y, RIGHT_W, SECT_OUTPUT_H, 4, COLOR_PANEL_BG);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
  tft.drawString("OUTPUT", RIGHT_X + 8, SECT_OUTPUT_Y + 6, 2);

  // ---- Bottom bar background ----
  tft.fillRect(0, BAR_Y, SCREEN_W, BAR_H, COLOR_BAR_BG);

  // ---- Calibration info in output panel ----
  char calBuf[28];
  snprintf(calBuf, sizeof(calBuf), "k=%ld p/km   %dx mode", PULSES_PER_KM, PULSE_MODE);
  tft.setTextDatum(BL_DATUM);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
  tft.drawString(calBuf, RIGHT_X + 8, SECT_OUTPUT_Y + SECT_OUTPUT_H - 6, 2);
}

// ================================================================
//  DISPLAY - speed (left panel, 7-segment with ghost segments)
// ================================================================
void updateSpeedDisplay() {
  int sw = (int)smoothedSpeed;
  int sf = (int)((smoothedSpeed - sw) * 10);
  if (sw == prevSpeedWhole && sf == prevSpeedFrac) return;
  prevSpeedWhole = sw;
  prevSpeedFrac  = sf;

  int sprW = PANEL_SPLIT_X - 6;
  int sprH = 56;

  speedSpr.createSprite(sprW, sprH);
  speedSpr.fillSprite(COLOR_BG);

  char ghost[] = "888.8";
  char val[8];
  snprintf(val, sizeof(val), "%3d.%d", sw, sf);

  speedSpr.setTextDatum(MC_DATUM);

  // Ghost segments (unlit)
  speedSpr.setTextColor(COLOR_SPEED_GHOST);
  speedSpr.drawString(ghost, sprW / 2, sprH / 2, 7);

  // Active segments
  speedSpr.setTextColor(COLOR_SPEED);
  speedSpr.drawString(val, sprW / 2, sprH / 2, 7);

  int yPos = CONTENT_TOP + (CONTENT_BOT - CONTENT_TOP - 40) / 2 - sprH / 2;
  speedSpr.pushSprite(3, yPos);
  speedSpr.deleteSprite();
}

// ================================================================
//  DISPLAY - trip distance (right panel, top section)
// ================================================================
void updateTripDisplay() {
  if (fabsf(tripDistance - prevTrip) < 0.0005) return;
  prevTrip = tripDistance;

  char buf[14];
  if (tripDistance < 100.0)
    snprintf(buf, sizeof(buf), "%.3f km", tripDistance);
  else
    snprintf(buf, sizeof(buf), "%.2f km", tripDistance);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TRIP, COLOR_PANEL_BG);
  tft.setTextPadding(RIGHT_W - 16);
  tft.drawString(buf, RIGHT_X + 8, SECT_TRIP_Y + SECT_TRIP_H / 2 + 8, 4);
}

// ================================================================
//  DISPLAY - total distance (right panel, middle section)
// ================================================================
void updateTotalDisplay() {
  if (fabsf(totalDistance - prevTotal) < 0.005) return;
  prevTotal = totalDistance;

  char buf[14];
  if (totalDistance < 1000.0)
    snprintf(buf, sizeof(buf), "%.2f km", totalDistance);
  else
    snprintf(buf, sizeof(buf), "%.1f km", totalDistance);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TOTAL, COLOR_PANEL_BG);
  tft.setTextPadding(RIGHT_W - 16);
  tft.drawString(buf, RIGHT_X + 8, SECT_TOTAL_Y + SECT_TOTAL_H / 2 + 8, 4);
}

// ================================================================
//  DISPLAY - frequency & status (right panel, bottom section)
// ================================================================
void updateOutputDisplay() {
  float freqHz = 0;
  if (smoothedSpeed >= MIN_PULSE_SPEED && pulseActive)
    freqHz = (smoothedSpeed / 1.609344) * PULSE_MODE;

  bool changed = (pulseActive != prevActive) || (fabsf(freqHz - prevFreq) > 0.08);
  if (!changed) return;
  prevActive = pulseActive;
  prevFreq   = freqHz;

  // Frequency value
  char fBuf[12];
  if (freqHz < 0.1)
    snprintf(fBuf, sizeof(fBuf), "0.0 Hz");
  else
    snprintf(fBuf, sizeof(fBuf), "%.1f Hz", freqHz);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FREQ, COLOR_PANEL_BG);
  tft.setTextPadding(RIGHT_W - 16);
  tft.drawString(fBuf, RIGHT_X + 8, SECT_OUTPUT_Y + 36, 4);

  // Status indicator (dot + text) on header right side
  int dotX = SCREEN_W - 120;
  int dotY = HEADER_H / 2;
  tft.fillRect(dotX - 10, 4, 128, HEADER_H - 8, COLOR_HEADER_BG);

  if (pulseActive && smoothedSpeed >= MIN_PULSE_SPEED) {
    tft.fillCircle(dotX, dotY, 6, COLOR_RUNNING);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_RUNNING, COLOR_HEADER_BG);
    tft.drawString("ACTIVE", dotX + 12, dotY, 2);
  } else if (!pulseActive) {
    tft.fillCircle(dotX, dotY, 6, COLOR_STOPPED);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_STOPPED, COLOR_HEADER_BG);
    tft.drawString("STOPPED", dotX + 12, dotY, 2);
  } else {
    tft.fillCircle(dotX, dotY, 6, COLOR_LABEL);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_LABEL, COLOR_HEADER_BG);
    tft.drawString("IDLE", dotX + 12, dotY, 2);
  }
}

// ================================================================
//  DISPLAY - speed bar graph (bottom strip)
// ================================================================
void updateSpeedBar() {
  float ratio   = constrain(smoothedSpeed / MAX_SPEED_KMH, 0.0, 1.0);
  int   maxBarW = SCREEN_W - 8;
  int   fillW   = (int)(ratio * maxBarW);

  if (fillW == prevBarWidth) return;
  prevBarWidth = fillW;

  int bx = 4, by = BAR_Y + 5, bh = BAR_H - 10;

  // Colour based on speed zone
  uint16_t col = COLOR_RUNNING;
  if (smoothedSpeed > 120) col = COLOR_STOPPED;
  else if (smoothedSpeed > 60) col = COLOR_FREQ;

  // Filled portion
  if (fillW > 0)
    tft.fillRect(bx, by, fillW, bh, col);
  // Empty portion
  if (fillW < maxBarW)
    tft.fillRect(bx + fillW, by, maxBarW - fillW, bh, COLOR_BAR_BG);

  // Speed text overlay
  char sBuf[16];
  snprintf(sBuf, sizeof(sBuf), "%.1f km/h", smoothedSpeed);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);   // transparent-ish
  tft.setTextPadding(0);
  tft.drawString(sBuf, SCREEN_W / 2, by + bh / 2, 4);
}

// ================================================================
//  DISPLAY - update all sections
// ================================================================
void updateDisplay() {
  updateSpeedDisplay();
  updateTripDisplay();
  updateTotalDisplay();
  updateOutputDisplay();
  updateSpeedBar();
}

// ================================================================
//  HANDLE BUTTON PRESSES
// ================================================================
void handleButtons() {
  unsigned long now = millis();

  bool rs = digitalRead(BTN_TRIP_RESET);
  if (rs == LOW && lastResetState == HIGH && (now - lastResetTime) > DEBOUNCE_MS) {
    tripDistance = 0;
    prevTrip    = -999;
    lastResetTime = now;
    Serial.println("Trip reset");
  }
  lastResetState = rs;

  bool ss = digitalRead(BTN_START_STOP);
  if (ss == LOW && lastStartStopState == HIGH && (now - lastStartStopTime) > DEBOUNCE_MS) {
    pulseActive = !pulseActive;
    prevActive  = !pulseActive;
    lastStartStopTime = now;
    Serial.print("Pulse: ");
    Serial.println(pulseActive ? "ACTIVE" : "STOPPED");
  }
  lastStartStopState = ss;
}

// ================================================================
//  SAVE TOTAL DISTANCE
// ================================================================
void saveTotalDistance() {
  prefs.putFloat("totalDist", totalDistance);
}

// ================================================================
//  SPLASH SCREEN
// ================================================================
void showSplash() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_SPEED);
  tft.drawString("TAXIMETER", SCREEN_W / 2, 80, 4);
  tft.drawString("PULSE GENERATOR", SCREEN_W / 2, 120, 4);

  tft.setTextColor(COLOR_LABEL);
  tft.drawString("CANM8 CANNECT PULSE Compatible", SCREEN_W / 2, 170, 2);

  char calBuf[32];
  snprintf(calBuf, sizeof(calBuf), "k = %ld pulses/km", PULSES_PER_KM);
  tft.drawString(calBuf, SCREEN_W / 2, 200, 2);

  char modeBuf[24];
  if (PULSE_MODE == 1)
    snprintf(modeBuf, sizeof(modeBuf), "Mode: 1 Hz/MPH (std)");
  else
    snprintf(modeBuf, sizeof(modeBuf), "Mode: %d Hz/MPH", PULSE_MODE);
  tft.drawString(modeBuf, SCREEN_W / 2, 225, 2);

  tft.setTextColor(COLOR_DIVIDER);
  tft.drawString("MSP4021 4.0\" ST7796 480x320", SCREEN_W / 2, 270, 1);

  delay(2500);
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Taximeter Pulse Generator ===");
  Serial.print("CANM8 mode: ");
  Serial.print(PULSE_MODE);
  Serial.print("x  (k=");
  Serial.print(PULSES_PER_KM);
  Serial.println(" pulses/km)");

  // ---- Disable built-in TFT (T-Display boards) ----
  pinMode(BUILTIN_TFT_CS, OUTPUT);
  digitalWrite(BUILTIN_TFT_CS, HIGH);

  // ---- GPIO ----
  pinMode(PULSE_PIN, OUTPUT);
  digitalWrite(PULSE_PIN, INVERT_OUTPUT ? HIGH : LOW);
  pinMode(POT_PIN, INPUT);
  pinMode(BTN_TRIP_RESET, INPUT_PULLUP);
  pinMode(BTN_START_STOP, INPUT_PULLUP);

  // ---- Display ----
  tft.init();
  tft.setRotation(1);          // Landscape 480x320
  tft.fillScreen(TFT_BLACK);

  showSplash();

  // ---- Load persistent total distance ----
  prefs.begin("taximeter", false);
  totalDistance = prefs.getFloat("totalDist", 0.0);
  Serial.print("Total distance loaded: ");
  Serial.print(totalDistance, 2);
  Serial.println(" km");

  // ---- Hardware timer for pulse generation ----
  setupPulseTimer();

  // ---- ADC ----
  analogReadResolution(12);
#if ESP_ARDUINO_VERSION_MAJOR < 3
  analogSetAttenuation(ADC_11db);
#endif

  // ---- Draw UI ----
  drawLayout();

  Serial.println("Ready - turn the potentiometer to set speed");
}

// ================================================================
//  MAIN LOOP
// ================================================================
void loop() {
  unsigned long now = millis();

  // Read & filter speed
  currentSpeed  = readSpeed();
  smoothedSpeed = smoothedSpeed * (1.0 - SPEED_SMOOTHING)
                + currentSpeed  * SPEED_SMOOTHING;
  if (smoothedSpeed < 0.3) smoothedSpeed = 0;

  // Pulse output
  updatePulseFrequency(smoothedSpeed);

  // Distance from pulse count
  updateDistance();

  // Buttons
  handleButtons();

  // Display
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    updateDisplay();
    lastDisplayUpdate = now;
  }

  // Periodic NVS save
  if (now - lastDistSave >= SAVE_INTERVAL_MS) {
    saveTotalDistance();
    lastDistSave = now;
  }

  delay(10);
}
