#pragma once
#include <Arduino.h>

namespace audio {

constexpr int EQ_BANDS = 5;
extern const float EQ_FREQS[EQ_BANDS];
extern const char* EQ_LABELS[EQ_BANDS];

constexpr int SCOPE_LEN = 1024;  // power of two

void begin();
bool play(const String& path);   // .mp3 or .wav on the SD card
void stop();
void togglePause();
bool isPlaying();                // a track is loaded (playing or paused)
bool isPaused();
bool finished();                 // last track ended on its own (cleared on read)
float progress();                // 0..1
uint32_t elapsedMs();
int sampleRate();
String title();                  // ID3 title/artist if present
String artist();

void setVolume(uint8_t v);       // 0..21
void setEq(const int8_t db[EQ_BANDS]);  // -12..+12 dB

// Copy the most recent n output samples (mono, post-EQ) for visualisation
void scope(int16_t* out, int n);

// Short tone through the speaker (only when music is not playing)
void beep(uint16_t freq, uint16_t ms, uint8_t vol = 10);
}
