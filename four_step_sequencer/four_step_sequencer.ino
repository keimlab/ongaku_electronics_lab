/*
  Pro Micro 4-Step USB-MIDI Drum Sequencer (Fixed Tempo)
  - Tempo fixed at BPM = 127
  - Buttons: D2..D5 (step toggle), D10 (play toggle)
  - LEDs: D6..D9 (ON=enabled)
  - USB-MIDI: one fixed note (DRUM_NOTE)
*/

#include <MIDIUSB.h>

/*** ====== CONFIG ====== ***/
const uint8_t  DRUM_NOTE     = 36;   // 36=Kick
const uint8_t  MIDI_CHANNEL  = 9;    // Ch10 for GM drums
const uint8_t  NOTE_VELOCITY = 110;
const uint16_t GATE_MS_BASE  = 120;  // ms
const int      FIXED_BPM     = 127;  // ← テンポ固定

// Pins
const uint8_t STEP_BTN[4] = {2, 3, 4, 5};
const uint8_t STEP_LED[4] = {6, 7, 8, 9};
const uint8_t PLAY_BTN    = 10;

// Debounce
const uint16_t DEBOUNCE_STEP_MS = 20;
const uint16_t DEBOUNCE_PLAY_MS = 30;

/*** ====== STATE ====== ***/
struct Btn {
  uint8_t pin;
  bool    stable;
  bool    lastRead;
  uint32_t lastChangeMs;
};

Btn stepBtn[4];
Btn playBtn;

bool stepEnabled[4] = {true, true, true, true};

bool     isPlaying      = false;
uint8_t  stepIndex      = 0;
uint32_t stepIntervalMs = 0;
uint32_t nextStepAtMs   = 0;

bool     noteActive     = false;
uint32_t noteOffAtMs    = 0;

/*** ====== MIDI helpers ====== ***/
inline void sendNoteOn(uint8_t note, uint8_t vel, uint8_t ch) {
  midiEventPacket_t p = {0x09, (uint8_t)(0x90 | (ch & 0x0F)), note, vel};
  MidiUSB.sendMIDI(p); MidiUSB.flush();
}
inline void sendNoteOff(uint8_t note, uint8_t vel, uint8_t ch) {
  midiEventPacket_t p = {0x08, (uint8_t)(0x80 | (ch & 0x0F)), note, vel};
  MidiUSB.sendMIDI(p); MidiUSB.flush();
}

/*** ====== SETUP ====== ***/
void setup() {
  // LED
  for (int i = 0; i < 4; i++) {
    pinMode(STEP_LED[i], OUTPUT);
    digitalWrite(STEP_LED[i], stepEnabled[i] ? HIGH : LOW);
  }

  // Buttons
  for (int i = 0; i < 4; i++) {
    stepBtn[i].pin = STEP_BTN[i];
    pinMode(stepBtn[i].pin, INPUT_PULLUP);
    stepBtn[i].stable = digitalRead(stepBtn[i].pin);
    stepBtn[i].lastRead = stepBtn[i].stable;
    stepBtn[i].lastChangeMs = millis();
  }
  playBtn.pin = PLAY_BTN;
  pinMode(playBtn.pin, INPUT_PULLUP);
  playBtn.stable = digitalRead(playBtn.pin);
  playBtn.lastRead = playBtn.stable;
  playBtn.lastChangeMs = millis();

  // Tempo fixed to BPM=127
  stepIntervalMs = (uint32_t)(60000UL / FIXED_BPM);
  nextStepAtMs = millis() + stepIntervalMs;
}

/*** ====== LOOP ====== ***/
void loop() {
  handleStepButtons();
  handlePlayButton();
  runSequencer();
}

/*** ====== INPUT ====== ***/
bool pollEdgeFalling(Btn &b, uint16_t debounceMs) {
  const uint32_t now = millis();
  bool r = digitalRead(b.pin);
  if (r != b.lastRead) {
    b.lastChangeMs = now;
    b.lastRead = r;
  }
  if ((now - b.lastChangeMs) > debounceMs) {
    if (r != b.stable) {
      bool falling = (b.stable == HIGH && r == LOW);
      b.stable = r;
      return falling;
    }
  }
  return false;
}

void handleStepButtons() {
  for (int i = 0; i < 4; i++) {
    if (pollEdgeFalling(stepBtn[i], DEBOUNCE_STEP_MS)) {
      stepEnabled[i] = !stepEnabled[i];
      digitalWrite(STEP_LED[i], stepEnabled[i] ? HIGH : LOW);
    }
  }
}

void handlePlayButton() {
  if (pollEdgeFalling(playBtn, DEBOUNCE_PLAY_MS)) {
    isPlaying = !isPlaying;
    if (isPlaying) {
      nextStepAtMs = millis() + stepIntervalMs;
    } else {
      if (noteActive) {
        sendNoteOff(DRUM_NOTE, 0, MIDI_CHANNEL);
        noteActive = false;
      }
    }
  }
}

/*** ====== SEQUENCER ====== ***/
void runSequencer() {
  const uint32_t now = millis();

  // ノートOFF
  if (noteActive && (int32_t)(now - noteOffAtMs) >= 0) {
    sendNoteOff(DRUM_NOTE, 0, MIDI_CHANNEL);
    noteActive = false;
  }

  if (!isPlaying) return;

  if ((int32_t)(now - nextStepAtMs) >= 0) {
    if (stepEnabled[stepIndex]) {
      uint16_t gate = GATE_MS_BASE;
      if (gate > stepIntervalMs) gate = stepIntervalMs - 10;
      sendNoteOn(DRUM_NOTE, NOTE_VELOCITY, MIDI_CHANNEL);
      noteActive = true;
      noteOffAtMs = now + gate;
    }

    stepIndex = (stepIndex + 1) & 0x03;
    nextStepAtMs = now + stepIntervalMs;
  }
}