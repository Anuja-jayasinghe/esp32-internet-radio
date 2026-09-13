# esp32-internet-radio

A headless, always-on ESP32 internet radio appliance. It streams a Shoutcast/Icecast MP3
station over Wi-Fi, decodes it on-device, and pushes it out over I2S to a PCM5102 DAC and
into a powered speaker via 3.5 mm AUX.

Built to replace an aging analog radio that suffered from weak RF signal and static. There
is no display, no buttons, and no web UI — it powers on, connects, and plays. Design
priorities, in order: **reliable unattended operation**, then **clean audio output**.

> **Status:** the firmware is complete but has **not yet been bench-tested end to end**.
> Wiring has been dry-fit against the physical hardware. First flash + listen is the next
> milestone — see [Bench testing](#bench-testing).

## Hardware

| Part | Notes |
|---|---|
| ESP32 NodeMCU DevKit V1 | 30-pin variant |
| GY-PCM5102 DAC | purple board, TI PCM5102A chip |
| Powered speaker | 3.5 mm AUX input |

## Wiring

| ESP32 pin | PCM5102 pin |
|---|---|
| VIN | VIN |
| GND | GND |
| GPIO25 | LCK |
| GPIO27 | DIN |
| GPIO26 | BCK |

Two board mods are **mandatory** and the radio will be silent without them:

1. Bridge `SCK` to `GND` on the DAC board.
2. Move the XSMT (H3L) solder jumper to the 3.3 V side.

Full detail, the diagram, and the reasoning: **[docs/wiring.md](docs/wiring.md)**.

## Flashing

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and add ESP32 board
   support (Boards Manager → `esp32` by Espressif Systems).
2. Install **ESP32-audioI2S** by schreibfaul1, **version 3.2.1** — Library Manager (pick the
   version explicitly), or clone <https://github.com/schreibfaul1/ESP32-audioI2S> and check
   out the `3.2.1` tag into your Arduino `libraries/` folder. See below — do not install
   latest.
3. Open `firmware/esp32_internet_radio/esp32_internet_radio.ino`.
4. Fill in `WIFI_SSID` and `WIFI_PASSWORD`, and set `STREAM_URL` if you want a different
   station.
5. Select board **ESP32 Dev Module**, pick the serial port, and set
   **Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)** — see below, this one's
   required.
6. Upload.

### Partition scheme must be "Huge APP"

The default 4MB partition scheme only gives the app ~1.2MB of flash. With
ESP32-audioI2S's MP3 decoder tables plus the WiFi/TLS stack, this sketch builds to
~1.85MB and won't fit — you'll get `Sketch too big` / `text section exceeds available
space` at compile time, before you ever get to upload.

Set **Tools → Partition Scheme → "Huge APP (3MB No OTA/1MB SPIFFS)"**. That gives a 3MB
app partition; the sketch uses about 44% of it (with library version 3.2.1, below).

### Library version is pinned at 3.2.1 — do not install latest

Starting at **ESP32-audioI2S 3.4.0**, and still true in the current 4.0.0, the library
states **"PSRAM is mandatory"** and allocates its input ring buffer accordingly (~700KB).
The NodeMCU DevKit V1 in this build is a plain WROOM-32 module — **no PSRAM** — so on 3.4.0+
you'll boot, connect to Wi-Fi, connect to the stream, and then immediately crash with:

```
OOM: failed to allocate 720896 bytes for AudioBuffer
```

**Version 3.2.1** is the last release before that requirement, and it degrades gracefully:
it tries PSRAM first, and falls back to a 16KB buffer in internal RAM when none is found —
exactly this board's situation. Compiles to 44% flash / 19% RAM on this sketch.

If you ever move to a PSRAM-equipped board (ESP32-S3 with PSRAM, WROVER, etc.), upgrading
to latest becomes an option worth revisiting — but stay on 3.2.1 for the plain WROOM-32
DevKit V1 this project targets.

> ⚠️ This is a public repo. `WIFI_SSID` / `WIFI_PASSWORD` are placeholders in the committed
> sketch — take care not to commit your real credentials once you fill them in.

### Stream URL

The default is a Shoutcast v1/v2 endpoint:

```
http://uk14freenew.listen2myradio.com:20718/;
```

