#include <MIDIUSB.h>

/// ====== CONFIG ======
const uint8_t VOICE_COUNT = 4;
const uint8_t VOICE_NOTES[VOICE_COUNT] = {
  36,  // Voice 0
  38,  // Voice 1
  42,  // Voice 2
  49   // Voice 3
};

// MIDI
const uint8_t MIDI_CHANNEL  = 0;   // CH1
const uint8_t NOTE_VELOCITY = 110;

// Pins
const uint8_t STEP_BTN[4]  = {2, 3, 4, 5};
const uint8_t STEP_LED[4]  = {6, 7, 8, 9};
const uint8_t PLAY_BTN     = 10;
const uint8_t INST_BTN     = 16;

// Pots
const uint8_t TEMPO_POT = A0;   // Tempo (0–240 BPM, 0 = STOP)
const uint8_t SWING_POT = A1;   // Swing (center = 0)

// Timing
const uint16_t GATE_MS = 60;
const float    MAX_SWING_RATIO = 0.6f; // ±60%（前ノリ/後ノリ）

/// ====== STATE ======
struct Btn {
  uint8_t pin;
  bool stable;
  bool lastRead;
  uint32_t lastChange;
};

Btn stepBtn[4], playBtn, instBtn;

bool pattern[VOICE_COUNT][4];
bool noteActive[VOICE_COUNT];
uint32_t noteOffAt[VOICE_COUNT];

uint8_t currentVoice = 0;
uint8_t stepIndex = 0;
bool isPlaying = false;

uint32_t nextStepAt = 0;
uint32_t stepIntervalMs = 0;

/// ====== MIDI HELPERS ======
inline void sendNoteOn(uint8_t note) {
  midiEventPacket_t p = {0x09, uint8_t(0x90 | MIDI_CHANNEL), note, NOTE_VELOCITY};
  MidiUSB.sendMIDI(p);
}

inline void sendNoteOff(uint8_t note) {
  midiEventPacket_t p = {0x08, uint8_t(0x80 | MIDI_CHANNEL), note, 0};
  MidiUSB.sendMIDI(p);
}

/// ====== UTILS ======
bool pollFalling(Btn &b, uint16_t debounceMs) {
  uint32_t now = millis();
  bool r = digitalRead(b.pin);

  if (r != b.lastRead) {
    b.lastRead = r;
    b.lastChange = now;
  }

  if ((now - b.lastChange) > debounceMs && r != b.stable) {
    bool falling = (b.stable == HIGH && r == LOW);
    b.stable = r;
    return falling;
  }
  return false;
}

void refreshLEDs() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(STEP_LED[i], pattern[currentVoice][i] ? HIGH : LOW);
  }
}

/// ====== SETUP ======
void setup() {
  for (int v = 0; v < VOICE_COUNT; v++) {
    for (int s = 0; s < 4; s++) pattern[v][s] = false;
    noteActive[v] = false;
  }

  for (int i = 0; i < 4; i++) {
    pinMode(STEP_LED[i], OUTPUT);
    pinMode(STEP_BTN[i], INPUT_PULLUP);
    stepBtn[i] = {STEP_BTN[i], HIGH, HIGH, 0};
  }

  pinMode(PLAY_BTN, INPUT_PULLUP);
  pinMode(INST_BTN, INPUT_PULLUP);
  playBtn = {PLAY_BTN, HIGH, HIGH, 0};
  instBtn = {INST_BTN, HIGH, HIGH, 0};

  refreshLEDs();
}

/// ====== LOOP ======
void loop() {
  uint32_t now = millis();

  // ---- Instrument select
  if (pollFalling(instBtn, 30)) {
    currentVoice = (currentVoice + 1) % VOICE_COUNT;
    refreshLEDs();
  }

  // ---- Step buttons
  for (int i = 0; i < 4; i++) {
    if (pollFalling(stepBtn[i], 20)) {
      pattern[currentVoice][i] = !pattern[currentVoice][i];
      digitalWrite(STEP_LED[i], pattern[currentVoice][i] ? HIGH : LOW);
    }
  }

  // ---- Play toggle
  if (pollFalling(playBtn, 30)) {
    isPlaying = !isPlaying;
    stepIndex = 0;
    nextStepAt = now;

    // 停止時：鳴りっぱなし防止
    if (!isPlaying) {
      for (int v = 0; v < VOICE_COUNT; v++) {
        if (noteActive[v]) {
          sendNoteOff(VOICE_NOTES[v]);
          noteActive[v] = false;
        }
      }
      MidiUSB.flush();
    }
  }

  // ---- Tempo (A0): 0..1023 -> 0..240 BPM
  int tempoRaw = analogRead(TEMPO_POT);
  float bpm = (tempoRaw / 1023.0f) * 240.0f;

  // 0 BPM 付近は完全停止
  if (bpm <= 0.5f) return;

  stepIntervalMs = (uint32_t)(60000.0f / bpm);

  // ---- Swing (A1): -1.0 .. +1.0
  int swingRaw = analogRead(SWING_POT);
  float swingNorm  = (swingRaw - 512) / 512.0f;
  float swingRatio = swingNorm * MAX_SWING_RATIO;

  // ---- Note OFF
  for (int v = 0; v < VOICE_COUNT; v++) {
    if (noteActive[v] && now >= noteOffAt[v]) {
      sendNoteOff(VOICE_NOTES[v]);
      noteActive[v] = false;
    }
  }

  if (!isPlaying) return;

  // ---- Step advance
  if (now >= nextStepAt) {

    // ノートON
    for (int v = 0; v < VOICE_COUNT; v++) {
      if (pattern[v][stepIndex]) {
        sendNoteOn(VOICE_NOTES[v]);
        noteActive[v] = true;
        noteOffAt[v] = now + GATE_MS;
      }
    }
    MidiUSB.flush();

    // ===== Tempo-fixed Swing =====
    // 表拍（0,2）：base - offset
    // 裏拍（1,3）：base + offset
    int32_t base   = (int32_t)stepIntervalMs;
    int32_t offset = (int32_t)(base * swingRatio);

    int32_t delta;
    if ((stepIndex % 2) == 0) {
      delta = base - offset;
    } else {
      delta = base + offset;
    }

    // 安全下限（前ノリで負にならないように）
    if (delta < 1) delta = 1;

    nextStepAt = now + (uint32_t)delta;
    stepIndex = (stepIndex + 1) & 0x03;
  }
}