<h1 align="center">tpms-reader</h1>
<p align="center">Standalone ESP32-C6 dashboard that reads Bosch TPMS tire-pressure sensors over BLE — no phone, no app, no cloud</p>
<div align="center">
  <a href="https://github.com/abhijithvijayan/bosch-tpms-decoder/blob/main/license">
    <img src="https://img.shields.io/github/license/abhijithvijayan/bosch-tpms-decoder.svg" alt="LICENSE" />
  </a>
  <a href="https://twitter.com/intent/tweet?text=Check%20out%20bosch-tpms-decoder%21%20by%20%40_abhijithv%0A%0ARead%20Bosch%20TPMS%20BLE%20sensors%20on%20an%20ESP32%0Ahttps%3A%2F%2Fgithub.com%2Fabhijithvijayan%2Fbosch-tpms-decoder%0A%0A%23tpms%20%23bluetooth%20%23reverseengineering%20%23esp32">
     <img src="https://img.shields.io/twitter/url/http/shields.io.svg?style=social" alt="TWEET" />
  </a>
</div>
<h3 align="center">Made by <a href="https://twitter.com/_abhijithv">@abhijithvijayan</a></h3>
<p align="center">
  Donate:
  <a href="https://www.paypal.me/iamabhijithvijayan" target='_blank'><i><b>PayPal</b></i></a>,
  <a href="https://www.patreon.com/abhijithvijayan" target='_blank'><i><b>Patreon</b></i></a>
</p>
<p align="center">
  <a href='https://www.buymeacoffee.com/abhijithvijayan' target='_blank'>
    <img height='36' style='border:0px;height:36px;' src='https://bmc-cdn.nyc3.digitaloceanspaces.com/BMC-button-images/custom_images/orange_img.png' border='0' alt='Buy Me a Coffee' />
  </a>
</p>
<hr />

## Table of Contents

