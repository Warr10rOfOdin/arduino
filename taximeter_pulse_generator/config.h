#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
//  TAXIMETER PULSE GENERATOR - Configuration
// ============================================================
//  Board: ESP32 with ST7789 1.14" TFT
//         (IdealSpark TM-ESP32-114LCDSP / TTGO T-Display)
// ============================================================

// -- Display (ST7789 135x240) --------------------------------
#define TFT_CS    5
#define TFT_DC    16
#define TFT_RST   23
#define TFT_MOSI  19
#define TFT_SCLK  18
#define TFT_BL    4

// -- Speed Control (Potentiometer) ---------------------------
// Use an input-only ADC1 pin (GPIO 36/VP recommended)
// Wire: 3.3V -> pot outer leg, GND -> other outer leg,
//        wiper (middle) -> this pin
// Add 100nF capacitor between wiper and GND for stability
#define POT_PIN   36

// -- Pulse Output --------------------------------------------
// Square wave output to taximeter
// For direct 3.3V output: connect directly
// For 12V output: use NPN transistor circuit (see README)
#define PULSE_PIN 25

// -- Buttons (active LOW, internal pull-up enabled) ----------
// GPIO 0  = BOOT button (built-in on most ESP32 boards)
// GPIO 35 = Side button (built-in on TTGO T-Display)
// If your board lacks built-in buttons, wire external ones
// between the GPIO pin and GND
#define BTN_TRIP_RESET  0
#define BTN_START_STOP  35

// ============================================================
//  CANM8 PULSE CALIBRATION
// ============================================================
// The CANM8 CANNECT PULSE outputs: 1 Hz per 1 MPH (standard)
// Also available: 4 Hz/MPH and 10 Hz/MPH variants
//
// Pulse modes:
//   1 = Standard   (1 Hz/MPH,  k=2237  pulses/km)
//   4 = 4x mode    (4 Hz/MPH,  k=8948  pulses/km)
//  10 = 10x mode   (10 Hz/MPH, k=22369 pulses/km)
//
// Set this to match your taximeter's calibration
#define PULSE_MODE      1

// Calculated constants (do not change)
// 1 MPH = 1.609344 km/h
// At 1 km/h: freq = 1/1.609344 Hz, over 1 hour = 3600/1.609344 = 2236.94 pulses
#define PULSES_PER_KM   (2237L * PULSE_MODE)

// ============================================================
//  SPEED SETTINGS
// ============================================================
#define MAX_SPEED_KMH   200
#define MIN_PULSE_SPEED 0.5     // Below this speed (km/h), pulse stops
#define ADC_DEADZONE    50      // ADC values below this = 0 km/h
#define ADC_SAMPLES     16      // Number of ADC samples to average
#define SPEED_SMOOTHING 0.12    // EMA filter factor (lower = smoother)

// ============================================================
//  TIMING
// ============================================================
#define DISPLAY_UPDATE_MS   200     // Display refresh interval
#define SAVE_INTERVAL_MS    30000   // Save total distance to NVS every 30s
#define DEBOUNCE_MS         250     // Button debounce time

// ============================================================
//  DISPLAY COLORS (RGB565)
// ============================================================
#define COLOR_HEADER_BG     0x000F  // Dark blue
#define COLOR_HEADER_TEXT   0xFFFF  // White
#define COLOR_SPEED         0x07E0  // Green
#define COLOR_LABEL         0x8410  // Gray
#define COLOR_TRIP          0x07FF  // Cyan
#define COLOR_TOTAL         0xFD20  // Orange
#define COLOR_FREQ          0xFFE0  // Yellow
#define COLOR_RUNNING       0x07E0  // Green
#define COLOR_STOPPED       0xF800  // Red
#define COLOR_DIVIDER       0x2104  // Dark gray

// ============================================================
//  OUTPUT INVERSION
// ============================================================
// Set to true if using an NPN transistor for 12V output
// (transistor inverts the signal, so we pre-invert in software)
#define INVERT_OUTPUT   false

#endif
