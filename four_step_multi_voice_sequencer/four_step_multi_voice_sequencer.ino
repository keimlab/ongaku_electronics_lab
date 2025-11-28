/*
  Pro Micro 4-Step USB-MIDI Drum Sequencer (4 voices, fixed tempo)

  - Tempo: fixed BPM = 127
  - Buttons:
      D2..D5  : 4 step buttons (timing input)
      D10     : Play/Stop toggle
      D16     : Instrument select (Kick -> Snare -> Hat -> Crash -> ...)
  - LEDs:
      D6..D9  : 4 step LEDs
                → 現在選択中の音色のパターンを表示 (ON=そのステップで鳴る)
  - MIDI:
      USB-MIDI (MIDIUSB)
      4 voices: Kick, Snare, Hat, Crash (note numbers are placeholders)
*/

#include <MIDIUSB.h>

/*** ====== CONFIG ====== ***/
// 4つの音色（後で好きに変えてOK）
const uint8_t VOICE_COUNT = 4;
const uint8_t VOICE_NOTES[VOICE_COUNT] = {
  36, // Kick
  38, // Snare
  42, // Hat (Closed)
  49  // Crash
};

const uint8_t  MIDI_CHANNEL  = 9;    // GM drums = Ch10 (index 9)
const uint8_t  NOTE_VELOCITY = 110;
const uint16_t GATE_MS_BASE  = 120;  // 各ノートの長さ(ms)
const int      FIXED_BPM     = 127;  // ← テンポ固定

// Pins
const uint8_t STEP_BTN[4]   = {2, 3, 4, 5};  // 4ステップ入力
const uint8_t STEP_LED[4]   = {6, 7, 8, 9};  // 4ステップ表示
const uint8_t PLAY_BTN_PIN  = 10;            // 再生/停止
const uint8_t INST_BTN_PIN  = 16;            // 音色選択ボタン（新規）

// Debounce
const uint16_t DEBOUNCE_STEP_MS = 20;
const uint16_t DEBOUNCE_PLAY_MS = 30;
const uint16_t DEBOUNCE_INST_MS = 30;

/*** ====== STATE ====== ***/
struct Btn {
  uint8_t  pin;
  bool     stable;
  bool     lastRead;
  uint32_t lastChangeMs;
};

Btn stepBtn[4];
Btn playBtn;
Btn instBtn;

// pattern[voice][step] : voice=0..3, step=0..3
bool pattern[VOICE_COUNT][4];

// 再生系
bool     isPlaying      = false;
uint8_t  stepIndex      = 0;
uint32_t stepIntervalMs = 0;
uint32_t nextStepAtMs   = 0;

// 各ボイスごとのNote ON/OFF管理
bool     noteActive[VOICE_COUNT];
uint32_t noteOffAtMs[VOICE_COUNT];

// 現在「入力編集している音色」
uint8_t currentVoice = 0;

/*** ====== MIDI helpers ====== ***/
inline void sendNoteOn(uint8_t note, uint8_t vel, uint8_t ch) {
  midiEventPacket_t p = {0x09, (uint8_t)(0x90 | (ch & 0x0F)), note, vel};
  MidiUSB.sendMIDI(p); MidiUSB.flush();
}
inline void sendNoteOff(uint8_t note, uint8_t vel, uint8_t ch) {
  midiEventPacket_t p = {0x08, (uint8_t)(0x80 | (ch & 0x0F)), note, vel};
  MidiUSB.sendMIDI(p); MidiUSB.flush();
}

/*** ====== BUTTON UTILS ====== ***/
bool pollEdgeFalling(Btn &b, uint16_t debounceMs) {
  const uint32_t now = millis();
  bool r = digitalRead(b.pin);
  if (r != b.lastRead) {
    b.lastChangeMs = now;
    b.lastRead = r;
  }
  if ((now - b.lastChangeMs) > debounceMs) {
    if (r != b.stable) {
      bool falling = (b.stable == HIGH && r == LOW); // HIGH→LOW = 押下
      b.stable = r;
      return falling;
    }
  }
  return false;
}

/*** ====== LED 表示更新 ====== ***/
void refreshStepLedsForCurrentVoice() {
  for (int step = 0; step < 4; step++) {
    digitalWrite(STEP_LED[step], pattern[currentVoice][step] ? HIGH : LOW);
  }
}