**The trailing semicolon is required.** Without it, Shoutcast redirects to its browser /
admin page and the connection fails — quietly, and in a way that looks like a network
problem rather than a URL problem. It is very easy to drop when copying the URL around.

### Configuration

| Constant | Default | Purpose |
|---|---|---|
| `INITIAL_VOLUME` | `21` | Digital ceiling (0–21). Final loudness is set on the speaker's own dial. |
| `WIFI_CHECK_INTERVAL_MS` | `5000` | Wi-Fi supervisor poll interval. |
| `STREAM_STALL_TIMEOUT_MS` | `15000` | How long without a running stream before forcing a reconnect. |

## How it stays up

Unattended 24/7 operation is the whole point, so recovery is layered:

- **Stream recovery, layer 1** — the `audio_eof_stream()` callback catches clean TCP closes
  and immediately reconnects.
- **Stream recovery, layer 2** — `handleStreamSupervisor()` is a stall watchdog for silent
  hangs that never fire the EOF callback.
- **Wi-Fi supervisor** — polls every 5 s and reconnects manually, then restarts the stream
  once the link is back.
- **Hardware task watchdog** — `esp_task_wdt`, 30 s timeout, panics and resets the chip if
  the firmware itself locks up.

Both stream-recovery layers are needed. Either one alone leaves a failure mode that ends
with a silent radio nobody notices.

## Bench testing

1. Do both board mods first (SCK bridge, XSMT jumper). Most "it doesn't work" reports are
   really the XSMT jumper.
2. Flash, then open Serial Monitor at **115200 baud**.
3. Confirm, in order:
   - `Wi-Fi connected. IP: …`
   - `[stream] connecting to: …`
   - `[audio] info:` lines, then `[audio] now playing:` track titles
   - **actual sound from the speaker**
4. Then soak it — leave it running overnight and check the log for reconnect churn.

If you get all the way to scrolling track titles and still hear nothing, it is almost
certainly the XSMT jumper, not the code and not the wiring.

### Isolating a DAC/wiring problem from a software problem

`firmware/i2s_dac_test/i2s_dac_test.ino` is a standalone sketch that plays a continuous
440Hz tone over I2S — no Wi-Fi, no streaming, no ESP32-audioI2S library. Flash this first
if you're not sure whether silence is a hardware problem (wiring, SCK bridge, XSMT jumper)
or a software one (stream/decoder).

- **Tone plays cleanly** → the DAC and wiring are good; go debug the main sketch's
  Wi-Fi/stream path.
- **Still silent** → it's the DAC board: recheck the SCK-to-GND bridge and the XSMT
  jumper before touching any code.

It uses the same pin mapping as the main sketch (`GPIO25`→LCK, `GPIO27`→DIN,
`GPIO26`→BCK) and needs no extra library — `ESP_I2S` ships with the esp32 Arduino core.

## Current status (2026-09-13): paused, no audio output yet

Software side is fully confirmed working: USB/serial (CP2102, appears as a COM port once
the driver is installed), Wi-Fi connect, and Shoutcast stream connect + MP3 decode all
verified via serial log with the main sketch. `firmware/i2s_dac_test` (a raw I2S tone, no
Wi-Fi, no ESP32-audioI2S) was also silent, which rules out software/streaming entirely —
this is purely a DAC-board hardware problem.

**What's been found and fixed:**
- An earlier solder mistake bridged the `FLT`/`DEMP`/`XSMT` (`H1L`/`H2L`/`H3L`) jumper pads
  together — cleaned up and re-isolated.
- Chip confirmed genuine: TI `PCM5102A` (Burr-Brown "BB" marking), not a clone part.

**Current leading suspect, not yet confirmed:** the physical header pin order on this
board is `VIN — GND — LCK — DIN — BCK — SCK` (see [docs/wiring.md](docs/wiring.md)) — `GND`
and `SCK` are **4 positions apart, not adjacent**. `BCK` is `SCK`'s actual physical
neighbor. Photos of the attempted SCK→GND bridge look like they may have landed on
`SCK`↔`BCK` instead. Per the TI PCM510xA datasheet (SLAS859C, §9.3.5.3 and §11.5.2): the
internal PLL only starts if `SCK` sits at a **clean, static ground level** while `BCK`/
`LRCK` run; a clock error on `SCK`/`BCK`/`LRCK` puts the chip into standby with **the DAC
and line driver powered off** — which would produce exactly the total silence seen in
every test.

