# nyanBOX — Cheap Yellow Display (ESP32‑2432S028) port

A port of nyanBOX to the **CYD** ("Cheap Yellow Display") ESP32‑2432S028 board — a
2.8" ILI9341 240×320 SPI TFT with an XPT2046 resistive touchscreen and a classic
ESP32‑WROOM (4 MB flash, no PSRAM). No physical buttons and no OLED, so this port
adapts the UI and input while keeping the original nyanBOX feature set intact
(all app modules build unchanged).

Built as a new PlatformIO env — the original `nyanbox-main` / `hardware-test` envs
are untouched. Build with `pio run -e nyanbox-cyd`.

## What changed vs. stock nyanBOX

- **Radio pin remap** (`include/pindefs.h`): the 3× nRF24 share the CYD's SD‑card
  SPI bus (SCK18 / MISO19 / MOSI23, unchanged in code) and the CE/CSN pairs are
  moved to free pins — **R1 22/5, R2 27/17, R3 4/16** — because the stock pins
  collide with the TFT (15=CS, 2=DC). Sacrifices the microSD slot and the RGB LED.
  NeoPixel moved 14→0 (14 = TFT_SCLK).
- **Display bridge** (`include/cyd_u8g2_bridge.h`, `src/cyd_u8g2_bridge.cpp`): a
  `CydU8g2` subclass keeps the 128×64 U8g2 mono full‑buffer (zero I2C traffic) and
  blits it, letterboxed, onto the ILI9341 via TFT_eSPI. A build‑flag macro rewrites
  the SSD1306 type so all 42 UI files compile unchanged.
- **Touch input** (`src/touch_input.cpp`, `include/touch_input.h`): bit‑banged
  XPT2046 (software SPI, so it doesn't contend for HSPI=TFT / VSPI=radios).
  **Pressure‑gated** (not PENIRQ, which is unreliable on the CYD). **Self‑calibrating**
  — a 4‑corner tap routine on first boot auto‑detects axis swap/direction/range and
  stores it to EEPROM (hold a finger at power‑up to re‑run it). `nyanDigitalRead()`
  shims the five logical buttons onto touch.
- **Touch‑native menus** (`src/nyanBOX.ino`): tap a row to select, drag a left‑edge
  slider to scroll, a bottom BACK/LEVEL bar — no arrow reliance. Apps auto‑switch to
  a 5‑zone on‑screen D‑pad (● center exits). Menu vs. app mode is an explicit flag
  toggled at the `runApp()` boundary (can't use `currentState` — `runApp()` blocks).
- **Boot robustness**: the radio‑init loop no longer hangs forever when a radio is
  absent (it skips missing radios), and the backlight (GPIO21) is driven explicitly —
  both were causes of a dark screen on the CYD.

## Wiring notes (nRF24)

3× nRF24 at PA_MAX will brown out the CYD's onboard AMS1117‑3.3. Power the radios
from the **5V header through per‑module 3.3V regulation** (socket adapters or a
≥500 mA buck) with 10–100 µF + 100 nF decoupling per module, common ground. If
`isChipConnected()` is flaky, drop the RF24 SPI clock 16 MHz→8 MHz.

## Status

Builds green, boots, display + touch verified on hardware. Radios pending physical
wiring per the notes above.
