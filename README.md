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
2. Install **ESP32-audioI2S** by schreibfaul1 — Library Manager, or clone
   <https://github.com/schreibfaul1/ESP32-audioI2S> into your Arduino `libraries/` folder.
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
app partition; the sketch uses about 58% of it.

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

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Clean serial log, silent speaker | XSMT (H3L) jumper not moved to the 3.3 V side |
| No audio at all, no error in the log | `SCK` not bridged to `GND` on the DAC board |
| Stream won't connect | Trailing `;` missing from `STREAM_URL` |
| Audio stutters | `WiFi.setSleep(false)` removed, or `audio.loop()` being throttled |
| Radio silently offline after a Wi-Fi drop | `WiFi.setAutoReconnect(true)` reintroduced |

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
