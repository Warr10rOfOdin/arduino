// ============================================================
//  TFT_eSPI User Setup for MSP4021 4.0" ST7796 SPI TFT
//  with ESP32 (IdealSpark TM-ESP32-114LCDSP / TTGO T-Display)
// ============================================================
//
//  INSTALLATION:
//  Copy this file to your TFT_eSPI library folder and rename
//  it to User_Setup.h, replacing the existing one.
//
//  Typical locations:
//    Windows:  Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
//    macOS:    ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
//    Linux:    ~/Arduino/libraries/TFT_eSPI/User_Setup.h
//
// ============================================================

#define USER_SETUP_ID 100

// ---- Display driver ----
#define ST7796_DRIVER

#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// ---- ESP32 SPI pin assignments ----
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_MISO  19
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   4

// ---- Touch (XPT2046, shares SPI bus) ----
#define TOUCH_CS  33

// ---- Fonts to load ----
#define LOAD_GLCD    // Font 1  - 8px Adafruit
#define LOAD_FONT2   // Font 2  - 16px
#define LOAD_FONT4   // Font 4  - 26px
#define LOAD_FONT6   // Font 6  - 24x48 7-segment (thin)
#define LOAD_FONT7   // Font 7  - 48x48 7-segment
#define LOAD_FONT8   // Font 8  - 75px large digits
#define LOAD_GFXFF   // FreeFonts
#define SMOOTH_FONT

// ---- SPI clock speeds ----
#define SPI_FREQUENCY        40000000    // 40 MHz display
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000    // 2.5 MHz touch
