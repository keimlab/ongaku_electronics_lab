# 🎶 ongaku_electronics_lab

Arduino / Pro Micro / Teensy を使った **音楽 × 電子工作** の実験室。  
MIDIシーケンサー、コントローラー、サウンドビジュアライザーなど、  
音とインタラクションをテーマにした自作ハードウェアを公開しています。

---

## 🧠 Philosophy

「演奏」と「設計」のあいだにある創造性を、手を動かしながら探る。  
**音を自分で“設計”する感覚**を、そのまま形にしていくための研究ノートです。

- 電子回路と音  
- インタラクションとフィードバック  
- 軽量なハードウェアと創造性  

これらが交差する領域を、実験と試作を通して探求しています。

---

## 📦 Projects

| Project | Description |
|--------|-------------|
| [four_step_sequencer/](four_step_sequencer) | 4ステップ USB-MIDI シーケンサーの基礎版 |
| [four_step_multi_voice_sequencer/](four_step_multi_voice_sequencer) | 4ステップ × 音色切り替え（マルチボイス）対応の USB-MIDI 発展版 |
| [four_step_multi_voice_sequencer_tempo_swing/](four_step_multi_voice_sequencer_tempo_swing) | **テンポ固定＋スイング（前ノリ／後ノリ）対応**の完成形 USB-MIDI シーケンサー |
| [four_step_multi_voice_sequencer_midi_out/](four_step_multi_voice_sequencer_midi_out) | 外部ハードウェアを直接鳴らす **MIDI OUT（DIN5）対応版** |

_※ 今後、MIDIコントローラー、モジュラー向けインターフェイス、ビジュアライザーなども追加予定。_

---

## 🔧 Environment

- Arduino IDE / PlatformIO  
- Pro Micro (ATmega32U4)  
- USB-MIDI または **DIN MIDI OUT**（プロジェクトによる）  
- 5V ロジック  
- ソフト音源・ハードウェア音源の両方に対応

---

## 📜 License

MIT License © 2025 keimlab  
自由に改造・再利用いただけます。出典の明記を推奨します。

---

## 🌐 Links

- 🧪 Note (制作記・考察): https://note.com/keimlab  
- 🐦 X (updates): https://x.com/keimlab  
- 💻 GitHub: https://github.com/keimlab