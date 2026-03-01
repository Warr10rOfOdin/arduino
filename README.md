# Taximeter Pulse Generator

Desktop speed pulse simulator that outputs a square-wave signal identical to the
**CANM8 CANNECT PULSE** CAN-bus interface. Use it for taximeter calibration,
bench testing, or development without needing a vehicle.

## Features

- CANM8-compatible pulse output (1 Hz / 4 Hz / 10 Hz per MPH, selectable)
- Speed control via potentiometer (0–200 km/h)
- **4.0" colour TFT display** (MSP4021, ST7796, 480x320 landscape) showing:
  - Large 7-segment speed readout with ghost segments (km/h)
  - Trip distance (resettable)
  - Total distance (persists across power cycles)
  - Pulse frequency (Hz) and output status
  - Colour-coded speed bar graph
- Start/Stop and Trip Reset buttons
- Optional 12 V level-shifted output via NPN transistor
- Touch-capable display (for future expansion)

## Hardware

### Required Components

| Component | Notes |
|-----------|-------|
| ESP32 dev board | IdealSpark TM-ESP32-114LCDSP, TTGO T-Display, or any ESP32 |
| MSP4021 4.0" SPI TFT | ST7796 driver, 320x480, SPI with touch |
| 10 kΩ potentiometer | Any linear (B10K) pot |
| 100 nF ceramic capacitor | Between pot wiper and GND (reduces ADC noise) |
| Jumper wires | For all connections |
| USB-C cable | Power and programming |

### Optional – 12 V Pulse Output

| Component | Notes |
|-----------|-------|
| NPN transistor | 2N2222, BC547, or similar from your component kit |
| 1 kΩ resistor | Base resistor (ESP32 GPIO → transistor base) |
| 4.7 kΩ resistor | Pull-up resistor (12 V → collector) |
| 12 V power supply | Your AC-DC 12 V module or DC barrel jack supply |
| DC5521 connector | For 12 V input |

## Wiring

### MSP4021 Display → ESP32

```
  MSP4021 Pin     ESP32 GPIO     Notes
  ───────────     ──────────     ──────────────────
  VCC             3V3            Display power
  GND             GND            Common ground
  CS              GPIO 15        Display chip select
  RESET           GPIO 4         Display reset
  DC/RS           GPIO 2         Data / Command
  SDI (MOSI)      GPIO 23        SPI data in
  SCK             GPIO 18        SPI clock
  LED             3V3            Backlight (always on)
  SDO (MISO)      GPIO 19        SPI data out (touch readback)
  T_CLK           GPIO 18        Touch clock (shared with SCK)
  T_CS            GPIO 33        Touch chip select
  T_DIN           GPIO 23        Touch data in (shared with MOSI)
  T_DO            GPIO 19        Touch data out (shared with MISO)
  T_IRQ           (not connected) Touch interrupt (future use)
```

> **T-Display / IdealSpark board users:** GPIO 5 (the built-in ST7789 CS)
> is automatically held HIGH in software so the on-board display is
> disabled and doesn't interfere with SPI traffic.

### Potentiometer (Speed Control)

```
  3.3V ──────┐
             │
         ┌───┴───┐
         │  POT  │  10 kΩ linear
         │ (B10K)│
         └───┬───┘
             │
             ├────── GPIO 36 (VP)
             │
          [100nF]
             │
  GND ───────┘
```

### Pulse Output – Direct 3.3 V

For taximeters that accept 3.3 V logic-level input:

```
  GPIO 25 ──────── Taximeter speed input
  GND ──────────── Taximeter GND
```

### Pulse Output – 12 V (NPN Transistor)

For taximeters requiring 12 V signal (CANM8 standard):

```
  12V ───[4.7kΩ]───┬─── Output to taximeter
                    │
                  [C]
  GPIO 25 ─[1kΩ]─[B]  NPN (2N2222 / BC547)
                  [E]
                    │
  GND ──────────────┘

  (Also connect taximeter GND to this GND)
```

> **Note:** The NPN transistor inverts the signal. Set `INVERT_OUTPUT` to
> `true` in `config.h` to compensate.

### Buttons

The ESP32 boards listed above have two built-in buttons:

| Button | GPIO | Function |
|--------|------|----------|
| BOOT   | 0    | Reset trip distance |
| Side   | 35   | Start / Stop pulse output |

If your board lacks built-in buttons, wire momentary push-buttons between
the GPIO pin and GND (internal pull-ups are enabled in software).

### Full Wiring Summary

```
  ESP32 Pin      Connection
  ─────────      ──────────────────────────────────
  GPIO 23        Display MOSI + Touch DIN
  GPIO 18        Display SCK  + Touch CLK
  GPIO 19        Display MISO + Touch DO
  GPIO 15        Display CS
  GPIO 2         Display DC
  GPIO 4         Display RESET
  GPIO 33        Touch CS
  GPIO 5         (held HIGH — disables built-in TFT)
  GPIO 36 (VP)   Potentiometer wiper (+ 100nF cap to GND)
  GPIO 25        Pulse output (direct or via NPN to 12V)
  GPIO 0         Trip reset button (built-in BOOT)
  GPIO 35        Start/Stop button (built-in side button)
  3V3            Display VCC, Display LED, Pot high side
  GND            Display GND, Pot low, buttons, transistor
```

## Software Setup

### Arduino IDE

1. **Install ESP32 board support**
   - File → Preferences → Additional Board Manager URLs, add:
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   - Tools → Board → Board Manager → search "esp32" → Install

2. **Install TFT_eSPI library** (Tools → Manage Libraries):
   - Search for `TFT_eSPI` by Bodmer → Install

