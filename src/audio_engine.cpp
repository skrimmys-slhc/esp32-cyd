// Audio engine: SD file -> ID3 -> buffer -> decoder -> [mono mix, 5-band EQ,
// volume, scope tap] -> I2S in built-in-DAC mode -> GPIO26 -> on-board amp.
#include "audio_engine.h"
#include <driver/i2s.h>
#include <driver/dac.h>
#include <SD.h>
#include <AudioOutput.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioGeneratorAAC.h>
#include "dsp.h"
#include "pins.h"

namespace audio {

const float EQ_FREQS[EQ_BANDS] = {60, 250, 1000, 4000, 12000};
const char* EQ_LABELS[EQ_BANDS] = {"60", "250", "1k", "4k", "12k"};

static constexpr i2s_port_t PORT = I2S_NUM_0;

// ------------------------------------------------------------------ state
static SemaphoreHandle_t s_mtx;
static int16_t s_ring[SCOPE_LEN];
static volatile uint32_t s_ringPos = 0;
static volatile float s_gain = 0.5f;
static volatile int8_t s_eqDb[EQ_BANDS] = {0};
static volatile bool s_eqDirty = true;
static volatile bool s_paused = false;
static volatile bool s_finished = false;
static volatile uint64_t s_samplesOut = 0;
static volatile int s_rate = 44100;
static String s_title, s_artist;

static AudioFileSourceSD* s_file = nullptr;
static AudioFileSourceID3* s_id3 = nullptr;
static AudioFileSourceBuffer* s_buf = nullptr;
static AudioGenerator* s_gen = nullptr;

static void ampOn(bool on) { digitalWrite(PIN_AUDIO_EN, on ? AUDIO_EN_LEVEL : !AUDIO_EN_LEVEL); }

// ------------------------------------------------------------------ output
class DacEqOutput : public AudioOutput {
 public:
  bool begin() override { return true; }

  bool SetRate(int hz) override {
    if (hz != hertz) {
      hertz = hz;
      s_rate = hz;
      i2s_set_sample_rates(PORT, hz);
      s_eqDirty = true;
    }
    return true;
  }

  bool ConsumeSample(int16_t sample[2]) override {
    if (_fill == BUF_FRAMES && !flushSome()) return false;
    if (s_eqDirty) updateEq();

    float x = ((int32_t)sample[0] + (int32_t)sample[1]) * 0.5f;
    if (_eqOn) {
      x *= _pre;
      for (int i = 0; i < EQ_BANDS; i++) x = _bq[i].process(x);
    }
    x *= s_gain;
    int32_t s = (int32_t)x;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;

    s_ring[s_ringPos & (SCOPE_LEN - 1)] = (int16_t)s;
    s_ringPos = s_ringPos + 1;

    uint16_t u = (uint16_t)(s + 32768);  // DAC wants unsigned, uses the top 8 bits
    _buf[_fill++] = ((uint32_t)u << 16) | u;
    if (_fill == BUF_FRAMES) flushSome();
    return true;
  }

  void invalidateRate() { hertz = 0; }

  bool stop() override {
    _fill = 0;
    _sent = 0;
    return true;
  }

  void silence(int frames) {
    uint32_t mid = 0x80008000;
    size_t w;
    for (int i = 0; i < frames; i++) i2s_write(PORT, &mid, 4, &w, pdMS_TO_TICKS(20));
  }

 private:
  static constexpr int BUF_FRAMES = 128;
  uint32_t _buf[BUF_FRAMES];
  int _fill = 0, _sent = 0;
  Biquad _bq[EQ_BANDS];
  bool _eqOn = false;
  float _pre = 1.0f;

  bool flushSome() {
    size_t written = 0;
    i2s_write(PORT, (uint8_t*)_buf + _sent * 4, (_fill - _sent) * 4, &written, 0);
    _sent += written / 4;
    if (_sent >= _fill) {
      s_samplesOut = s_samplesOut + _fill;
      _fill = 0;
      _sent = 0;
      return true;
    }
    return false;
  }

  void updateEq() {
    s_eqDirty = false;
    float fs = hertz ? hertz : 44100;
    int maxBoost = 0;
    _eqOn = false;
    for (int i = 0; i < EQ_BANDS; i++) {
      int g = s_eqDb[i];
      if (g) _eqOn = true;
      if (g > maxBoost) maxBoost = g;
      Biquad::Type t = i == 0 ? Biquad::LOWSHELF : i == EQ_BANDS - 1 ? Biquad::HIGHSHELF : Biquad::PEAK;
      float f = EQ_FREQS[i];
      if (f > fs * 0.45f) f = fs * 0.45f;
      _bq[i].set(t, fs, f, g, i == 0 || i == EQ_BANDS - 1 ? 0.707f : 1.0f);
    }
    _pre = powf(10.0f, -maxBoost / 20.0f);  // headroom so boosts don't clip
  }
};

static DacEqOutput* s_out = nullptr;
static void onMeta(void*, const char* type, bool isUnicode, const char* str);

// ------------------------------------------------------------------ task
static void freeChain() {
  if (s_gen) { if (s_gen->isRunning()) s_gen->stop(); delete s_gen; s_gen = nullptr; }
  if (s_buf) { delete s_buf; s_buf = nullptr; }
  if (s_id3) { delete s_id3; s_id3 = nullptr; }
  if (s_file) { s_file->close(); delete s_file; s_file = nullptr; }
}

static void audioTask(void*) {
  for (;;) {
    bool idle = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_gen && s_gen->isRunning()) {
      if (s_paused) {
        xSemaphoreGive(s_mtx);
        s_out->silence(64);
        continue;
      }
      idle = false;
      if (!s_gen->loop()) {
        freeChain();
        s_out->silence(256);
        ampOn(false);
        s_finished = true;
      }
    }
    xSemaphoreGive(s_mtx);
    vTaskDelay(idle ? pdMS_TO_TICKS(20) : 1);
  }
}

