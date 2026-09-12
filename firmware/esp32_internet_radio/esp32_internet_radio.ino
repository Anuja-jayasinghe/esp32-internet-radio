/*
  esp32_internet_radio.ino
  Headless 24/7 ESP32 internet radio appliance.
  Streams a Shoutcast/Icecast MP3 stream, decoded on-device and sent over I2S to a PCM5102 DAC.

  Hardware:
    ESP32 NodeMCU DevKit V1 (30-pin)
    GY-PCM5102 DAC (PCM5102A)
      ESP32 VIN    -> PCM5102 VIN
      ESP32 GND    -> PCM5102 GND
      ESP32 GPIO25 -> PCM5102 LCK
      ESP32 GPIO27 -> PCM5102 DIN
      ESP32 GPIO26 -> PCM5102 BCK
      PCM5102 SCK  -> PCM5102 GND (on-board jumper, required for internal PLL mode)

  Library: ESP32-audioI2S by schreibfaul1
*/

#include <WiFi.h>
#include <Audio.h>
#include <esp_task_wdt.h>

// ---------- User configuration ----------
const char* WIFI_SSID     = "Area_34";
const char* WIFI_PASSWORD = "Tele@slt@4567";

// Trailing semicolon is required to bypass the Shoutcast browser/admin redirect.
const char* STREAM_URL = "http://uk14freenew.listen2myradio.com:20718/;";

// I2S pin mapping (must match wiring above)
#define I2S_LCK  25
#define I2S_DIN  27
#define I2S_BCK  26

// Initial digital volume ceiling (0-21). Physical output level is set on the speaker.
const int INITIAL_VOLUME = 21;

// ---------- Globals ----------
Audio audio;

unsigned long lastWifiCheckMs   = 0;
const unsigned long WIFI_CHECK_INTERVAL_MS = 5000;

unsigned long lastAudioDataMs   = 0;
const unsigned long STREAM_STALL_TIMEOUT_MS = 15000; // no audio data for this long => force reconnect

bool wifiWasConnected = false;

// ---------- Forward declarations ----------
void connectWiFi();
void handleWiFiSupervisor();
void handleStreamSupervisor();
void startStream();

// ---------- ESP32-audioI2S callbacks ----------
// Called by the library on a clean end-of-stream / TCP close.
void audio_eof_stream(const char *info) {
  Serial.print("[audio] eof_stream: ");
  Serial.println(info ? info : "(null)");
  startStream();
}

void audio_info(const char *info) {
  Serial.print("[audio] info: ");
  Serial.println(info);
}

void audio_showstreamtitle(const char *info) {
  Serial.print("[audio] now playing: ");
  Serial.println(info);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32 internet radio starting...");

  // Hardware watchdog: backstop against firmware lockups.
  // arduino-esp32 core 3.x (IDF 5.x) auto-initializes the TWDT at boot with a default
  // 5s timeout, so it must be RE-configured here, not re-initialized.
  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdt_cfg);
  esp_task_wdt_add(NULL); // loop task isn't auto-subscribed; this must stay

  // Avoid Wi-Fi power-save related audio stutter.
  WiFi.setSleep(false);

  connectWiFi();

  audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  audio.setVolume(INITIAL_VOLUME);

  startStream();

  lastAudioDataMs = millis();
}

// ---------- Main loop ----------
void loop() {
  // Feed hardware watchdog.
  esp_task_wdt_reset();

  // Run the audio decode loop as fast as possible to prevent I2S DMA underruns.
  audio.loop();

  handleWiFiSupervisor();
  handleStreamSupervisor();
}

// ---------- Wi-Fi ----------
void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 20000) {
    delay(250);
    Serial.print(".");
    esp_task_wdt_reset();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected. IP: " + WiFi.localIP().toString());
    wifiWasConnected = true;
  } else {
    Serial.println("\nWi-Fi connect timed out, will retry in supervisor loop.");
    wifiWasConnected = false;
  }
}

// Manual reconnect polling — WiFi.setAutoReconnect(true) is avoided due to
// known ESP32 SDK race conditions that can leave the radio silently offline.
void handleWiFiSupervisor() {
  unsigned long now = millis();
  if (now - lastWifiCheckMs < WIFI_CHECK_INTERVAL_MS) return;
  lastWifiCheckMs = now;

  bool connected = (WiFi.status() == WL_CONNECTED);

  if (!connected) {
    Serial.println("[wifi] disconnected, attempting reconnect...");
    wifiWasConnected = false;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else if (!wifiWasConnected) {
    // Just came back online after a drop — restart the stream.
    Serial.println("[wifi] reconnected. IP: " + WiFi.localIP().toString());
    wifiWasConnected = true;
    startStream();
  }
}

// ---------- Stream ----------
void startStream() {
  Serial.println("[stream] connecting to: " + String(STREAM_URL));
  audio.connecttohost(STREAM_URL);
  lastAudioDataMs = millis();
}

// Stall watchdog: catches silent hangs that don't trigger audio_eof_stream().
void handleStreamSupervisor() {
  if (audio.isRunning()) {
    lastAudioDataMs = millis();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) return; // Wi-Fi supervisor will handle this case

  if (millis() - lastAudioDataMs > STREAM_STALL_TIMEOUT_MS) {
    Serial.println("[stream] stall detected, forcing reconnect...");
    startStream();
  }
}