A continuity check came back beeping on **both** `SCK`↔`BCK` and `SCK`↔`GND`, which is
inconclusive — the PCM5102A's pins have internal ESD protection diodes to ground that can
trigger a continuity beeper without an actual copper bridge. Distinguishing a real short
from a diode artifact needs **resistance (Ω) or diode-test mode, board fully unpowered**,
not just continuity beep.

**Next steps, in order, whenever this resumes:**
1. Board unpowered. Multimeter in **resistance or diode-test mode** (not continuity beep):
   measure `SCK`↔`BCK` and `SCK`↔`GND`. A real short reads ~0Ω; an ESD-diode path reads
   open (resistance mode) or ~0.4–0.7V (diode mode).
2. Whichever pin `SCK` is actually shorted to, desolder it clean. Since `GND` is 4 pins
   away from `SCK` on this header (not adjacent), the fix is a short **insulated** wire
   from `SCK` to `GND`, routed clear of `LCK`/`DIN`/`BCK` — not a simple adjacent blob.
3. Separately, with the board powered, measure the `XSMT` (`H3L`) pad voltage directly
   (black probe on `GND`, red on `H3L`): expect ~3.3V for unmuted, ~0V means it still
   needs to move. This measurement was requested repeatedly during bench testing but
   never actually obtained — it's still an open question.
4. Re-test with `firmware/i2s_dac_test` first (simplest possible signal) before going
   back to the full radio sketch.
5. If XSMT is fiddly to get right, the datasheet (§9.3.3) explicitly sanctions wiring it
   **permanently to AVDD (3.3V)** instead of jumpering it, eliminating that variable
   entirely: *"In systems where XSMT is not required, it can be directly connected to
   AVDD."*

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Clean serial log, silent speaker | XSMT (H3L) jumper not moved to the 3.3 V side |
| No audio at all, no error in the log | `SCK` not bridged to `GND` on the DAC board |
| Stream won't connect | Trailing `;` missing from `STREAM_URL` |
| Audio stutters | `WiFi.setSleep(false)` removed, or `audio.loop()` being throttled |
| Radio silently offline after a Wi-Fi drop | `WiFi.setAutoReconnect(true)` reintroduced |
| `OOM: failed to allocate 720896 bytes for AudioBuffer` right after stream connect | ESP32-audioI2S 3.4.0+ installed — this board has no PSRAM, pin the library to 3.2.1 |

### Watchdog API depends on your installed core version

The sketch uses `esp_task_wdt_reconfigure()` with a config struct, which is the
arduino-esp32 core 3.x (ESP-IDF 5.x) API. On this core, the TWDT is already
auto-initialized at boot with a default 5s timeout, so it must be *reconfigured*, not
re-initialized — calling `esp_task_wdt_init()` again fails.

If you're on arduino-esp32 core 2.x (ESP-IDF 4.x) instead, switch to the older two-argument
form:

```cpp
esp_task_wdt_init(30, true); // 30s timeout, panic (reset) on trigger
esp_task_wdt_add(NULL);
```

Check **Tools → Board → Boards Manager → esp32** for your installed version if you're not
sure which API you need.

## Hard constraints

These were each arrived at the hard way. Don't refactor them away without a deliberate
decision:

- Wiring diagrams follow **physical board pin order**, never signal-function groupings.
- The trailing `;` in the Shoutcast URL stays.
- **Both** stream-recovery layers stay.
- `WiFi.setAutoReconnect(true)` is **not** used — known ESP32 SDK race conditions can leave
  the radio silently offline. Manual polling is the fix.
- `WiFi.setSleep(false)` stays, or Wi-Fi power management causes audio stutter.
- `audio.loop()` runs every iteration at full speed, unthrottled, or the I2S DMA buffer
  underruns.
- `SCK` grounded and XSMT moved — both mandatory, both silent failures.

## Repository layout

```
firmware/esp32_internet_radio/esp32_internet_radio.ino   the sketch
docs/wiring.md                                           wiring, jumpers, board mods
docs/wiring.svg                                          wiring diagram
```