/*** ====== SETUP ====== ***/
void setup() {
  // パターン初期化（全部オフ）
  for (int v = 0; v < VOICE_COUNT; v++) {
    for (int s = 0; s < 4; s++) {
      pattern[v][s] = false;
    }
    noteActive[v] = false;
    noteOffAtMs[v] = 0;
  }

  // ステップLED
  for (int i = 0; i < 4; i++) {
    pinMode(STEP_LED[i], OUTPUT);
    digitalWrite(STEP_LED[i], LOW);
  }

  // ステップボタン
  for (int i = 0; i < 4; i++) {
    stepBtn[i].pin = STEP_BTN[i];
    pinMode(stepBtn[i].pin, INPUT_PULLUP);
    stepBtn[i].stable = digitalRead(stepBtn[i].pin);
    stepBtn[i].lastRead = stepBtn[i].stable;
    stepBtn[i].lastChangeMs = millis();
  }

  // 再生ボタン
  playBtn.pin = PLAY_BTN_PIN;
  pinMode(playBtn.pin, INPUT_PULLUP);
  playBtn.stable = digitalRead(playBtn.pin);
  playBtn.lastRead = playBtn.stable;
  playBtn.lastChangeMs = millis();

  // 音色切替ボタン
  instBtn.pin = INST_BTN_PIN;
  pinMode(instBtn.pin, INPUT_PULLUP);
  instBtn.stable = digitalRead(instBtn.pin);
  instBtn.lastRead = instBtn.stable;
  instBtn.lastChangeMs = millis();

  // 最初の音色 = 0 (Kick想定)
  currentVoice = 0;
  refreshStepLedsForCurrentVoice();

  // 固定テンポからステップ間隔を算出
  stepIntervalMs = (uint32_t)(60000UL / FIXED_BPM);
  nextStepAtMs = millis() + stepIntervalMs;
}

/*** ====== LOOP ====== ***/
void loop() {
  handleInstrumentButton();
  handleStepButtons();
  handlePlayButton();
  runSequencer();
}

/*** ====== ハンドラ群 ====== ***/

// 音色切替ボタン：押すごとに voice 0→1→2→3→0… をループ
void handleInstrumentButton() {
  if (pollEdgeFalling(instBtn, DEBOUNCE_INST_MS)) {
    currentVoice = (currentVoice + 1) % VOICE_COUNT;
    refreshStepLedsForCurrentVoice();
  }
}

// ステップボタン：現在の音色のそのステップをON/OFF
void handleStepButtons() {
  for (int s = 0; s < 4; s++) {
    if (pollEdgeFalling(stepBtn[s], DEBOUNCE_STEP_MS)) {
      pattern[currentVoice][s] = !pattern[currentVoice][s];
      // LED表示は「現在の音色のパターン」
      digitalWrite(STEP_LED[s], pattern[currentVoice][s] ? HIGH : LOW);
    }
  }
}

// 再生/停止ボタン
void handlePlayButton() {
  if (pollEdgeFalling(playBtn, DEBOUNCE_PLAY_MS)) {
    isPlaying = !isPlaying;
    if (isPlaying) {
      nextStepAtMs = millis() + stepIntervalMs;
    } else {
      // 止めるときは全部のノートを強制OFF
      for (int v = 0; v < VOICE_COUNT; v++) {
        if (noteActive[v]) {
          sendNoteOff(VOICE_NOTES[v], 0, MIDI_CHANNEL);
          noteActive[v] = false;
        }
      }
    }
  }
}

/*** ====== シーケンサー ====== ***/
void runSequencer() {
  const uint32_t now = millis();

  // 各ボイスのNote OFF処理
  for (int v = 0; v < VOICE_COUNT; v++) {
    if (noteActive[v] && (int32_t)(now - noteOffAtMs[v]) >= 0) {
      sendNoteOff(VOICE_NOTES[v], 0, MIDI_CHANNEL);
      noteActive[v] = false;
    }
  }

  if (!isPlaying) return;

  // ステップ進行
  if ((int32_t)(now - nextStepAtMs) >= 0) {
    // すべての音色について、現在ステップにパターンがあればノートON
    for (int v = 0; v < VOICE_COUNT; v++) {
      if (pattern[v][stepIndex]) {
        uint16_t gate = GATE_MS_BASE;
        if (gate > stepIntervalMs) {
          gate = (stepIntervalMs > 10) ? (stepIntervalMs - 10) : stepIntervalMs;
        }
        sendNoteOn(VOICE_NOTES[v], NOTE_VELOCITY, MIDI_CHANNEL);
        noteActive[v] = true;
        noteOffAtMs[v] = now + gate;
      }
    }

    // 次ステップへ
    stepIndex = (stepIndex + 1) & 0x03;  // 0..3
    nextStepAtMs = now + stepIntervalMs;
  }
}