3. **Configure TFT_eSPI for the ST7796 display**:
   - Find the file `TFT_User_Setup.h` in the project folder
   - Copy it to the TFT_eSPI library directory, renaming it to `User_Setup.h`:
     - **Windows:** `Documents\Arduino\libraries\TFT_eSPI\User_Setup.h`
     - **macOS:** `~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`
     - **Linux:** `~/Arduino/libraries/TFT_eSPI/User_Setup.h`
   - This **replaces** the existing `User_Setup.h` in that folder

4. **Select board**:
   - Tools → Board → ESP32 Arduino → **ESP32 Dev Module**

5. **Upload**:
   - Open `taximeter_pulse_generator/taximeter_pulse_generator.ino`
   - Connect the ESP32 via USB
   - Click Upload

### Configuration

Edit `config.h` to adjust:

- **`PULSE_MODE`** – set to `1`, `4`, or `10` to match the CANM8 variant
  your taximeter expects
- **`MAX_SPEED_KMH`** – maximum simulated speed (default 200)
- **`INVERT_OUTPUT`** – set `true` when using the NPN transistor circuit
- **Pin assignments** – if your board differs from the defaults
- **Display colours** – RGB565 colour values
- **Layout constants** – panel sizes and positions

## Display Layout

```
  ┌────────────────────────────────────────────────────┐
  │ TAXIMETER PULSE GENERATOR            ● ACTIVE      │
  ├──────────────────────┬─────────────────────────────┤
  │                      │ ┌ TRIP ───────────────────┐ │
  │                      │ │  45.678 km              │ │
  │      120.5           │ └─────────────────────────┘ │
  │                      │ ┌ TOTAL ──────────────────┐ │
  │      km/h            │ │  1234.56 km             │ │
  │                      │ └─────────────────────────┘ │
  │                      │ ┌ OUTPUT ─────────────────┐ │
  │                      │ │  74.9 Hz                │ │
  │                      │ │  k=2237 p/km   1x mode  │ │
  │                      │ └─────────────────────────┘ │
  ├──────────────────────┴─────────────────────────────┤
  │ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░  120.5 km/h          │
  └────────────────────────────────────────────────────┘
```

- **Left panel:** Large 7-segment speed with ghost segments (digital readout effect)
- **Right panel:** Trip, Total, and Output frequency in rounded panels
- **Bottom bar:** Colour-coded speed bar (green → yellow → red)
- **Header:** Title and live status indicator

## CANM8 Pulse Specifications

The CANM8 CANNECT PULSE outputs a 12 V rectangular (square) wave:

| Parameter | Standard | 4x | 10x |
|-----------|----------|----|-----|
| Frequency at 1 MPH | 1 Hz | 4 Hz | 10 Hz |
| Frequency at 60 MPH | 60 Hz | 240 Hz | 600 Hz |
| Frequency at 100 km/h | 62.1 Hz | 248.5 Hz | 621.4 Hz |
| Pulses per km (k-value) | 2 237 | 8 948 | 22 369 |
| Duty cycle | ~50% | ~50% | ~50% |
| Voltage | 12 V | 12 V | 12 V |

The formula:  **frequency (Hz) = speed (km/h) / 1.609344 × PULSE_MODE**

## Usage

1. Power on the ESP32 via USB (or external 5 V)
2. The splash screen shows the active calibration mode and display info
3. Turn the potentiometer to set the desired speed
4. The pulse output begins immediately on GPIO 25
5. Connect the output to your taximeter's speed input
6. Press **BOOT** (GPIO 0) to reset the trip counter
7. Press the **side button** (GPIO 35) to start/stop the pulse

### Serial Monitor

Open the Arduino Serial Monitor at **115200 baud** for debug output
including speed changes, button presses, and calibration info.

## Testing with the Oscilloscope

Use your ZOYI ZT-702S to verify the output:

1. Connect oscilloscope probe to GPIO 25 (or the 12 V output)
2. Set timebase to ~10 ms/div for speeds around 50 km/h
3. You should see a clean square wave at ~50% duty cycle
4. Verify frequency matches the expected value:
   - At 50 km/h standard mode → ~31.1 Hz
   - At 100 km/h standard mode → ~62.1 Hz

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Display is white/blank | Verify all 6 display wires (MOSI, SCK, CS, DC, RST, LED). Check `User_Setup.h` was copied correctly |
| Colours are inverted (red/blue swapped) | Add `#define TFT_RGB_ORDER TFT_RGB` to `TFT_User_Setup.h` |
| Display artefacts/noise | Reduce `SPI_FREQUENCY` to `27000000` in `TFT_User_Setup.h`. Use shorter wires |
| Speed reads 0 with pot turned | Verify pot wiring (3.3V / wiper / GND). Check `POT_PIN` matches your wiring |
| Jittery speed reading | Add 100 nF capacitor on pot wiper. Increase `ADC_SAMPLES` or decrease `SPEED_SMOOTHING` in config |
| No pulse output | Check `pulseActive` status on display. Press Start/Stop button. Verify `PULSE_PIN` |
| Taximeter not reading pulses | Check GND is shared. Try 12 V output circuit. Verify k-value matches taximeter calibration |
| Total distance not saving | NVS save happens every 30 s. Wait or reduce `SAVE_INTERVAL_MS` |
| Built-in TFT flickers | The code disables GPIO 5 (built-in CS). Verify `BUILTIN_TFT_CS` is set to 5 |

## Project Files

```
taximeter_pulse_generator/
├── taximeter_pulse_generator.ino   Main sketch
├── config.h                        Pin definitions, calibration, colours
└── TFT_User_Setup.h                Copy to TFT_eSPI library as User_Setup.h
```
