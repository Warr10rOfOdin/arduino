// ============================================================
//  TAXIMETER PULSE GENERATOR — Tinkercad Version
//  CANM8 CANNECT PULSE Compatible (1 Hz = 1 MPH)
// ============================================================
//
//  Tinkercad-ready project using only components available
//  in the Tinkercad Circuits simulator.
//
//  Hardware:
//    - Arduino Uno R3
//    - 16x2 LCD (parallel, HD44780)
//    - 10K potentiometer (speed control)
//    - 10K potentiometer (LCD contrast)
//    - 2x push-buttons (trip reset, start/stop)
//    - LED on pin 13 (pulse activity indicator)
//
//  Pulse output on Pin 9 (OC1A) using Timer1 CTC mode
//  for hardware-accurate square wave generation.
//
//  Tinkercad link tip: add an Oscilloscope component and
//  connect its probe to Pin 9 to see the waveform live.
//
// ============================================================
//
//  WIRING (match this in Tinkercad):
//
//  LCD 16x2 (parallel):
//    VSS → GND          VDD → 5V
//    V0  → Contrast pot wiper (or GND for max contrast)
//    RS  → Pin 12       RW  → GND
//    EN  → Pin 11       D4  → Pin 5
//    D5  → Pin 4        D6  → Pin 3
//    D7  → Pin 2        A(+)→ 5V via 220Ω
//    K(-) → GND
//
//  Speed potentiometer:
//    One outer leg → 5V
//    Other outer   → GND
//    Wiper (middle)→ A0
//
//  Buttons (active LOW, internal pull-up):
//    Trip Reset  → Pin 7 to GND (momentary)
//    Start/Stop  → Pin 8 to GND (momentary)
//
//  Pulse output:
//    Pin 9 (OC1A) → Taximeter speed input / Oscilloscope
//
//  LED indicator:
//    Pin 13 (built-in LED) — lights when pulse is active
//
// ============================================================

#include <LiquidCrystal.h>
#include <EEPROM.h>

// ---- Pin definitions ----
#define LCD_RS    12
#define LCD_EN    11
#define LCD_D4    5
#define LCD_D5    4
#define LCD_D6    3
#define LCD_D7    2

#define POT_PIN   A0
#define PULSE_PIN 9       // OC1A — Timer1 hardware output
#define LED_PIN   13
#define BTN_RESET 7
#define BTN_START 8

// ---- CANM8 calibration ----
// Pulse modes: 1 = standard (1 Hz/MPH), 4 = 4x, 10 = 10x
#define PULSE_MODE      1
#define PULSES_PER_KM   (2237L * PULSE_MODE)

// ---- Speed settings ----
#define MAX_SPEED       200
#define MIN_PULSE_SPEED 1.0     // Below this, pulse stops
#define ADC_DEADZONE    10
#define ADC_SAMPLES     8
#define SMOOTHING       0.12

// ---- Timing ----
#define DISPLAY_INTERVAL  200   // ms between display updates
#define SAVE_INTERVAL     60000 // ms between EEPROM saves
#define DEBOUNCE_MS       250

// ---- Timer1 constants ----
// Prescaler 256: timer clock = 16 MHz / 256 = 62 500 Hz
// Square wave freq = 62500 / (2 * (OCR1A + 1))
// So: OCR1A = 31250 / freq - 1
#define T1_BASE  31250.0

// ---- EEPROM address ----
#define EEPROM_ADDR  0    // Store totalDistance as float (4 bytes)

// ============================================================

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ---- State ----
volatile uint32_t halfCycles    = 0;    // ISR counter
uint32_t          lastHalfCycles = 0;
float    smoothedSpeed  = 0;
float    tripDistance    = 0;
float    totalDistance   = 0;
bool     pulseActive    = true;
bool     timerRunning   = false;
uint16_t currentOCR     = 0;

// ---- Timing ----
unsigned long lastDisplayUpdate = 0;
unsigned long lastSave          = 0;

// ---- Buttons ----
bool          lastResetState = HIGH;
bool          lastStartState = HIGH;
unsigned long lastResetTime  = 0;
unsigned long lastStartTime  = 0;

// ============================================================
//  Timer1 Compare Match ISR — counts half-cycles for distance
// ============================================================
ISR(TIMER1_COMPA_vect) {
  halfCycles++;
}

// ============================================================
//  Start / update pulse output
// ============================================================
void startPulse(uint16_t ocrVal) {
  if (timerRunning && ocrVal == currentOCR) return;

  cli();
  TCCR1A = (1 << COM1A0);                   // Toggle OC1A on match
  TCCR1B = (1 << WGM12) | (1 << CS12);      // CTC mode, prescaler 256
  if (!timerRunning || abs((int)ocrVal - (int)currentOCR) > 1) {
    OCR1A = ocrVal;
    if (TCNT1 > ocrVal) TCNT1 = 0;          // Prevent missed match
  }
  TIMSK1 = (1 << OCIE1A);
  sei();

  currentOCR   = ocrVal;
  timerRunning = true;
}

void stopPulse() {
  if (!timerRunning) return;
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TIMSK1 = 0;
  TCNT1  = 0;
  sei();
  digitalWrite(PULSE_PIN, LOW);
  timerRunning = false;
  currentOCR   = 0;
}

// ============================================================
//  Read speed from potentiometer
// ============================================================
float readSpeed() {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) sum += analogRead(POT_PIN);
  int raw = sum / ADC_SAMPLES;
  if (raw < ADC_DEADZONE) return 0;
  float spd = (float)(raw - ADC_DEADZONE) / (1023.0 - ADC_DEADZONE) * MAX_SPEED;
  return constrain(spd, 0, MAX_SPEED);
}

