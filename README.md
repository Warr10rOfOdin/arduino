# Taximeter Pulse Generator

Desktop speed pulse simulator that outputs a square-wave signal identical to the
**CANM8 CANNECT PULSE** CAN-bus interface. Use it for taximeter calibration,
bench testing, or development without needing a vehicle.

## Features

- CANM8-compatible pulse output (1 Hz / 4 Hz / 10 Hz per MPH, selectable)
- Speed control via potentiometer (0–200 km/h)
- Built-in 1.14" colour TFT display showing:
  - Current speed (km/h)
  - Trip distance (resettable)
  - Total distance (persists across power cycles)
  - Output frequency (Hz) and status
- Start/Stop and Trip Reset buttons
- Optional 12 V level-shifted output via NPN transistor

## Hardware

### Required Components

| Component | Notes |
|-----------|-------|
| ESP32 with 1.14" ST7789 TFT | IdealSpark TM-ESP32-114LCDSP or TTGO T-Display |
| 10 kΩ potentiometer | Any linear (B10K) pot from your kit |
| 100 nF ceramic capacitor | Between pot wiper and GND (reduces ADC noise) |
| Jumper wires | For connections |
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
  ESP32 Pin    Connection
  ─────────    ──────────────────────────
  GPIO 36 (VP) Potentiometer wiper (+ 100nF cap to GND)
  GPIO 25      Pulse output (direct or via NPN to 12V)
  GPIO 0       Trip reset button (built-in BOOT)
  GPIO 35      Start/Stop button (built-in side button)
  3V3          Potentiometer high side
  GND          Potentiometer low side, button common, transistor emitter
```

## Software Setup

### Arduino IDE

1. **Install ESP32 board support**
   - File → Preferences → Additional Board Manager URLs, add:
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   - Tools → Board → Board Manager → search "esp32" → Install

2. **Install libraries** (Tools → Manage Libraries):
   - `Adafruit GFX Library`
   - `Adafruit ST7735 and ST7789 Library`
   - (dependency `Adafruit BusIO` will install automatically)

3. **Select board**:
   - Tools → Board → ESP32 Arduino → **ESP32 Dev Module**
   - (or "TTGO T-Display" if available in your board list)

4. **Upload**:
   - Open `taximeter_pulse_generator/taximeter_pulse_generator.ino`
   - Connect the ESP32 via USB
   - Click Upload

### Configuration

Edit `config.h` to adjust:

- **Pin assignments** – if your board differs from the defaults
- **`PULSE_MODE`** – set to `1`, `4`, or `10` to match the CANM8 variant
  your taximeter expects
- **`MAX_SPEED_KMH`** – maximum simulated speed
- **`INVERT_OUTPUT`** – set `true` when using the NPN transistor circuit
- **Display colours** – RGB565 colour values

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
2. The splash screen shows the active calibration mode
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
3. You should see a clean square wave
4. Verify frequency matches the expected value:
   - At 50 km/h standard mode → ~31.1 Hz
   - At 100 km/h standard mode → ~62.1 Hz

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Display is blank | Check `TFT_BL` pin (GPIO 4) is set correctly. Try different `tft.setRotation()` values |
| Speed reads 0 with pot turned | Verify pot wiring (3.3V / wiper / GND). Check `POT_PIN` matches your wiring |
| Jittery speed reading | Add 100 nF capacitor on pot wiper. Increase `ADC_SAMPLES` or decrease `SPEED_SMOOTHING` in config |
| No pulse output | Check `pulseActive` status on display. Press Start/Stop button. Verify `PULSE_PIN` |
| Taximeter not reading pulses | Check GND is shared. Try 12 V output circuit. Verify k-value matches taximeter calibration |
| Total distance not saving | NVS save happens every 30 s. Wait or reduce `SAVE_INTERVAL_MS` |
