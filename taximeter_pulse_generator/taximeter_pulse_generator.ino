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
//  Hardware: ESP32 with built-in 1.14" ST7789 TFT
//            + potentiometer for speed control
//            + optional NPN transistor for 12V output
//
//  Display shows:
//    - Current speed (km/h)
//    - Trip distance (km) - resettable
//    - Total distance (km) - persistent across power cycles
//    - Pulse frequency (Hz)
//    - Output status
//
//  Controls:
//    - Potentiometer: set simulated speed 0-200 km/h
//    - Button 1 (GPIO 0):  reset trip distance
//    - Button 2 (GPIO 35): start/stop pulse output
//
// ============================================================

#include "config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Preferences.h>

// ---- Display (software SPI for pin flexibility) ----
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// ---- NVS persistent storage ----
Preferences prefs;

// ---- Pulse generation (hardware timer) ----
hw_timer_t *pulseTimer = NULL;
volatile uint32_t pulseCount    = 0;
volatile bool     pulseState    = false;
bool              pulseActive   = true;
bool              timerRunning  = false;

// ---- Speed & distance ----
float    currentSpeed   = 0;       // Raw speed from pot (km/h)
float    smoothedSpeed  = 0;       // Filtered speed (km/h)
float    tripDistance    = 0;       // Resettable trip (km)
float    totalDistance   = 0;       // Persistent total (km)
uint32_t lastPulseCount = 0;       // For distance delta calculation

// ---- Timing ----
unsigned long lastDisplayUpdate = 0;
unsigned long lastDistSave      = 0;

// ---- Previous display values (skip redraws when unchanged) ----
int   prevSpeedWhole = -1;
int   prevSpeedFrac  = -1;
float prevTrip       = -999;
float prevTotal      = -999;
float prevFreq       = -999;
bool  prevActive     = !pulseActive;   // Force first draw

// ---- Button debounce ----
bool          lastResetState     = HIGH;
bool          lastStartStopState = HIGH;
unsigned long lastResetTime      = 0;
unsigned long lastStartStopTime  = 0;

// ============================================================
//  TIMER ISR - toggles the pulse output pin
// ============================================================
void IRAM_ATTR onPulseTimer() {
  pulseState = !pulseState;

  bool level = INVERT_OUTPUT ? !pulseState : pulseState;

  if (level) {
    GPIO.out_w1ts = ((uint32_t)1 << PULSE_PIN);
  } else {
    GPIO.out_w1tc = ((uint32_t)1 << PULSE_PIN);
  }

  // Count completed cycles (one full HIGH+LOW = one pulse)
  if (pulseState) {
    pulseCount++;
  }
}

// ============================================================
//  TIMER HELPERS (ESP32 Arduino Core v2 / v3 compatible)
// ============================================================
void setupPulseTimer() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  pulseTimer = timerBegin(1000000);                    // 1 MHz tick
  timerAttachInterrupt(pulseTimer, &onPulseTimer);
#else
  pulseTimer = timerBegin(0, 80, true);                // Timer 0, prescaler 80 → 1 µs tick
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
  pulseState  = false;
  timerRunning = false;
}

// ============================================================
//  UPDATE PULSE FREQUENCY
// ============================================================
void updatePulseFrequency(float speedKmh) {
  if (speedKmh < MIN_PULSE_SPEED || !pulseActive) {
    stopPulseTimer();
    return;
  }

  // CANM8: freq = speed_mph * PULSE_MODE
  // speed_mph = speed_kmh / 1.609344
  float freqHz = (speedKmh / 1.609344) * PULSE_MODE;

  // Half-period in microseconds (timer fires on each edge)
  uint64_t halfPeriodUs = (uint64_t)(500000.0 / freqHz);
  if (halfPeriodUs < 10) halfPeriodUs = 10;   // Safety floor

  startPulseTimer(halfPeriodUs);
}

// ============================================================
//  READ SPEED FROM POTENTIOMETER
// ============================================================
float readSpeed() {
  // Multi-sample average to reduce ESP32 ADC noise
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(POT_PIN);
  }
  int rawAdc = sum / ADC_SAMPLES;

  // Dead zone at bottom end
  if (rawAdc < ADC_DEADZONE) return 0;

  // Linear map to speed range
  float speed = (float)(rawAdc - ADC_DEADZONE) / (4095.0 - ADC_DEADZONE) * MAX_SPEED_KMH;
  return constrain(speed, 0, MAX_SPEED_KMH);
}

// ============================================================
//  UPDATE DISTANCE COUNTERS
// ============================================================
void updateDistance() {
  uint32_t current   = pulseCount;
  uint32_t newPulses = current - lastPulseCount;
  lastPulseCount     = current;

  if (newPulses > 0) {
    float km = (float)newPulses / PULSES_PER_KM;
    tripDistance  += km;
    totalDistance += km;
  }
}

