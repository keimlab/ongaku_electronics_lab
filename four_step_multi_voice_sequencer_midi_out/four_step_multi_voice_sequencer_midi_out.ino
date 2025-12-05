/*
  four_step_multi_voice_sequencer - MIDI OUT版 (Pro Micro用)

  - USB MIDIは使わず、DIN5ピンのMIDI OUTで外部機器を制御
  - 4ステップ × 4ボイス（Kick / Snare / Hat / Crash想定）
  - テンポ固定: BPM = 127
  - ボタン:
      D2..D5  : 4ステップボタン（現在選択中の音色のステップON/OFF）
      D10     : 再生/停止トグル
      D16     : 音色切替ボタン（Kick → Snare → Hat → Crash → …）
  - LED:
      D6..D9  : 現在選択中の音色のパターンを表示 (ON=鳴るステップ)
      D30     : 再生インジケータ（ステップ毎に点滅）
  - MIDI OUT:
      TX0 (D1) を使用して Serial1 から 31250bps で送信
      DIN5コネクタ: ピン5 = TX0側, ピン4 = +5V側（220Ω経由）, ピン2 = GND
*/

/// ====== CONFIG ======

// ボイス設定（後で好きに変えてOK）
const uint8_t VOICE_COUNT = 4;
const uint8_t VOICE_NOTES[VOICE_COUNT] = {
  60, // Voice0: Kick   (仮)
  65, // Voice1: Snare  (仮)
  72, // Voice2: Hat    (仮)
  75  // Voice3: Crash  (仮)
};

// MIDI設定
// ※MIDIチャンネル1 → コード内部では 0
const uint8_t  MIDI_CHANNEL  = 0;    // Ch1
const uint8_t  NOTE_VELOCITY = 110;
const uint16_t GATE_MS_BASE  = 120;  // ノート長（ms）
const int      FIXED_BPM     = 127;  // テンポ固定

// ピンアサイン
const uint8_t STEP_BTN[4]   = {2, 3, 4, 5};  // ステップボタン
const uint8_t STEP_LED[4]   = {6, 7, 8, 9};  // ステップLED
const uint8_t PLAY_BTN_PIN  = 10;            // 再生/停止ボタン
const uint8_t INST_BTN_PIN  = 16;            // 音色切替ボタン
const uint8_t TX_LED_PIN    = 30;            // 再生インジケータ（TX LED）

// デバウンス時間
const uint16_t DEBOUNCE_STEP_MS = 20;
const uint16_t DEBOUNCE_PLAY_MS = 30;
const uint16_t DEBOUNCE_INST_MS = 30;

/// ====== STATE ======

struct Btn {
  uint8_t  pin;
  bool     stable;
  bool     lastRead;
  uint32_t lastChangeMs;
};

Btn stepBtn[4];
Btn playBtn;
Btn instBtn;

// pattern[voice][step]: 各音色ごとの4ステップパターン
bool pattern[VOICE_COUNT][4];

// 再生状態
bool     isPlaying      = false;
uint8_t  stepIndex      = 0;
uint32_t stepIntervalMs = 0;
uint32_t nextStepAtMs   = 0;

// 各ボイスのノートON/OFF状態
bool     noteActive[VOICE_COUNT];
uint32_t noteOffAtMs[VOICE_COUNT];

// 現在編集対象の音色インデックス (0..VOICE_COUNT-1)
uint8_t currentVoice = 0;

/// ====== MIDI 送信ヘルパ ======

// 3バイトのMIDIメッセージを送信
inline void midiSend3(uint8_t b1, uint8_t b2, uint8_t b3) {
  Serial1.write(b1);
  Serial1.write(b2);
  Serial1.write(b3);
}

inline void sendNoteOn(uint8_t note, uint8_t vel, uint8_t ch) {
  uint8_t status = 0x90 | (ch & 0x0F); // Note On, channel
  midiSend3(status, note, vel);
}

inline void sendNoteOff(uint8_t note, uint8_t vel, uint8_t ch) {
  uint8_t status = 0x80 | (ch & 0x0F); // Note Off, channel
  midiSend3(status, note, vel);
}