// ------------------------------------------------------------------ API
void begin() {
  pinMode(PIN_AUDIO_EN, OUTPUT);
  ampOn(false);

  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  c.sample_rate = 44100;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count = 8;
  c.dma_buf_len = 256;
  c.use_apll = false;
  c.tx_desc_auto_clear = true;
  i2s_driver_install(PORT, &c, 0, nullptr);
  // GPIO26 only (DAC channel 2) so GPIO25 stays free for the I2C header
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
  dac_output_disable(DAC_CHANNEL_1);

  s_mtx = xSemaphoreCreateMutex();
  s_out = new DacEqOutput();
  s_out->silence(64);
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 0);
}

bool play(const String& path) {
  stop();
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  bool ok = false;
  s_title = "";
  s_artist = "";
  String lower = path;
  lower.toLowerCase();
  s_file = new AudioFileSourceSD(path.c_str());
  if (s_file->isOpen()) {
    AudioFileSource* src = s_file;
    if (lower.endsWith(".mp3")) {
      s_id3 = new AudioFileSourceID3(s_file);
      s_id3->RegisterMetadataCB(onMeta, nullptr);
      src = s_id3;
    }
    s_buf = new AudioFileSourceBuffer(src, 4096);
    if (lower.endsWith(".wav")) s_gen = new AudioGeneratorWAV();
    else if (lower.endsWith(".aac")) s_gen = new AudioGeneratorAAC();
    else s_gen = new AudioGeneratorMP3();
    s_samplesOut = 0;
    s_paused = false;
    s_finished = false;
    s_out->silence(32);
    ampOn(true);
    ok = s_gen->begin(s_buf, s_out);
  }
  if (!ok) { freeChain(); ampOn(false); }
  xSemaphoreGive(s_mtx);
  return ok;
}

void stop() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  bool was = s_gen != nullptr;
  freeChain();
  s_paused = false;
  if (was) { s_out->stop(); s_out->silence(128); ampOn(false); }
  xSemaphoreGive(s_mtx);
}

void togglePause() { if (s_gen) s_paused = !s_paused; }
bool isPlaying() { return s_gen != nullptr; }
bool isPaused() { return s_paused; }
bool finished() { bool f = s_finished; s_finished = false; return f; }

float progress() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  float p = 0;
  if (s_file && s_file->getSize()) p = (float)s_file->getPos() / s_file->getSize();
  xSemaphoreGive(s_mtx);
  return p;
}

int sampleRate() { return s_rate; }
uint32_t elapsedMs() { return (uint32_t)(s_samplesOut * 1000ULL / (uint64_t)max(8000, (int)s_rate)); }
String title() { return s_title; }
String artist() { return s_artist; }

static void onMeta(void*, const char* type, bool isUnicode, const char* str) {
  if (isUnicode) return;  // UTF-16 tags: fall back to the file name
  if (!strcmp(type, "Title")) s_title = str;
  else if (!strcmp(type, "Artist") || !strcmp(type, "Performer")) s_artist = str;
}

void setVolume(uint8_t v) {
  if (v > 21) v = 21;
  s_gain = v == 0 ? 0.0f : powf(10.0f, (int)(v - 21) * 2.0f / 20.0f);
}

void setEq(const int8_t db[EQ_BANDS]) {
  for (int i = 0; i < EQ_BANDS; i++) s_eqDb[i] = constrain(db[i], -12, 12);
  s_eqDirty = true;
}

void scope(int16_t* out, int n) {
  if (n > SCOPE_LEN) n = SCOPE_LEN;
  uint32_t end = s_ringPos;
  for (int i = 0; i < n; i++) out[i] = s_ring[(end - n + i) & (SCOPE_LEN - 1)];
}

void beep(uint16_t freq, uint16_t ms, uint8_t vol) {
  if (s_gen) return;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  i2s_set_sample_rates(PORT, 22050);
  ampOn(true);
  int n = 22050 * ms / 1000;
  int half = 22050 / (2 * max<int>(freq, 50));
  int16_t amp = vol * 1200;
  uint32_t buf[64];
  size_t w;
  int ph = 0;
  for (int i = 0; i < n;) {
    int k = 0;
    for (; k < 64 && i < n; k++, i++) {
      int16_t s = ((ph++ / half) & 1) ? amp : -amp;
      uint16_t u = (uint16_t)(s + 32768);
      buf[k] = ((uint32_t)u << 16) | u;
    }
    i2s_write(PORT, buf, k * 4, &w, portMAX_DELAY);
  }
  s_out->silence(256);
  delay(15);
  ampOn(false);
  s_out->stop();
  s_out->invalidateRate();
  xSemaphoreGive(s_mtx);
}

}  // namespace audio
