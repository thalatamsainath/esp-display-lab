# Synology NAS display

ESP12F/ESP8266 firmware for the GeekMagic SmallTV-Ultra/ST7789 240×240 display. A sender runs directly on the Synology NAS; Home Assistant and a continuously running desktop helper are not required.

The overview shows Volume 1 and Volume 2 used percentage and used/total/free TB, receive/transmit rates, and average physical-drive temperature. Storage faults replace the overview with `WARNING` and the reported reason, flashing yellow/dark once per second. Long or multiple messages advance every six seconds. Version 1.1 updates only changed overview fields and provides a persistent 0–100% backlight brightness slider in the web UI.

## Build the firmware

Install Arduino CLI, ESP8266 Arduino core 3.1.2, and TFT_eSPI 2.5.43:

```sh
arduino-cli core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core install esp8266:esp8266@3.1.2 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli lib install TFT_eSPI@2.5.43
```

Create a local updater credential header:

```sh
python3 scripts/generate_ota_credentials.py synology
```

The script generates a unique password and does not overwrite an existing header. Read the resulting private `firmware/esp_synology_display/ota_credentials.h` file for your login. Alternatively copy `ota_credentials.h.example` to `ota_credentials.h` and choose your own password. If upgrading a device with this firmware already installed, retain its existing local header to keep its updater login unchanged.

Build from the repository root:

```sh
sh scripts/build_firmware.sh synology
```

The script supplies the hardware pin settings without editing the installed TFT_eSPI library. It selects generic ESP8266, 4 MB flash/1 MB filesystem, DIO, 40 MHz, then validates the full-image CRC. The upload file is `build/synology/SDP_synology_ota.bin`.

For an existing isolated Arduino installation, including the native Apple Silicon build kit, point the script at it:

```sh
ARDUINO_CLI=/absolute/path/to/kit/bin/arduino-cli \
ARDUINO_CONFIG_FILE=/absolute/path/to/kit/arduino-config.json \
sh scripts/build_firmware.sh synology
```

The stock macOS ESP8266 3.1.2 tools include Intel executables. Use Rosetta or a separately configured native ARM64 build kit on Apple Silicon. Compiler/core/library bundles are intentionally not committed here.

Your local header, actual sender configuration, and generated builds are ignored by Git. **Firmware binaries contain the compiled updater password:** do not publish personal builds. This repository uses source/configuration templates rather than a shared updater password.

## Upload and configure Wi-Fi

If your current firmware already has `/update`, open `http://<display-ip>/update` in a browser that supports Basic Authentication, such as Safari or Chrome. Use the current firmware's login, select the new binary in the **Firmware** field, and keep power connected through upload/reboot. Subsequent updater logins use the credentials compiled into the new build.

Web OTA can be used for the initial migration only if the existing firmware supports a compatible application updater; otherwise use an appropriate 3.3 V UART flashing adapter and the upstream hardware instructions.

The NAS sketch retains the upstream Wi-Fi EEPROM layout. If saved Wi-Fi is unavailable at boot, join `MiniScreen-Setup` with password `12345678` and open `http://192.168.4.1` to configure Wi-Fi. After connecting, the root page provides status, a brightness slider, sample snapshot testing, and a link to the password-protected firmware updater. At 0% brightness the backlight is off; the web UI remains available. Brightness is saved after 1.2 seconds without changes and restored after reboot.

## Set up the NAS sender

The sender targets DSM 7.1+ with the disk-health SNMP field available. Confirm the readings and table names on your NAS before enabling scheduled uploads. Python 3.8+ is required; there are no third-party Python dependencies. The runner searches standard Python 3 paths. If no runtime is present, use Synology Package Center's Python3 package when available for your DSM.

1. Create a shared folder named `esp-display` on a volume of your choice. The commands below use `/volume1/esp-display`; substitute the actual folder path consistently if you use Volume 2.
2. Upload `senders/synology/synology_sync.py`, `snmp_reader.py`, and `run-sender.sh` into that folder.
3. Copy `sender-config.json.example` into the folder as `sender-config.json`. Set `display_url` to the display's local URL.
4. In DSM Control Panel → Terminal & SNMP → SNMP, enable SNMPv1/SNMPv2c with a custom community name. Put that name in the NAS-local configuration. Keep this file readable only by the task account.

The sender queries `127.0.0.1:161`; it does not require a DSM administrator password or remote SNMP access. SNMPv2c is unencrypted, but these queries use NAS loopback. The display's telemetry/test endpoints use unauthenticated HTTP on the local network; the web firmware updater uses Basic Authentication.

Create a disabled DSM **Task Scheduler → Scheduled Task → User-defined script** with an account that can read/write the sender folder. Use **Run** manually and inspect task output while configuring it. Root is not required.

Inspect the tables:

```sh
sh /volume1/esp-display/run-sender.sh --inspect
```

Match `volume1_raid_name` and `volume2_raid_name` to the names in `raidRows`. Defaults are `volume1` and `volume2`; case, spaces, and punctuation are normalized for matching. Volume capacity comes from `statvfs` on `/volume1` and `/volume2`, using decimal TB, rather than assuming a RAID table index. Reserved filesystem space and DSM accounting can produce differences from Storage Manager.