// ============================================================
//  DRAW STATIC DISPLAY LAYOUT (called once)
// ============================================================
void drawLayout() {
  tft.fillScreen(ST77XX_BLACK);

  // ---- Title bar ----
  tft.fillRect(0, 0, 135, 28, COLOR_HEADER_BG);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_HEADER_TEXT);
  tft.setCursor(6, 4);
  tft.print("TAXIMETER  PULSE");
  tft.setCursor(30, 16);
  tft.print("GENERATOR");

  // ---- Speed unit label ----
  tft.setTextSize(2);
  tft.setTextColor(COLOR_LABEL);
  tft.setCursor(40, 88);
  tft.print("km/h");

  // ---- Dividers and section labels ----
  tft.drawFastHLine(4, 110, 127, COLOR_DIVIDER);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_LABEL);
  tft.setCursor(4, 114);
  tft.print("TRIP");

  tft.drawFastHLine(4, 152, 127, COLOR_DIVIDER);

  tft.setCursor(4, 156);
  tft.print("TOTAL");

  tft.drawFastHLine(4, 194, 127, COLOR_DIVIDER);

  // ---- Calibration info ----
  tft.setTextSize(1);
  tft.setTextColor(COLOR_LABEL);
  tft.setCursor(4, 228);
  char calStr[20];
  snprintf(calStr, sizeof(calStr), "k=%ld p/km", PULSES_PER_KM);
  tft.print(calStr);
}

// ============================================================
//  UPDATE DISPLAY (partial redraws for efficiency)
// ============================================================
void updateDisplay() {
  float freqHz = 0;
  if (smoothedSpeed >= MIN_PULSE_SPEED && pulseActive) {
    freqHz = (smoothedSpeed / 1.609344) * PULSE_MODE;
  }

  int sWhole = (int)smoothedSpeed;
  int sFrac  = (int)((smoothedSpeed - sWhole) * 10);

  // ---- Speed (large) ----
  if (sWhole != prevSpeedWhole || sFrac != prevSpeedFrac) {
    tft.fillRect(0, 34, 135, 50, ST77XX_BLACK);
    tft.setTextSize(5);
    tft.setTextColor(COLOR_SPEED);

    char buf[8];
    snprintf(buf, sizeof(buf), "%3d.%d", sWhole, sFrac);

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((135 - w) / 2, 38);
    tft.print(buf);

    // Redraw unit label (overlaps cleared area boundary)
    tft.setTextSize(2);
    tft.setTextColor(COLOR_LABEL);
    tft.setCursor(40, 88);
    tft.print("km/h");

    prevSpeedWhole = sWhole;
    prevSpeedFrac  = sFrac;
  }

  // ---- Trip distance ----
  if (fabsf(tripDistance - prevTrip) > 0.0005) {
    tft.fillRect(0, 126, 135, 22, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TRIP);

    char buf[14];
    if (tripDistance < 100.0)
      snprintf(buf, sizeof(buf), "%7.3f km", tripDistance);
    else
      snprintf(buf, sizeof(buf), "%7.2f km", tripDistance);
    tft.setCursor(2, 128);
    tft.print(buf);

    prevTrip = tripDistance;
  }

  // ---- Total distance ----
  if (fabsf(totalDistance - prevTotal) > 0.005) {
    tft.fillRect(0, 168, 135, 22, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TOTAL);

    char buf[14];
    if (totalDistance < 1000.0)
      snprintf(buf, sizeof(buf), "%7.2f km", totalDistance);
    else
      snprintf(buf, sizeof(buf), "%7.1f km", totalDistance);
    tft.setCursor(2, 170);
    tft.print(buf);

    prevTotal = totalDistance;
  }

  // ---- Status indicator + frequency ----
  if (pulseActive != prevActive || fabsf(freqHz - prevFreq) > 0.08) {
    // Status line
    tft.fillRect(0, 198, 135, 14, ST77XX_BLACK);
    tft.setTextSize(1);

    if (pulseActive && smoothedSpeed >= MIN_PULSE_SPEED) {
      tft.fillCircle(10, 205, 4, COLOR_RUNNING);
      tft.setTextColor(COLOR_RUNNING);
      tft.setCursor(18, 201);
      tft.print("ACTIVE");
    } else if (!pulseActive) {
      tft.fillCircle(10, 205, 4, COLOR_STOPPED);
      tft.setTextColor(COLOR_STOPPED);
      tft.setCursor(18, 201);
      tft.print("STOPPED");
    } else {
      tft.fillCircle(10, 205, 4, COLOR_LABEL);
      tft.setTextColor(COLOR_LABEL);
      tft.setCursor(18, 201);
      tft.print("IDLE");
    }

    // Frequency
    tft.fillRect(0, 214, 100, 14, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_FREQ);

    char buf[10];
    if (freqHz < 0.1)
      snprintf(buf, sizeof(buf), "  0.0");
    else
      snprintf(buf, sizeof(buf), "%5.1f", freqHz);
    tft.setCursor(2, 214);
    tft.print(buf);

    tft.setTextSize(1);
    tft.setTextColor(COLOR_LABEL);
    tft.setCursor(66, 218);
    tft.print("Hz");

    prevActive = pulseActive;
    prevFreq   = freqHz;
  }
}