// ============================================================
//  Update distance from pulse count
// ============================================================
void updateDistance() {
  uint32_t cur;
  cli();
  cur = halfCycles;
  sei();

  uint32_t delta = cur - lastHalfCycles;
  lastHalfCycles = cur;

  if (delta > 0) {
    // Two half-cycles = one complete pulse
    float km = (delta / 2.0) / PULSES_PER_KM;
    tripDistance  += km;
    totalDistance += km;
  }
}

// ============================================================
//  EEPROM load / save
// ============================================================
float loadTotal() {
  float val;
  EEPROM.get(EEPROM_ADDR, val);
  if (isnan(val) || val < 0 || val > 999999) return 0;
  return val;
}

void saveTotal() {
  float stored;
  EEPROM.get(EEPROM_ADDR, stored);
  // Only write if changed (reduces EEPROM wear)
  if (abs(stored - totalDistance) > 0.01)
    EEPROM.put(EEPROM_ADDR, totalDistance);
}

// ============================================================
//  Update LCD display
// ============================================================
//  Line 1: "120.5kph  62.1Hz"   (speed + frequency)
//  Line 2: "T:12.35*A:1234.5"   (trip + status + total)
// ============================================================
void updateDisplay() {
  float freqHz = 0;
  if (smoothedSpeed >= MIN_PULSE_SPEED && pulseActive)
    freqHz = (smoothedSpeed / 1.609344) * PULSE_MODE;

  // ---- Line 1: speed + frequency ----
  char sBuf[7], fBuf[7], line1[17];
  dtostrf(smoothedSpeed, 5, 1, sBuf);   // "120.5" or "  5.0"
  dtostrf(freqHz, 5, 1, fBuf);          // " 62.1" or "  0.0"
  sprintf(line1, "%skph %sHz", sBuf, fBuf);
  lcd.setCursor(0, 0);
  lcd.print(line1);

  // ---- Line 2: trip + status + total ----
  // Trip: 5 chars
  char tBuf[8];
  if (tripDistance < 100)
    dtostrf(tripDistance, 5, 2, tBuf);
  else if (tripDistance < 1000)
    dtostrf(tripDistance, 5, 1, tBuf);
  else
    dtostrf(tripDistance, 5, 0, tBuf);

  // Total: 6 chars
  char oBuf[8];
  if (totalDistance < 1000)
    dtostrf(totalDistance, 6, 1, oBuf);
  else
    dtostrf(totalDistance, 6, 0, oBuf);

  // Status: '*' = active, ' ' = stopped
  char line2[17];
  sprintf(line2, "T:%s%cA:%s", tBuf, pulseActive ? '*' : ' ', oBuf);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// ============================================================
//  Handle buttons
// ============================================================
void handleButtons() {
  unsigned long now = millis();

  // Trip reset
  bool rs = digitalRead(BTN_RESET);
  if (rs == LOW && lastResetState == HIGH && (now - lastResetTime) > DEBOUNCE_MS) {
    tripDistance = 0;
    lastResetTime = now;
    Serial.println("Trip reset");
  }
  lastResetState = rs;

  // Start / Stop
  bool ss = digitalRead(BTN_START);
  if (ss == LOW && lastStartState == HIGH && (now - lastStartTime) > DEBOUNCE_MS) {
    pulseActive = !pulseActive;
    lastStartTime = now;
    Serial.print("Pulse: ");
    Serial.println(pulseActive ? "ACTIVE" : "STOPPED");
  }
  lastStartState = ss;
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  Serial.begin(9600);
  Serial.println(F("=== Taximeter Pulse Generator (Tinkercad) ==="));
  Serial.print(F("CANM8 mode: "));
  Serial.print(PULSE_MODE);
  Serial.print(F("x  k="));
  Serial.print(PULSES_PER_KM);
  Serial.println(F(" pulses/km"));

  // Pins
  pinMode(PULSE_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_START, INPUT_PULLUP);

  // LCD
  lcd.begin(16, 2);
  lcd.print(F("TAXIMETER PULSE"));
  lcd.setCursor(0, 1);
  lcd.print(F("k="));
  lcd.print(PULSES_PER_KM);
  lcd.print(F(" p/km "));
  lcd.print(PULSE_MODE);
  lcd.print(F("x"));
  delay(2000);
  lcd.clear();

  // Load persistent total distance
  totalDistance = loadTotal();
  Serial.print(F("Total loaded: "));
  Serial.print(totalDistance, 1);
  Serial.println(F(" km"));

  Serial.println(F("Ready — turn pot to set speed"));
}

// ============================================================
//  Main loop
// ============================================================
void loop() {
  unsigned long now = millis();

  // ---- Read & smooth speed ----
  float rawSpeed = readSpeed();
  smoothedSpeed  = smoothedSpeed * (1.0 - SMOOTHING)
                 + rawSpeed * SMOOTHING;
  if (smoothedSpeed < 0.3) smoothedSpeed = 0;

  // ---- Pulse output ----
  if (smoothedSpeed >= MIN_PULSE_SPEED && pulseActive) {
    float freqHz  = (smoothedSpeed / 1.609344) * PULSE_MODE;
    uint16_t ocr  = (uint16_t)(T1_BASE / freqHz) - 1;
    if (ocr < 1) ocr = 1;
    startPulse(ocr);
    digitalWrite(LED_PIN, HIGH);
  } else {
    stopPulse();
    digitalWrite(LED_PIN, LOW);
  }

  // ---- Distance ----
  updateDistance();

  // ---- Buttons ----
  handleButtons();

  // ---- Display ----
  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = now;
  }

  // ---- Periodic EEPROM save ----
  if (now - lastSave >= SAVE_INTERVAL) {
    saveTotal();
    lastSave = now;
  }

  delay(10);
}
