/*
  i2s_dac_test.ino
  Standalone DAC/wiring hardware test — NOT the radio firmware.

  Plays a continuous 440Hz sine tone over I2S. No Wi-Fi, no streaming, no
  ESP32-audioI2S library — this exists purely to prove out the ESP32 <-> PCM5102
  wiring and the two board mods (SCK->GND bridge, XSMT jumper) in isolation from
  any network or MP3-decoding variable.

  If you hear a clean, continuous tone: the hardware chain is good and the radio
  firmware's silence (if any) is a software/stream issue, not wiring/jumpers.
  If you hear nothing: it's still the DAC board (jumpers, SCK bridge, or wiring).

  Wiring (must match the main sketch):
    ESP32 GPIO25 -> PCM5102 LCK
    ESP32 GPIO27 -> PCM5102 DIN
    ESP32 GPIO26 -> PCM5102 BCK
*/

#include <ESP_I2S.h>
#include <math.h>

#define I2S_LCK  25
#define I2S_DIN  27
#define I2S_BCK  26

const int frequency  = 440;   // A4, easy to recognize
const int sampleRate = 44100;
const int16_t amplitude = 12000; // moderate level, avoids clipping/distortion

I2SClass i2s;
float phase = 0.0f;
const float phaseIncrement = 2.0f * PI * frequency / sampleRate;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nI2S DAC hardware test: 440Hz tone");

  i2s.setPins(I2S_BCK, I2S_LCK, I2S_DIN);

  if (!i2s.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Failed to initialize I2S! Check wiring.");
    while (1) delay(1000);
  }

  Serial.println("I2S started. You should hear a continuous tone now.");
}

void loop() {
  int16_t sample = (int16_t)(amplitude * sinf(phase));
  phase += phaseIncrement;
  if (phase >= 2.0f * PI) phase -= 2.0f * PI;

  int16_t left = sample;
  int16_t right = sample;
  i2s.write(&left, sizeof(left));
  i2s.write(&right, sizeof(right));
}
