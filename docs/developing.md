# Develop another firmware

## Start from a sample

Use the NAS sketch as the reference for incremental drawing, complete telemetry snapshots, stale-data warnings and persistent brightness. Use the limits sketch for compact cards and partial form updates. Both demonstrate Wi-Fi provisioning and authenticated web OTA. Install one sketch on the device at a time.

Create `firmware/esp_your_sample/esp_your_sample.ino`: Arduino requires the main sketch filename to match its folder. Copy only the needed sample source files, retaining attribution for inherited code. Add an `ota_credentials.h.example`, never a real password. Register the new sample in the build script's `case` and the credential generator's `SKETCHES`, then document its payload and add a synthetic preview.

## Separate responsibilities

Keep sensor/provider access on the sender where possible. The ESP should accept a small validated payload, hold the latest snapshot, draw it and serve a lightweight configuration UI. The NAS sender uses Python's standard library, local SNMP and filesystem/network reads; no DSM administrator login belongs on the display.

Put numerical and state logic in a small header that can also compile on the host. Keep web HTML in `PROGMEM`, as the NAS sample does in `web_pages.h`. Avoid retaining large response bodies or allocating a full 240 × 240 framebuffer: a 16-bit buffer alone is 115,200 bytes, beyond this device's available RAM.

## Define your telemetry contract

Describe every field, its unit, whether it is required, and what an unavailable sensor means. Use `application/x-www-form-urlencoded` for these examples. Missing or invalid values must not become plausible zero readings. Validate finite numbers, ranges, payload length and completeness before replacing state.

The NAS `/nas` endpoint requires a complete snapshot and an explicit empty `storageWarnings` when healthy. It refuses incomplete snapshots, and invalid capacities become warnings. The limits `/limits` endpoint accepts partial metrics; omitted fields retain their previous values. Choose deliberately between these semantics.

Keep fault reasons visible. Do not use a healthy volume summary to mask a degraded storage pool or missing drive. Use an expected inventory to detect disappearance, and distinguish maintenance from failure. Mark stale readings after a bounded interval; never keep displaying an old healthy snapshot as if it were fresh.

## Draw without routine flashing

Initialize the panel once. Draw static labels on entry, cache the displayed text and clear/redraw only changed rectangles. The NAS overview uses eight cached fields for this. Redrawing an entire screen each second visibly flashes on this SPI panel.

Full-screen flashing is intentional only for warning mode. NAS warnings alternate yellow/dark at one-second intervals and page longer reasons every six seconds. When a healthy snapshot arrives, invalidate the overview cache so every field is drawn again. Keep sample previews clearly separate from live device data.

## Brightness and persistence

GPIO 5 is active LOW. Full brightness uses PWM duty 0; off uses 1023. The NAS helper maps percent 0–100 to the inverted duty and clamps its input. The web slider coalesces changes; firmware waits 1.2 seconds after the last change before saving. Avoid writing flash for every slider movement.

Maintain [the shared EEPROM credential layout](hardware.md). Read existing bytes before changing an EEPROM allocation and reserve/document additional fields. Credentials entered in a local browser should not become tracked files, logs or screenshots.

## Networking and web OTA

Call `server.handleClient()` regularly, keep blocking work short, and allow the ESP8266 networking stack to run with `yield()`/`delay()`. Use unsigned subtraction for `millis()` intervals so rollover is handled.

Keep `ESP8266HTTPUpdateServer` and `/update` in a working firmware while experimenting. Use a private credential header and the Arduino build script. The updater password is compiled into the image, so publishing a personal binary publishes that credential. Basic Authentication and other controls use local HTTP; do not assume the sample provides internet-facing authentication or encryption.

Preserve failed-Wi-Fi recovery and serial recovery before changing networking or pin assignments. Keep IRAM, RAM and flash build reports under their limits. The reference NAS build uses about 38% RAM, 93% instruction/cache RAM and 32% of the selected application flash region; IRAM is the tightest resource. These figures can change with code and compiler options.

## Check a change

1. Run `sh scripts/test.sh` and add meaningful tests for new parser/state behavior.
2. Compile the affected sample with `sh scripts/build_firmware.sh <sample>`; its image CRC must pass.
3. Load a synthetic healthy and fault payload from `examples/` and inspect `/state`.
4. On hardware, verify orientation, flicker, long warning text, brightness endpoints, reboot persistence, Wi-Fi fallback and authenticated OTA.
5. Review `git diff --cached` and run the public-tree check before sharing source.

Host tests cannot validate LCD wiring, actual SNMP support or a stock updater's format. The included CI runs host tests and compiles both samples with temporary credentials. It does not upload firmware or publish binaries.
