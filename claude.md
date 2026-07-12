# Lake Lanier E-Ink Display — Project Brief

## What this is
A desk display that shows live Lake Lanier water level, trend, inflow/outflow, and weather, built on an ESP32 + e-paper panel. First hardware prototype, "play around and learn" scope — not a polished product yet. Inspired by lakelanierwater.com; the goal is a filtered, physical version of that dashboard.

## Hardware (confirmed working)
- **Display:** Waveshare 4.2" e-Paper raw display (no PCB), 400×300, black/white, SPI, FPC cable. Panel arrived labeled `042BN-T81-D2, V2` — identified as Good Display **GDEY042T81** (SSD1683 driver, no inking). GxEPD2 driver class in code: `GxEPD2_420_GDEY042T81` (not `GxEPD2_420`, which is for the older GDEW042T2/UC8176 panel).
- **Driver board:** Waveshare Universal e-Paper Driver Board with ESP32 onboard (chip identifies as ESP32-D0WD-V3 via esptool). WiFi/Bluetooth SoC built in — this is the microcontroller, no separate board needed.
- Panel connects directly to the driver board's FPC connector.
- A/B switch: confirmed **A** is correct for this panel (board shipped set to B; flipped to A, first flash rendered correctly).
- Pin mapping (fixed by board PCB): BUSY=25, RST=26, DC=27, CS=15, CLK=13, DIN=14. Requires HSPI remap in code (Waveshare's driver board uses non-standard SPI pins) — see `firmware/hello_world/hello_world.ino`.
- FQBN for arduino-cli: `esp32:esp32:esp32` ("ESP32 Dev Module" — no dedicated Waveshare entry for this plain-ESP32 driver board).
- Reset button is labeled **EN** on this driver board (not RST). Relevant when using `arduino-cli monitor` from a terminal: opening the port doesn't reliably pulse a clean auto-reset the way the Arduino IDE's serial monitor does, so the board can sit there producing no output until EN is pressed manually while the monitor is already attached.
- Power: plugged in via USB-C for this prototype (battery explicitly deferred — driver board's stock deep-sleep draw is too high, ~10-13mA, for meaningful battery life without bypassing the board's own power management; revisit later with a bare low-power ESP32 if battery becomes a priority)
- Screen size: started at 5.83" (648×480), downsized to 4.2" (400×300) for cost/prototyping. Architecture is designed to scale up later — same driver board family, same code structure, just a bigger panel and larger layout when ready.

## Data sources
| Data | Source | Notes |
|---|---|---|
| Lake level (elevation) | USGS Water Services API, gauge `02334400` (Buford Dam), parameter `00062` | Free, no key, updates every 15 min, history back to Oct 2007. Endpoint redirects http→https (HSTS) — device must use https. This gauge has no water-temp parameter; no public live lake-temp gauge was found, so "lake temp" was dropped from scope entirely (see Display priorities). |
| 30-day elevation trend | USGS `dv` (daily values) service, same gauge/parameter, `period=P30D` | Daily means, not 15-min values — 30 data points is plenty for a sparkline and far lighter to parse. |
| Seasonal average elevation | USGS `stat` service (`statReportType=daily&statTypeCd=mean`), same gauge/parameter | Historical mean-of-daily-means for the current calendar day across the full period of record (~20+ years) — i.e. "the average for this time of year." Returns **RDB (tab-delimited), not JSON** — parsed by scanning lines for the `USGS\t02334400\t00062\t` prefix and matching `month_nu`/`day_nu` columns. |
| Inflow | USGS gauges `02331000` (Chattahoochee near Leaf) + `02333500` (Chestatee near Dahlonega), parameter `00060`, summed | Same API family/reliability as elevation, updates every 15 min. |
| Outflow | USGS gauge `02334430` (Chattahoochee at Buford Dam), parameter `00060` | Same API family/reliability as elevation, updates every 15 min. |
| Precipitation (rain 24h) | Open-Meteo, `daily.precipitation_sum` with `past_days=1` | Reuses the weather API already needed below rather than adding a third API family. |
| Weather (current/forecast) | Open-Meteo | Free, no key, covers past/present/future with consistent parameter naming. |

**USACE CWMS API was tried first for inflow/outflow** (it publishes pre-computed `Buford.Flow-In`/`Buford.Flow-Out` series for the dam, found via `cwms-data.usace.army.mil`'s catalog endpoint) but its catalog showed both series' `last-update` stuck several days in the past — i.e. not actually live — so milestone 5 switched to the USGS gauges above instead. CWMS integration was removed entirely; if it's ever revisited, check current data freshness via the catalog endpoint first before trusting it.

## Display priorities (4.2" / 400×300 layout) — implemented in milestone 5
Final layout, centered where noted, after a few rounds of on-device iteration:
- **A priority (top):** "LAKE LANIER" title + date, current elevation (big number, centered), delta from full pool (centered, bold), delta from seasonal average for this day (centered, bold), 30-day trend sparkline (2px thick)
- **B priority (middle):** inflow, outflow, rain (24h)
- **C priority (bottom):** today's + tomorrow's ("TMRW") precipitation/weather-condition forecast side by side, centered "Updated HH:MM AM/PM" footer

Lake temp (originally C-priority) was dropped — no public live water-temperature gauge exists for Lake Lanier. Today's/tomorrow's temperature forecast (hi/lo) was also cut after seeing it on the physical panel — just the weather-condition text reads cleaner at this size. Known constraint: at 400×300 this layout is near its practical limit; the sparkline in particular may get reworked if a bigger panel happens later.

## Software stack
- **Language:** Arduino-flavored C++ (not MicroPython — chosen because e-paper library/example support is much more mature in the Arduino ecosystem)
- **Libraries:** `GxEPD2` (drives the display), `ArduinoJson` (parses API responses), `WiFi.h` + `HTTPClient.h` (bundled with ESP32 board package, no separate install)
- **Dev tools:** Claude Code (terminal) + `arduino-cli` (compiles/flashes) — chosen over the graphical Arduino IDE or a cloud editor, to match existing Git/terminal workflow
- **Version control:** GitHub, same pattern as prior projects (Crater, TrackDay)
- **WiFi credentials:** kept out of git via a `secrets.h` per sketch (gitignored), copied from a checked-in `secrets.h.example` template. `secrets.h` holds a `WIFI_NETWORKS[]` list of `{ssid, password}` pairs; `WiFiMulti` tries all of them and joins whichever is in range. This is intentional for the eventual gift use case — home network now, in-laws' network can be added as a second entry later with no code changes.
- **HTTPS calls:** USGS's endpoint forces https (redirects with HSTS), so device-side fetches use `WiFiClientSecure` with `setInsecure()` (skips CA validation) rather than pinning a root cert — acceptable for this prototype's scope, revisit if that changes.
- **Flash usage watchpoint:** `firmware/lake_level/lake_level.ino` used ~92% of the default 1.2MB app partition. `firmware/dashboard/dashboard.ino` (milestone 5, adds inflow/outflow/weather/seasonal-average) hit 94% on the same default partition — too little headroom for future work (bigger panel, more fonts/icons). Fixed by building with `PartitionScheme=huge_app` (3MB app, no OTA — fine since sketches are reflashed over USB, not updated remotely), which brings it down to 39%:
  ```
  arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app firmware/dashboard
  arduino-cli upload --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p <port> firmware/dashboard
  ```
- **Time sync:** `dashboard.ino` calls `configTime()` + `setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1)` + `tzset()` for NTP time, rather than a fixed UTC offset — keeps the date display and the seasonal full-pool switch (1071 ft May–Nov, 1070 ft Dec–Apr) correct across DST changes automatically.
- **Refresh pattern:** `dashboard.ino` stays on WiFi and re-fetches/re-renders every 15 minutes (`millis()`-based timer in `loop()`), rather than deep-sleeping between refreshes. This sketch is USB-powered, so there's no battery reason to sleep, and staying connected avoids paying WiFi-reconnect + NTP-resync latency every cycle. Revisit with deep sleep if this ever moves to battery power.
- **Serial monitoring gotcha:** `arduino-cli monitor` needs an interactive terminal and exits immediately with no output when run non-interactively (e.g. from an agent's shell). Capturing serial output programmatically works instead via `pyserial` (`pip3 install pyserial`) reading the port directly — plain `stty`+`cat` was unreliable (garbled bytes) for this board's USB-CDC port.

## Background on the person building this
Self-taught developer, comfortable with Git/GitHub/Terminal, has built full-stack apps before (Node/Express/PostgreSQL, GitHub Actions automation) but this is a first embedded/hardware project — C++ and the Arduino toolchain are new territory. Prefers direct, unadorned writing style — no filler intensifiers.

## Progress
- ✅ Repo created and pushed: `github.com/nkljucaric/lake-lanier-display`
- ✅ Milestone 1 (hardware bring-up) complete: `firmware/hello_world/hello_world.ino` flashes via `arduino-cli` and renders text on the physical panel. No WiFi/API code in it — proves panel, driver board, wiring, A/B switch, and toolchain all work together.
- ✅ Milestone 2 (WiFi connect) complete: `firmware/wifi_connect/wifi_connect.ino` joins WiFi via `WiFiMulti` (see `secrets.h` pattern under Software stack), prints the local IP, and does a plain HTTP GET to confirm real internet reachability, not just AP association. Confirmed working on the home network — got an IP and an HTTP 200.
- ✅ Milestone 3 (USGS API pre-test) complete: curled the endpoint directly, found parameter `00062` for elevation and the exact JSON path (`value.timeSeries[0].values[0].value[0].value`) — see Data sources table.
- ✅ Milestone 4 (lake level on screen) complete: `firmware/lake_level/lake_level.ino` joins WiFi, fetches elevation over HTTPS, parses it with ArduinoJson, and renders the number on the panel. Confirmed working on the physical panel.
- ✅ Milestone 5 (full dashboard) complete: `firmware/dashboard/dashboard.ino` implements the full A/B/C layout (see Display priorities) with inflow/outflow/weather/30-day trend/seasonal average, and refreshes itself every 15 minutes unattended. Confirmed running on the physical panel, including at least one unattended self-refresh. This is the project's current end state — see "Where this stands" below.

## Where this stands
The core "play and learn" goal is met: a real ESP32 + e-paper display, running unattended, showing live Lake Lanier data across all three A/B/C priority tiers. There's no fixed "next milestone" queued up right now. Possible future directions if this gets picked back up (none committed to):
- Bigger panel (5.83", 648×480) — architecture was intentionally kept scalable for this (see Hardware section).
- Battery power — would mean revisiting the refresh pattern (deep sleep instead of stay-awake polling) and the driver board's power draw (see Hardware section).
- Reworking the 30-day sparkline presentation (flagged as a maybe during milestone 5, not acted on).
- In-laws' WiFi network as a second `WIFI_NETWORKS[]` entry, once this becomes a gift.