/// ====== ボタン共通デバウンス ======

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

/// ====== LED表示（現在の音色のパターンを反映） ======

void refreshStepLedsForCurrentVoice() {
  for (int step = 0; step < 4; step++) {
    digitalWrite(STEP_LED[step], pattern[currentVoice][step] ? HIGH : LOW);
  }
}

/// ====== SETUP ======

void setup() {
  // MIDI OUT初期化（シリアル1をMIDIボーレートで）
  Serial1.begin(31250);

  // TX LED 初期化
  pinMode(TX_LED_PIN, OUTPUT);
  digitalWrite(TX_LED_PIN, LOW);

  // パターン初期化（全ボイス全ステップ = OFF）
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

  // 最初の編集対象音色 = 0 (Kick想定)
  currentVoice = 0;
  refreshStepLedsForCurrentVoice();

  // 固定テンポからステップ間隔を算出
  stepIntervalMs = (uint32_t)(60000UL / FIXED_BPM);
  nextStepAtMs = millis() + stepIntervalMs;
}

/// ====== LOOP ======

void loop() {
  handleInstrumentButton();
  handleStepButtons();
  handlePlayButton();
  runSequencer();
}

/// ====== ボタン処理 ======

// 音色切替ボタン：押すごとに 0→1→2→3→0→… と切替
void handleInstrumentButton() {
  if (pollEdgeFalling(instBtn, DEBOUNCE_INST_MS)) {
    currentVoice = (currentVoice + 1) % VOICE_COUNT;
    refreshStepLedsForCurrentVoice();
  }
}

// ステップボタン：現在の音色のパターンをON/OFF
void handleStepButtons() {
  for (int s = 0; s < 4; s++) {
    if (pollEdgeFalling(stepBtn[s], DEBOUNCE_STEP_MS)) {
      pattern[currentVoice][s] = !pattern[currentVoice][s];
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
      // 再生開始時にLEDを一度OFFにしておく
      digitalWrite(TX_LED_PIN, LOW);
    } else {
      // 停止時は全ボイスのノートを強制OFF
      for (int v = 0; v < VOICE_COUNT; v++) {
        if (noteActive[v]) {
          sendNoteOff(VOICE_NOTES[v], 0, MIDI_CHANNEL);
          noteActive[v] = false;
        }
      }
      // 再生停止時はLEDも消灯
      digitalWrite(TX_LED_PIN, LOW);
    }
  }
}

/// ====== シーケンサー本体 ======

void runSequencer() {
  const uint32_t now = millis();

  // 各ボイスのノートOFF処理
  for (int v = 0; v < VOICE_COUNT; v++) {
    if (noteActive[v] && (int32_t)(now - noteOffAtMs[v]) >= 0) {
      sendNoteOff(VOICE_NOTES[v], 0, MIDI_CHANNEL);
      noteActive[v] = false;
    }
  }

  if (!isPlaying) return;

  // 次ステップへ進むタイミングか？
  if ((int32_t)(now - nextStepAtMs) >= 0) {

    // ★ 再生中インジケータLEDをトグル点滅
    digitalWrite(TX_LED_PIN, !digitalRead(TX_LED_PIN));

    // 全ボイスについて、現在ステップのパターンを確認してNote On
    for (int v = 0; v < VOICE_COUNT; v++) {
      if (pattern[v][stepIndex]) {
        uint16_t gate = GATE_MS_BASE;
        if (gate > stepIntervalMs) {
          gate = (stepIntervalMs > 10) ? (stepIntervalMs - 10) : stepIntervalMs;
        }

        sendNoteOn(VOICE_NOTES[v], NOTE_VELOCITY, MIDI_CHANNEL);
        noteActive[v]   = true;
        noteOffAtMs[v]  = now + gate;
      }
    }

    // 次ステップへ
    stepIndex = (stepIndex + 1) & 0x03;  // 0..3
    nextStepAtMs = now + stepIntervalMs;
  }
}