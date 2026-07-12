# Lake Lanier E-Ink Display — Project Brief

## What this is
A desk display that shows live Lake Lanier water level, trend, inflow/outflow, and weather, built on an ESP32 + e-paper panel. First hardware prototype, "play around and learn" scope — not a polished product yet. Inspired by lakelanierwater.com; the goal is a filtered, physical version of that dashboard.

## Hardware (ordered)
- **Display:** Waveshare 4.2" e-Paper raw display (no PCB), 400×300, black/white, SPI, FPC cable
- **Driver board:** Waveshare Universal e-Paper Driver Board with ESP32 onboard (WiFi/Bluetooth SoC built in — this is the microcontroller, no separate board needed)
- Panel connects directly to the driver board's FPC connector. Confirm driver board is V2.1+ (supports both 3.3V and 5V) — check for the 20-pin chip on the board when it arrives.
- Set the physical A/B switch on the driver board to match the panel (use "A" if 4.2" isn't explicitly labeled).
- Power: plugged in via USB-C for this prototype (battery explicitly deferred — driver board's stock deep-sleep draw is too high, ~10-13mA, for meaningful battery life without bypassing the board's own power management; revisit later with a bare low-power ESP32 if battery becomes a priority)
- Screen size: started at 5.83" (648×480), downsized to 4.2" (400×300) for cost/prototyping. Architecture is designed to scale up later — same driver board family, same code structure, just a bigger panel and larger layout when ready.

## Data sources
| Data | Source | Notes |
|---|---|---|
| Lake level (elevation) | USGS Water Services API, gauge `02334400` (Buford Dam) | Free, no key, updates every 15 min, history back to Oct 2007 |
| Inflow | USGS gauges on Chattahoochee/Chestatee tributaries | Same API family as lake level, same reliability, "calculated" via stage-discharge rating curves |
| Outflow | USACE CWMS API (`cwms-data.usace.army.mil`) | Different API than USGS. Current data reliable; historical data has known gaps/pagination issues per USACE's own developer forum — treat historical outflow as a stretch goal, not v1 |
| Precipitation | USACE CWMS | Same caveats as outflow |
| Weather (current/forecast/historical, one consistent API) | Open-Meteo | Free, no key, covers past/present/future with consistent parameter naming — preferred over NWS for this project since it avoids juggling two providers |

## Display priorities (4.2" / 400×300 layout — approved sketch exists)
- **A priority (largest, top of screen):** "LAKE LANIER" title, date, current elevation (big number), delta from full pool, 30-day trend sparkline, today's weather
- **B priority (middle, smaller):** inflow, outflow, rain (24h)
- **C priority (smallest, bottom row, first to cut if space is tight):** lake temp, tomorrow's weather

Known constraint: at 400×300 this layout is near its practical limit. If real API strings (e.g. weather descriptions) run longer than placeholder text, drop C-priority items first before touching A or B.

## Software stack
- **Language:** Arduino-flavored C++ (not MicroPython — chosen because e-paper library/example support is much more mature in the Arduino ecosystem)
- **Libraries:** `GxEPD2` (drives the display), `ArduinoJson` (parses API responses), `WiFi.h` + `HTTPClient.h` (bundled with ESP32 board package, no separate install)
- **Dev tools:** Claude Code (terminal) + `arduino-cli` (compiles/flashes) — chosen over the graphical Arduino IDE or a cloud editor, to match existing Git/terminal workflow
- **Version control:** GitHub, same pattern as prior projects (Crater, TrackDay)

## Background on the person building this
Self-taught developer, comfortable with Git/GitHub/Terminal, has built full-stack apps before (Node/Express/PostgreSQL, GitHub Actions automation) but this is a first embedded/hardware project — C++ and the Arduino toolchain are new territory. Prefers direct, unadorned writing style — no filler intensifiers.

## Immediate next steps (where this brief picks up)
1. Get comfortable with basic terminal navigation (pwd/cd/ls/mkdir) — coming from a VS Code-integrated-terminal-only background
2. First milestone: get *any* text rendering on the physical panel via a basic GxEPD2 example sketch — before touching WiFi or API calls at all. Isolates hardware/wiring issues from software issues.
3. Pre-test API endpoints outside the device first (curl or browser) to see real JSON structure before parsing it in C++
4. Then: WiFi connect → pull lake level → render on screen → layer in inflow/outflow/weather per the priority tiers above