// ============================================================
//  HANDLE BUTTON PRESSES
// ============================================================
void handleButtons() {
  unsigned long now = millis();

  // ---- Trip reset button ----
  bool resetState = digitalRead(BTN_TRIP_RESET);
  if (resetState == LOW && lastResetState == HIGH && (now - lastResetTime) > DEBOUNCE_MS) {
    tripDistance = 0;
    prevTrip    = -999;     // Force display refresh
    lastResetTime = now;
    Serial.println("Trip reset");
  }
  lastResetState = resetState;

  // ---- Start / Stop button ----
  bool ssState = digitalRead(BTN_START_STOP);
  if (ssState == LOW && lastStartStopState == HIGH && (now - lastStartStopTime) > DEBOUNCE_MS) {
    pulseActive = !pulseActive;
    prevActive  = !pulseActive;   // Force display refresh
    lastStartStopTime = now;
    Serial.print("Pulse output: ");
    Serial.println(pulseActive ? "ACTIVE" : "STOPPED");
  }
  lastStartStopState = ssState;
}

// ============================================================
//  SAVE TOTAL DISTANCE TO NVS
// ============================================================
void saveTotalDistance() {
  prefs.putFloat("totalDist", totalDistance);
}

// ============================================================
//  SPLASH SCREEN
// ============================================================
void showSplash() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(COLOR_SPEED);
  tft.setCursor(4, 50);
  tft.print("TAXIMETER");
  tft.setCursor(20, 75);
  tft.print("PULSE");
  tft.setCursor(30, 100);
  tft.print("GEN");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_LABEL);
  tft.setCursor(8, 140);
  tft.print("CANM8 Compatible");

  char calStr[24];
  snprintf(calStr, sizeof(calStr), "k = %ld pulses/km", PULSES_PER_KM);
  tft.setCursor(8, 158);
  tft.print(calStr);

  tft.setCursor(8, 176);
  if (PULSE_MODE == 1)
    tft.print("Mode: 1 Hz/MPH (std)");
  else if (PULSE_MODE == 4)
    tft.print("Mode: 4 Hz/MPH");
  else
    tft.print("Mode: 10 Hz/MPH");

  delay(2500);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Taximeter Pulse Generator ===");
  Serial.print("CANM8 mode: ");
  Serial.print(PULSE_MODE);
  Serial.print("x  (k=");
  Serial.print(PULSES_PER_KM);
  Serial.println(" pulses/km)");

  // ---- GPIO ----
  pinMode(PULSE_PIN, OUTPUT);
  digitalWrite(PULSE_PIN, INVERT_OUTPUT ? HIGH : LOW);

  pinMode(POT_PIN, INPUT);
  pinMode(BTN_TRIP_RESET, INPUT_PULLUP);
  pinMode(BTN_START_STOP, INPUT_PULLUP);

  // ---- Backlight ----
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // ---- Display ----
  tft.init(135, 240);
  tft.setRotation(0);       // Portrait
  tft.fillScreen(ST77XX_BLACK);

  showSplash();

  // ---- Load persistent total distance ----
  prefs.begin("taximeter", false);
  totalDistance = prefs.getFloat("totalDist", 0.0);
  Serial.print("Loaded total distance: ");
  Serial.print(totalDistance, 2);
  Serial.println(" km");

  // ---- Hardware timer for pulse generation ----
  setupPulseTimer();

  // ---- ADC ----
  analogReadResolution(12);
#if ESP_ARDUINO_VERSION_MAJOR < 3
  analogSetAttenuation(ADC_11db);   // Full 0-3.3 V range
#endif

  // ---- Draw UI ----
  drawLayout();

  Serial.println("Ready - turn the potentiometer to set speed");
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // ---- Read speed ----
  currentSpeed  = readSpeed();
  smoothedSpeed = smoothedSpeed * (1.0 - SPEED_SMOOTHING)
                + currentSpeed  * SPEED_SMOOTHING;
  if (smoothedSpeed < 0.3) smoothedSpeed = 0;

  // ---- Update pulse output ----
  updatePulseFrequency(smoothedSpeed);

  // ---- Accumulate distance from pulse count ----
  updateDistance();

  // ---- Buttons ----
  handleButtons();

  // ---- Display refresh ----
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    updateDisplay();
    lastDisplayUpdate = now;
  }

  // ---- Periodic NVS save ----
  if (now - lastDistSave >= SAVE_INTERVAL_MS) {
    saveTotalDistance();
    lastDistSave = now;
  }

  delay(10);
}
