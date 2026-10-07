# Source attribution and licensing status

ESP Display Lab is inspired by [slavka08/esp-mini_screen](https://github.com/slavka08/esp-mini_screen). It starts with a fresh Git history rather than importing that repository's unrelated sketches, photographs, applications or commit authors. Selected code is **adapted from upstream**, not independently authored solely from inspiration.

| Files / component | Origin and changes |
| --- | --- |
| `firmware/esp_ai_limits/esp_ai_limits.ino` | Adapted from upstream `esp_mini_screen_limits`; adds authenticated OTA, a portable local credential header, clearer window labeling, input validation and safe Wi-Fi name rendering. |
| `senders/ai_limits/ai_limits_sync.py` | Adapted from upstream `macos_ai_limits/ai_limits_sync.py`; uses a portable Python launcher and project-local example configuration. |
| `firmware/esp_synology_display/*` | Built using the upstream Wi-Fi provisioning, EEPROM layout and board mapping; adds the two-volume NAS dashboard, fault display, incremental redraws, brightness and web controls. |
| `hardware/User_Setup.h` and build flags | Follow the upstream ESP12F/ST7789 signal mapping. |
| NAS sender and logic tests | Project additions using the fields in [Synology's published SNMP MIB guide](https://global.download.synology.com/download/Document/Software/DeveloperGuide/Firmware/DSM/All/enu/Synology_DiskStation_MIB_Guide.pdf). |
| Previews, developer documentation and repository scripts | Project additions. Preview readings are synthetic. |

The reference upstream snapshot is commit `c39c02d` (`Added macOS companion app for notifications.`). No explicit licence file was present in that snapshot. Credit alone does not grant rights to relicense inherited code. This repository does not claim a blanket MIT/Apache licence or change upstream authorship. Clarify upstream licensing before distributing inherited portions under a new licence. The same limitation applies to adapted files even though Git history is fresh.

[ESP8266 Arduino core](https://github.com/esp8266/Arduino), [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI), Arduino CLI and compiler utilities retain their respective licences. Install them separately; their binary toolchains and source trees are not vendored here. Codex, Claude and Synology are product names used to describe integrations; this project is not affiliated with their vendors.