RX/TX uses a two-second sample from `/proc/net/dev`. By default the interface is selected from the default IPv4 route. Set `network_interfaces` explicitly if needed, for example `["bond0"]`. Do not select a bond and its member interfaces together; that double-counts traffic. Rates use decimal MB/s.

While the NAS is healthy, save its current drive inventory:

```sh
sh /volume1/esp-display/run-sender.sh --learn-drives
```

This writes expected names to the private configuration so disappearing drives trigger warnings. If there is already a fault, configure `expected_drive_names` manually from the inspection/known inventory, retaining missing drives in the list. Update this inventory after intentional additions/removals.

Test one upload:

```sh
sh /volume1/esp-display/run-sender.sh --once
```

For a simple persistent setup, enable a daily scheduled task repeating **every minute** with that same `--once` command. Overlapping runs exit using a file lock. Fault detection is bounded by the schedule plus collection time.

For approximately 30-second updates, use a **Triggered Task → Boot-up** with the command below and run it once now as well:

```sh
sh /volume1/esp-display/run-sender.sh
```

Use one recurring method. Continuous mode follows `interval_seconds` (default 30). A one-minute scheduled task recovers more easily after a sender process failure; a continuous process remains stopped until restarted if it dies.

## Warnings and temperature

The average is calculated from individual available physical-drive temperatures, each drive counted once. Zero/missing/out-of-range values are excluded; no valid temperatures produces `--`. `/state` reports the count used. Two volume averages are not averaged together.

Faults are collected for all reported storage pools, volumes, and drives, even though the overview shows only Volume 1 and Volume 2. Degraded/crashed/read-only storage, interrupted expansion, failed/missing drives, non-normal drive health, reported bad sectors/identification failures, missing/unknown health, and unmounted/read-only filesystems trigger warnings. Historical error counters can keep a warning visible even when DSM's current overall status is normal; the text names the reported counter.

Routine repair, sync, scrub, and expansion are maintenance states, not failure warnings. Missing monitoring data is a monitoring warning, not an assertion that the disks themselves failed. After three minutes without uploads, the display warns that readings are stale. Active warnings clear only after a new healthy complete snapshot. Wi-Fi disconnects also warn.

This reports filesystem and published Synology SNMP fields; it cannot reproduce every DSM notification or fault not exposed by those sources. Very long fault lists are shortened at complete message boundaries with an explicit `More faults: inspect DSM Storage Manager` notice.

## HTTP interface

- `GET /`: web UI and manual test snapshots. Sample uploads replace displayed readings until the NAS's next snapshot.
- `GET /state`: JSON status, firmware version, readings, warning text, temperature count, and brightness.
- `POST /brightness`: form field `value`, integer 0–100; invalid values return HTTP 400.
- `/update`: Basic Authentication protected firmware uploader.
- `POST /nas`: complete form-encoded snapshot. A browser GET to `/nas` returns 404.

Every snapshot field below is required, including explicitly empty `storageWarnings` when healthy. Missing numeric data is an empty string, not zero. `nasName` is optional. Incomplete snapshots are rejected without clearing an existing fault.

```json
{
  "nasName": "SYNOLOGY",
  "volume1UsedTB": "5.4",
  "volume1TotalTB": "8",
  "volume1Status": "normal",
  "volume2UsedTB": "1.2",
  "volume2TotalTB": "4",
  "volume2Status": "normal",
  "rxMBps": "12.4",
  "txMBps": "1.8",
  "driveTemps": "38,36,40,34",
  "storageWarnings": ""
}
```

Send as `application/x-www-form-urlencoded`, not JSON. Warning reasons are plain ASCII separated by `|`, e.g. `Storage Pool 1: degraded|sata3: failing`. Abnormal Volume 1/2 status also warns automatically. Warning inputs are limited to 1,024 characters.

## Verification

Run host-side regression tests:

```sh
sh scripts/test.sh
```

Tests cover degraded pools with healthy volumes, failed/missing drives, unknown/missing health, readonly volumes, temperature handling, byte-rate conversion and counter resets, SNMP decoding, capacity validation, stale timers, and active-low brightness duty/validation. The build script verifies the Arduino full-image CRC. Hardware PWM, display appearance, Wi-Fi recovery, OTA, and your actual NAS readings still require device testing.

## References

- [Upstream project and hardware mapping](https://github.com/slavka08/esp-mini_screen)
- [Synology SNMP MIB guide](https://global.download.synology.com/download/Document/Software/DeveloperGuide/Firmware/DSM/All/enu/Synology_DiskStation_MIB_Guide.pdf)
- [Synology SNMP configuration](https://kb.synology.com/en-au/DSM/help/DSM/AdminCenter/system_snmp?version=6)
- [Synology Task Scheduler](https://kb.synology.com/index.php/en-ro/DSM/help/DSM/AdminCenter/system_taskscheduler?version=7)
