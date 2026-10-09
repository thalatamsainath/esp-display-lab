# Train arrivals and departures display

The ESP fetches the Realtime Trains next-generation station board directly over Wi-Fi. No NAS helper or Home Assistant is required. The screen uses the **Next train** layout: a large destination (departures) or origin (arrivals), countdown, operator colour, expected time and platform. Two following services fill the lower rows. The rows move up together when the primary train passes its expected time; previously hidden cached services fill the vacated rows. Cancellations remain visible in red, while disruption reasons and feed errors can replace the third service.

The station and clock use a bold 26-pixel proportional font; supporting rows use a 16-pixel font. Short destinations use a 26-pixel font and the countdown uses a 48-pixel numeric font. Long destinations wrap into two smaller lines; other rows fit by pixel width, reserving space for delay and cancellation labels. Disruption reasons page at word boundaries. Provider attribution and feed age appear in the web UI, leaving more space for departures on the screen. The showcase image uses the same bitmap fonts as the firmware.

## Install and configure

1. Build `trains` or use your locally prepared `build/trains/SDP_trains_ota.bin`.
2. Open the current display's `/update`, sign in with its **current** updater credentials, and upload the file in the Firmware field. This guide assumes a working compatible Arduino web updater; the filename alone does not establish factory-OTA compatibility.
3. Open the display's root web page after it reboots. Existing Wi-Fi credentials use the same EEPROM layout as the other samples. If it cannot connect, join `MiniScreen-Setup` (password `12345678`) and open `http://192.168.4.1` to configure Wi-Fi.
4. Fresh configurations default to **London Paddington (PAD)**, all routes, GWR + Elizabeth line, departures every three minutes, and active hours 06:00–23:00. Existing saved stations are preserved during an update. Select **Display station** to change it. Route choices appear after a station is selected and reset to all routes when the station changes. Select operators independently of the schedule.
5. Obtain a personal refresh token from the [Realtime Trains API portal](https://api-portal.rtt.io/). Enter the token labelled **request an access token** in the web UI's **Refresh token** field. The display exchanges it at `/api/get_access_token`, keeps the resulting access token in RAM, and renews it before its next use after expiry. The [RTT specification](https://realtimetrains.github.io/api-specification/) documents authentication and the departure schema.
6. Configure the **Time windows** table. Each of up to eight daily rows has a start, end, arrivals/departures mode, whole-number interval from **1–60 minutes**, and route. The **Rest of active hours** row supplies the default when no timed row applies. Adjacent windows are allowed; overlaps are rejected in both the browser and firmware. Windows can cross midnight, and their end is exclusive.
7. Choose **Active hours**, or enable **Run all day**. Pausing takes priority over every timed row. Defaults are 06:00–23:00 UK time and three minutes, without timed rows. British Summer Time is automatic. No RTT calls, including token renewals, run outside active hours; the first refresh is queued when the active window resumes.
8. Use the timeline and **Preview time** to check the resulting day. Disable **Use illustrative demo trains**, then press **Save schedule**. A board or route change clears the old snapshot before fetching the new type. Settings saves and window transitions respect at least one minute between attempts and any existing quota backoff.

For example, at **Paddington (PAD)** with all routes and active hours **06:00–23:00**:

| Time | Board | Interval |
| --- | --- | --- |
| 06:00–09:00 | Departures | 1 minute |
| 17:00–19:00 | Arrivals | 2 minutes |
| Rest of active hours | Departures | 5 minutes |

This schedule estimates **384 station-board requests per day**. The web UI counts each contiguous active phase, including overnight windows, and warns when the total exceeds the reference token's **1,000/day** allowance. Token exchanges, manual changes, other clients sharing the token, and daylight-saving clock changes can affect actual usage and are not included in this nominal 24-hour estimate. Saving an over-budget configuration is allowed; the firmware honours RTT quota headers and `429` / `Retry-After` responses. One-minute refreshes for 06:00–23:00 estimate 1,020 calls/day; 06:00–22:00 estimates 960.

For **London Paddington (PAD)**, departure routes include **All routes**, **Towards Abbey Wood / Shenfield**, **Towards Reading / Heathrow**, and **Other routes / branches**. Arrival choices use **From Abbey Wood / Shenfield** and **From Reading / Heathrow**; these describe where the train comes from relative to Paddington. GWR and Elizabeth line trains in the selected direction are combined and ordered by expected arrival or departure, so an on-time Elizabeth line train can become the next train ahead of a delayed GWR service. Operator settings can select either provider or all rail operators.

The built-in picker covers 30 stations along the corridor from Didcot Parkway through Reading and Paddington to Abbey Wood. Direction groups follow this corridor. Heathrow endpoints are classified through their Hayes & Harlington junction. When RTT omits origin/destination codes in a basic response, exact catalog names and known terminal aliases supply the corridor mapping. Train timestamps without an offset are interpreted in UK local time with automatic BST handling; explicit offsets retain their supplied meaning. The classifier uses terminal destinations and then origins on this corridor; these are broad direction groups, not a guarantee that a train calls at every named intermediate station. Routes it cannot determine remain under Other routes, and are included in Both directions. Other stations can be entered using **Other station** and their three-letter National Rail CRS code; choose all routes or a target CRS. Departures use RTT's server-side `filterTo` for a later calling station; arrivals use `filterFrom` for an earlier calling station, including intermediate stops. Corridor east/west groups remain a local destination/origin classification and do not guarantee intermediate calls. Demo target filters use terminal destinations or origins in their synthetic fixture.


The web page also provides a brightness slider, Wi-Fi settings, and the firmware updater. Brightness and rail settings survive reboots. Future updates use this firmware's private `firmware/esp_train_departures/ota_credentials.h` credentials; they may differ from the firmware you replaced.

An editable, credential-free settings example is in [`examples/train-schedule.json`](../examples/train-schedule.json). It demonstrates the table above; timed windows are opt-in rather than imposed on a fresh device. Select your own station and routes in the web UI. The preview, `examples/train-departures.json`, `examples/rtt-departures.json`, schedule example and embedded demo all use London Paddington (PAD) as their example station. Services, times and platforms are synthetic, not saved device settings.

## Demo and failure behaviour

Enable demo mode to check the screen before obtaining credentials. Demo trains always carry **DEMO** on the screen and web page; they are not live services or a timetable. Demo services reuse fixed synthetic routes and platforms regardless of the selected station; they do not represent services at that station.

- A confirmed delay shows the expected time and additional minutes in amber. A supplied, meaningful delay reason can replace the third train only while the main service is delayed. Generic placeholders such as “unknown cause” are ignored, so the third train remains visible; retained reasons on an on-time service do not replace it.
- An unconfirmed estimate shows **Delayed / time TBC** or **Time unconfirmed**, never an invented on-time countdown.
- Cancelled services cannot become the next-train countdown. The earliest upcoming cancellation remains visible in a lower row, with no reserved blank row above it; supplied reasons page through that row. Cancelled services leave the display once their scheduled time passes.
- As soon as a confirmed expected arrival or departure time passes, the next cached running train becomes primary and the following rows compact and refill from the cache, without an extra API request. This follows the cached forecast rather than confirming actual arrival or departure. Unconfirmed estimates remain labelled as such; stale boards still lose their countdown.
- Missing platforms stay unavailable. Cancellation and delay reasons are only displayed when supplied by Realtime Trains.
- Feed errors appear on the screen and web page. Predictions older than the configured interval plus 30 seconds lose their countdown and disappear from the web departure table. The age shown is time since receipt; RTT does not supply an envelope generation timestamp in this schema.
- If RTT reports degraded or unavailable live data, the display suppresses forecasts and shows the provider status. Actual completed arrivals/departures, non-stopping passes and explicitly non-passenger services are excluded. Set-down-only and terminating calls are accepted for arrivals; pick-up-only and starting calls are accepted for departures. Missing optional passenger/mode flags do not exclude an advertised boarding call. A forecast marked “no report” is suppressed unless an explicit entitled estimate is supplied.

The ESP requests `/rtt/location?code=gb-nr:PAD&timeWindow=60&timeTolerance=true`, using the configured station. It streams one service at a time through a bounded JSON document, then keeps the eight earliest upcoming predictions plus up to two unconfirmed and two cancelled services (12 entries maximum). Predictions whose expected time has already passed do not consume cache slots. The screen shows at most three services at once, retaining the others for automatic promotion between refreshes. The input is capped at 128 KiB, and oversized individual services fail safely. At very busy stations, a rejected or incomplete response leaves the previous complete snapshot in place until it becomes stale; **No running train in feed** is not a claim that no service is scheduled. `204 No Content` means an empty station board. Arrival and departure timing are selected from the same response, so a refresh uses one board request, not two.


## Build and checks

```sh
arduino-cli lib install ArduinoJson@6.21.5
python3 scripts/generate_ota_credentials.py trains
sh scripts/build_firmware.sh trains
```

The TFT and core dependencies are in [building.md](building.md). On Apple Silicon, select your existing native ARM64 kit; no Rosetta installation is needed for that kit.

```sh
sh scripts/test.sh
# Additionally run the shared production JSON parser against the synthetic board:
ARDUINOJSON_INCLUDE=/absolute/path/to/ArduinoJson/src sh scripts/test.sh
```

Native tests cover streaming hundreds of services, truncated envelopes, provider degradation, quota backoff, arrival timing and origin filters, set-down/terminating calls, strict schedule JSON validation, legacy-token migration, schedule boundaries and overlaps, configurable hours (including overnight and all-day windows), interval budgeting, UK summer/winter boundaries, directions, Heathrow routing, operator filters, cancellation exclusion, delay ordering, unknown forecasts, successive cached train promotions, lower-row compaction with cancellations, generic/stale delay-reason suppression, hidden-service refill and selection of three running trains, stale timestamps, missing platforms, station closure, British summer time and midnight rollovers. The web UI was checked with simulated responses for station-specific choices, direction reset, custom station/target CRS, saving, brightness and narrow-screen layout. Compilation and offline tests do not establish successful provider authentication or physical device operation.

## Implementation and storage

- `rail_logic.h`: independently testable station/direction/time/forecast logic.
- `rail_board.h`: shared snapshot types, train ordering and synthetic National Rail-format demo parser.
- `rtt_board.h`: streaming RTT JSON-to-display decoder and live arrival/departure filters.
- `rtt_policy.h`: provider retry and quota helpers.
- `rail_schedule.h`: daily phase selection, overlap validation, refresh spacing and request estimate.
- `rail_settings.h`: versioned settings and migration from the previous RTT configuration.
- `rail_config_json.h`: shared schedule JSON validation used by firmware and native tests.
- `web_pages.h`: station-dependent configuration and local control UI.
- `rtt_ca.h`: ISRG Root X2 used to verify the observed RTT certificate chain. Certificate changes to a different CA require an updated anchor.
- `/diagnostics`: last HTTP status, service/filter counters, free heap / largest free block and one filtered service sample (no credentials).
- `/stations`, `/settings`, `/state`: JSON reads; `/settings` and `/brightness`: form POSTs; `/wifi` and `/connect`: Wi-Fi configuration; `/update`: authenticated firmware uploader.

Wi-Fi occupies EEPROM bytes 0–95, brightness uses 96–97, and versioned rail settings start at 128 inside a 4 KiB allocation. Writes preserve the shared Wi-Fi area. Previous RTT settings migrate the saved refresh token, station, operators, demo mode, active hours, interval and direction into the default rule; they do not create timed rows automatically. The older National Rail-format settings migrate station, direction, operators and demo mode; their API credentials are not reused as an RTT token. Shorter EEPROM allocations used by other samples can discard these rail settings.

Refresh tokens are entered after flashing, stored on the device, never returned by `/state` or `/settings`, and never included in the source or binary. `/settings` returns only a Boolean indicating whether a token is saved. A blank field preserves it. `/settings` includes a `schedule` object with `start`, `end`, `allDay`, `default`, and `rules`; rules use `mode` (`arrivals` or `departures`), `intervalMinutes`, and `direction`, with timed rows also carrying `start` and `end`. POST `/settings` sends this object as a JSON-encoded `schedule` form field alongside station, operators, demo, railBase and refreshToken. The internal `to-CRS` route value applies the appropriate provider filter for the selected board mode. Refresh tokens up to 2,048 characters and access tokens up to 3,072 characters are supported; larger credentials fail explicitly rather than being truncated. Access tokens are kept only in RAM. The UI visibly credits and links Realtime Trains; no shared RTT token is distributed with this project.


The TLS client waits for NTP time and verifies the service's certificate and hostname. MFLN support is probed before reducing TLS receive buffers; an unsupported server keeps the full receive buffer. The station catalog response streams small chunks to avoid a large contiguous heap allocation after TLS. Streaming JSON parsing uses a 1.5 KiB filter and a 2 KiB reusable per-service document. Token exchanges use a separate bounded document; the firmware checks available memory before either request. Network fetches are synchronous with an eight-second request timeout, so the web page can briefly pause during a fetch. Routine screen updates redraw changed fields rather than clearing the full panel.

Configuration and the browser updater operate over local HTTP. Treat the display as a trusted-LAN device; the API credentials stored in EEPROM are not encrypted. Keep personal binaries and credential headers out of public Git, as with the other samples.

Train running information is supplied by **[Realtime Trains](https://www.realtimetrains.co.uk/)**. This project is not affiliated with Realtime Trains, National Rail, GWR or Transport for London. The token holder's access and attribution terms apply; personal tokens must not be embedded in distributed binaries or public source.
