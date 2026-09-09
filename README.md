# Weather Display — ESP32-P4 firmware

A 7-day weather display for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B**
(1024×600 MIPI-DSI, GT911 touch). The interface is the "Weather App" design from
Claude Design, ported to LVGL 9; the data is live from
[Open-Meteo](https://open-meteo.com) (no API key required).

Everything is operable from the touchscreen alone — including joining a Wi-Fi
network — so the device never needs a serial cable after the first flash.

## Features

- Current conditions, 7-day forecast strip, and a slide-up per-day detail panel.
- Hourly temperature + precipitation chart for today and for each forecast day.
- City search over Open-Meteo's geocoding API, with the on-screen keyboard.
- On-device Wi-Fi setup: scan, pick a network, type the passphrase, or enter a
  hidden SSID by hand. Credentials persist in NVS.
- Settings: language (EN/DE/ES/FR), °C/°F, km/h · mph · m/s, 12/24-hour clock.
  All of it persists across reboots.
- Header shows online/offline and warns when data is more than an hour stale.

## Layout

```
main/
├── main.c            display + Wi-Fi bring-up, wiring
├── app_wifi.c/h      station, scanning, runtime provisioning, SNTP
├── app_weather.c/h   Open-Meteo forecast + geocoding worker task
├── app_prefs.c/h     NVS-backed settings and selected city
├── app_format.c/h    locale dates/times and the "real feel" sentence
├── ui_fonts.h        Inter ⇄ Montserrat font mapping
├── weather_chart.c/h hourly temperature + precipitation chart
├── weather_ui.c/h    ── imported from Claude Design ──
├── weather_icons.c/h ── imported from Claude Design ──
├── weather_i18n.c/h  ── imported from Claude Design ──
└── fonts/            generated Inter faces (tools/gen_fonts.sh)
```

### Imported vs. authored

`weather_ui`, `weather_icons` and `weather_i18n` come from the design project's
`lvgl_export/` directory and should be **re-synced from Claude Design** rather than
edited freely. Local changes to them are all marked with a `FIX:` comment so a
future pull stays a readable merge. Everything else is firmware written here.

## Build

Built and verified against **ESP-IDF v6.0.2**. v5.4+ should also work, but only
v6.0.2 has actually been compiled.

Note for IDF 6: cJSON is no longer bundled in IDF core, so it is pulled from the
component registry as `espressif/cjson` (already declared in
`main/idf_component.yml`).

```sh
idf.py set-target esp32p4
idf.py menuconfig      # optional: default city, refresh interval, fallback Wi-Fi
idf.py build flash monitor
```

Dependencies resolve automatically through the component manager
(`main/idf_component.yml`): the Waveshare BSP `3.0.1`, LVGL `9.5.0`, and
`esp_hosted`/`esp_wifi_remote` for the radio.

### First boot

The ESP32-P4 has no radio of its own — Wi-Fi runs over SDIO to the on-board
ESP32-C6. On a device with no stored network, the Wi-Fi setup screen opens by
itself; pick a network and type the passphrase. Credentials go to NVS, so it only
happens once. You can also pre-seed a network under
*Weather Display Configuration → Fallback WiFi SSID* if you prefer.

## Fonts

The design specifies **Inter**. LVGL's built-in Montserrat faces cover ASCII only,
which would leave holes in every accented word in the German, Spanish and French
string tables (`Überwiegend`, `bewölkt`, `Sensación`, `dégagé`, …). So the build
ships generated Inter faces with the full Latin-1 supplement, in the design's own
weights — Regular (400) for captions, Medium (500) for display sizes.

They are already generated in `main/fonts/`. To regenerate (Node required):

```sh
# Inter is SIL OFL 1.1: https://github.com/rsms/inter/releases
tools/gen_fonts.sh ~/Downloads/inter/extras/ttf
```

`LV_SYMBOL_*` glyphs (the GPS pin, refresh, settings icons) are FontAwesome
codepoints that exist **only** in the Montserrat builds, so those specific labels
keep `FONT_SYMBOL`. That is why the header's location line is two labels rather
than one. Setting `CONFIG_WEATHER_USE_INTER_FONTS=n` falls back to Montserrat
everywhere and makes the app English-only in practice.

## Host simulator

`sim/` builds the UI layer against desktop LVGL and opens it in a 1024x600 SDL
window — no board, no emulator. It compiles the same `weather_ui.c`,
`weather_chart.c`, `weather_icons.c`, `weather_i18n.c` and `app_format.c` the
firmware does, against the same vendored LVGL 9.5 at the same colour depth, and
stands in for `main.c` + `app_weather.c` with fixture data and fake geocoding,
Wi-Fi scan and connect.

```sh
./scripts/sim.sh                              # interactive: mouse is the touchscreen
./scripts/sim.sh --screen settings            # open one screen straight away
./scripts/sim.sh --shots shots/               # capture all six states as PNG
```

Screens: `main`, `detail`, `search`, `settings`, `wifi`, plus the first-boot
state via `--wifi-setup`. The capture path is headless (`SDL_VIDEODRIVER=dummy`),
so it works over SSH. Screens are opened by walking the object tree for a known
label and clicking its owner, not by clicking fixed coordinates — a layout change
moves the target without breaking the capture.

What it does **not** show: DSI timing, PPA rotation, the tear-avoid mode, LVGL
buffer sizes, PSRAM throughput, or the real panel's colour behaviour.

Run it from any directory — the script resolves the repo from its own path. It
builds for the host, so a cross-toolchain ahead of `/usr/bin` on `PATH` breaks
it: gcc passes `--64` to an `as` that only knows RISC-V or Xtensa. The ESP
toolchains ship an unprefixed `as` under `<toolchain>/<target>/bin/`, which is
where that usually comes from. `sim.sh` probes the compiler first and says so
rather than letting binutils explain it; `which -a as gcc` confirms it.

## Comparing against the design

`tools/extract-design.mjs` renders `design/src/weather-app/Weather App.dc.html`
in headless Chrome and diffs it against the simulator's own screenshots, so
"does the port match the design" becomes a file instead of a memory.

```sh
./scripts/sim.sh --shots shots            # the LVGL side
node tools/extract-design.mjs --vendor    # once: fetch the React UMD builds
node tools/extract-design.mjs --compare shots
```

It writes `design-<screen>.png` and `compare-<screen>.png` into `shots-design/`
for all five screens — main, settings, detail, search, wifi. Each comparison
sheet stacks design, LVGL and a `mix-blend-mode: difference` overlay where
anything the two agree on goes black. About 14 s for the full set.

No npm dependencies: Chrome is driven through its own `--screenshot` flag and
does the pixel diff itself. Two things the script has to work around, both
documented at the top of it — the design is not a standalone page (its canvas
runtime needs `window.React` before it boots) and it pulls `WeatherIcon.dc.html`
with `fetch()`, which CORS blocks under `file://`, so it is served over a
throwaway localhost server instead.

Screens other than main are opened by patching the design component's initial
`state = { ... }` block (`SCREENS` at the top of the script), not by clicking —
deterministic, and it survives a re-layout of the controls that would open them.
If the design renames a state field the script fails loudly rather than
screenshotting the wrong screen. The simulator's first-boot state has no
counterpart in the design and is skipped.

Note the design fetches live weather from Open-Meteo on mount, so its numbers are
the real forecast while the simulator shows fixtures — compare layout, not values.

## The chart

The design's HTML original drew the temperature line and the precipitation bars on
one plot with two independent y-scales. Two scales on one frame invent a
correlation that isn't in the data, so this port keeps the same visual intent but
stacks two panels sharing a single x-axis — each panel then carries one series
against one scale. Bars and line points are placed from the same x coordinates, so
the panels stay in register.

## Threading

LVGL is not thread-safe. Two rules hold throughout:

- Everything touching LVGL runs under `bsp_display_lock()` / `bsp_display_unlock()`.
- No UI callback blocks. Each one posts a command to the `weather` worker task,
  which owns all HTTP, TLS, JSON, Wi-Fi scanning and connecting.

City-search keystrokes are debounced by 450 ms in the worker, so typing a name is
one request rather than one TLS handshake per character.

## Verification status

- **Builds clean** with ESP-IDF v6.0.2 for `esp32p4` — no warnings from any file in
  `main/`. Image is ~1.6 MB, leaving 80% of the 8 MB app partition free.
- **Runs on the physical board, end to end**: on-screen Wi-Fi provisioning (scan →
  passphrase on the built-in keyboard → connect), credentials persisted to NVS,
  SNTP clock sync, and a live Open-Meteo fetch (`forecast ok: 21.7C code=3, 7 days`).
- **Touch geometry verified by measurement** — corner taps land within ~45 px of
  the true corners on a 1024x600 panel.
- All nine generated Inter faces are confirmed linked into the ELF.
- The formatting layer (`app_format.c` — locale dates, 12/24-hour clock, the "real
  feel" sentence, ISO date parsing) is covered by host-run unit tests, all passing.

**Still unverified:** the finer visual detail. The panel is confirmed upright and
touch is confirmed accurate, but nobody has yet checked the rendered layout against
a photo — specifically the hourly chart's geometry, whether the forecast strip has
enough vertical room, and whether the generated Inter faces render the German /
Spanish / French accented characters correctly.

If taps ever land in the wrong place on a different unit, build with
`CONFIG_WEATHER_TOUCH_DEBUG=y`, tap the four corners, and read the reported
coordinates off the console rather than guessing at flag combinations.

### Hardware gotchas

This board's P4 is **silicon revision v1.3**, so `sdkconfig.defaults` uses the
vendor's `rev1_3` profile. A rev3_x image is refused at flash time — do not pass
`--force`, rebuild with the right profile.

`esp_hosted` is pinned to `3.0.*` and `esp_wifi_remote` to `1.6.*`, which is **not**
what the Waveshare examples use. Their pins (`1.4.*` / `==1.2.5`) double-start the
STA netif on IDF 6.0.2 and boot-loop on an lwip `netif already added` assert. If
you change these versions, delete `sdkconfig` — stale esp_hosted values persist,
including the SDIO reset polarity.

The boot log carries one benign error: `major version mismatch — OTA coprocessor
from host` (host esp-hosted 3.0.7 vs the C6's 2.6.7 firmware). Everything works;
updating the co-processor firmware would silence it.

## Licenses

This project's own code (`main/`, `components/`, `sim/`, `host_test/`,
`scripts/`) is licensed under the **Apache License 2.0** — see `LICENSE`.

It builds on ESP-IDF and a number of components pulled in through the IDF
Component Manager (`main/idf_component.yml`), each under its own license:

| Component | License |
|---|---|
| ESP-IDF (framework) | Apache-2.0 |
| FreeRTOS-Kernel (bundled in ESP-IDF) | MIT |
| mbedTLS (bundled in ESP-IDF) | Apache-2.0 OR GPL-2.0-or-later (dual) |
| lwIP (bundled in ESP-IDF) | BSD-3-Clause-style |
| Unity (bundled in ESP-IDF, used by `host_test/`) | MIT |
| `waveshare/esp32_p4_wifi6_touch_lcd_7b` (board BSP) | Apache-2.0 |
| `lvgl/lvgl` | MIT |
| `espressif/esp_wifi_remote` | Apache-2.0 |
| `espressif/esp_hosted` | Apache-2.0, with `common/eh_common` and `common/eh_tlv` dual-licensed GPL-2.0-only OR Apache-2.0 (electable), and a bundled BSD-3-Clause `protobuf-c` |
| `espressif/cjson` | MIT |
| `espressif/esp_video` | Espressif MIT |
| `espressif/esp_cam_sensor` | Apache-2.0 |
| `espressif/esp_ipa` | Espressif MIT |
| `espressif/esp_sccb_intf`, `esp_lvgl_adapter`, `esp_lcd_touch`, `esp_lcd_touch_gt911`, `esp_lcd_ek79007`, `esp_lv_fs`, `esp_lv_decoder`, `esp_mmap_assets`, `button`, `knob`, `cmake_utilities`, `eppp_link`, `esp_serial_slave_link`, `esp_codec_dev`, `wifi_remote_over_eppp` | Apache-2.0 |
| `espressif/esp_new_jpeg` | Espressif MIT |
| `espressif/freetype`, `libpng`, `zlib`, `usb`, `usb_host_uvc` | Present in the resolved dependency tree (transitive, optional decoder paths) but not required by this project's `main/CMakeLists.txt` and not compiled into the firmware |

The **Inter** typeface used to generate `main/fonts/inter_*.c`
(`tools/gen_fonts.sh`) is licensed under the **SIL Open Font License 1.1**.
Fallback glyphs come from LVGL's built-in Montserrat font (MIT).