- [Overview](#overview)
- [Hardware](#hardware)
- [Features](#features)
- [How It Works](#how-it-works)
- [Building & Flashing](#building--flashing)
- [Configuration](#configuration)
- [Wire Format](#wire-format)
- [How It Was Reverse-Engineered](#how-it-was-reverse-engineered)
- [Issues](#issues)
- [License](#license)

## Overview

Bosch TPMS sensors broadcast tire pressure, temperature, and battery state over Bluetooth Low Energy. The payload is encrypted with a hard-coded AES-128 key embedded in the official `com.bosch.boschtpms` app — so anyone with the key (it's the same in every sensor) can read their own tires without the vendor app.

This project is the firmware for a **standalone in-car display**: a small ESP32-C6 board with a 1.47″ LCD that passively listens for your four sensors, decrypts each advertisement on-device, and shows a live four-tire dashboard with low/high-pressure alerts. It never pairs, connects, or talks to the cloud — it just listens and renders.

## Hardware

- **Board:** [Waveshare ESP32-C6-LCD-1.47](https://docs.waveshare.com/ESP32-C6-LCD-1.47?variant=ESP32-C6-LCD-1.47) — ESP32-C6 (WiFi 6 + BLE 5), 4 MB flash, 1.47″ ST7789 IPS LCD (172×320, rounded corners), onboard RGB LED, USB-C.
- **Sensors:** Bosch TPMS BLE sensors (the kind that advertise service UUID `0xFFE0`).

LCD wiring (fixed on the board, per the schematic):

| Signal | GPIO |
|---|---|
| SCLK | 7 |
| MOSI | 6 |
| DC | 15 |
| CS | 14 |
| RST | 21 |
| Backlight | 22 |

> ⚠️ Note: the LCD-1.47 and the **Touch**-LCD-1.47 are different boards with different LCD pinouts. This firmware targets the **non-touch** LCD-1.47.

## Features

- **Passive, always-on scanning** — sensors are transmit-only beacons; the board only receives. No pairing, no connection.
- **On-device AES-128 decode** — matches the official app byte-for-byte (see [Wire Format](#wire-format)).
- **2×2 tire dashboard** (LVGL) with five per-card states:
  - **idle** — no data yet (`--`, dimmed)
  - **normal** — pressure in range
  - **high** — overinflated (amber card)
  - **low** — under-inflated / puncture (red card)
  - **stale** — no fresh reading for 30 min (faded; last value held)
- **Battery indicator** with tiered color (green / amber / red / deep-red) and a custom bar widget.
- **Last-seen age** per tire (`s` / `m` / `h`).
- **Header status pill** summarizing the fleet (all-OK / low / high / multiple issues), hidden until the first reading.
- **Regulation-grounded thresholds** derived from the car's placard pressure (US FMVSS-138 / EU ECE-R141), validated at compile time against the tire's sidewall max.
- **Thermal management** — 80 MHz CPU + PWM backlight, with a backstop that dims the backlight if the die runs hot (the LCD blanks above its clearing temperature).
- **Tire-rotation aware** — readings are stored per sensor; a wheel→card mapping is applied only at display time, so rotating tires is a one-line remap (not yet runtime-editable — see `TODO.md`).

## How It Works

```
 NimBLE task : advert ──▶ filter UUID 0xFFE0 ──▶ whitelist MAC ──▶ AES decode ──▶ recordPacket() ──┐
                                                                                                   │ (mutex)
 loop() task : every 400 ms ─▶ snapshot cache ─▶ classify each card ─▶ render texts/colors/pill ◀──┘
```

The BLE host task and the UI loop run on separate FreeRTOS tasks and only meet inside a short critical section that guards the shared reading cache — so a half-written record can never be rendered.

The UI is designed in [EEZ Studio](https://www.envox.eu/studio/) (LVGL 9 export) under `src/ui/`; the firmware in `src/main.cpp` binds to the generated widgets by name and drives all values, colors, and states from the decoded data.

## Building & Flashing

This is a [PlatformIO](https://platformio.org/) project.

```sh
# build
pio run

# flash + open the serial monitor
pio run -t upload && pio device monitor
```

Notes:

- The ESP32-C6 needs Arduino-ESP32 core 3.x, so the project uses the community **pioarduino** platform fork (configured in `platformio.ini`).
- Serial is over the native USB-C port (`ARDUINO_USB_CDC_ON_BOOT=1`) at `115200`.
- Libraries (auto-installed): NimBLE-Arduino, GFX Library for Arduino, LVGL 9, ArduinoJson.
- Static analysis: `pio check` (needs `cppcheck`).

## Configuration

Edit the constants at the top of `src/main.cpp`:

- **Your sensors:** `WHITELISTED_SENSOR_MAC_ADDRESSES` — the four sensor MACs (find a replacement's MAC from the serial log).
- **Wheel mapping:** `sensorIndexForPosition` — which sensor shows on which card (update after a tire rotation).
- **Alert thresholds:** `RECOMMENDED_PSI` (your door-placard cold pressure) and `SIDEWALL_MAX_PSI` (printed on the tire). `PSI_LOW`/`PSI_HIGH` derive from these and are checked by `static_assert`.
- **Thermal:** `PANEL_BLACKOUT_TEMPERATURE` — calibrate to the die temp at which the panel starts blanking.

## Wire Format

The full plaintext is 16 bytes after AES-128/ECB/NoPadding decryption with the key `#@Trl2018-lespl$`. Ciphertext is bytes `[15..31]` of the raw BLE scan record:

| Offset | Length | Field |
|---|---|---|
| `0` | 1 | Header `0x16` (constant) |
| `1..3` | 2 | Temperature — little-endian uint16, sign-magnitude (high bit = sign flag, low 15 bits = magnitude × 0.01 °C). `0xFFFF` = invalid. Valid range `[-40, 125]` °C. |
| `3..5` | 2 | Pressure — little-endian uint16, divided by 100 (raw scale is most likely kPa). `0xFFFF` = invalid. Valid range `[0, 217]`. |
| `5` | 1 | Battery percent (clamped to ≤ 100). |
| `6..14` | 8 | Status / reserved. |
| `14..16` | 2 | Trailer `0x15 0x14` (constant). |

Any field that fails its range check is reported as the in-app invalid sentinel `0xFF4C` (65356). Each tire is identified by its BLE MAC address.

## How It Was Reverse-Engineered

The encryption key, byte layout, and clamps were extracted from the decompiled `com.bosch.boschtpms` APK. See [`HARDWARE.md`](HARDWARE.md) for the full walk-through; the key files:

| File | What it provides |
|---|---|
| `base/smali/y6/t.smali` | The decoder entry point `c()`: scan-record slicing (offset 15, length 16), the AES call, field offsets, range clamps, the `0xFF4C` invalid sentinel. |
| `base/smali/y6/b.smali` | The helper math: `a([B)I` (LE-uint), `f([B)I` (sign-magnitude temperature), `e([B)I` (pressure `/100`), `z` / `A` (LE-uint `== 0xFFFF` checks). |

## Issues

Please file an issue [here](https://github.com/abhijithvijayan/bosch-tpms-decoder/issues/new) for bugs, missing documentation, or unexpected behavior.

## License

MIT © [Abhijith Vijayan](https://abhijithvijayan.in)
