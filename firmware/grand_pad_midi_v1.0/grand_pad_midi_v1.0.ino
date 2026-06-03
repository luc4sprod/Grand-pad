/*
 * Macropad MIDI Controller - v2.3
 *
 * PINAGEM
 * ───────────────────────────────────────────────────────────────────────
 *  GP0 –GP15  → 16 pads  (notas / acordes / harpejo / escalas)
 *  GP16       → C7  → Play/Pause DAW  (CC 29, toggle 127/0)
 *  GP17       → C6  → Oitava −
 *  GP18       → C5  → Oitava +
 *  GP19       → C4  → Sustain touch  (CC 64, hold=127, release=0)
 *  GP20       → C3  → Modo Harpejo
 *  GP21       → C2  → Modo Acorde
 *  GP22       → C1  → Modo Cromático
 *  GP26       → OLED SDA (I2C1)
 *  GP27       → OLED SCL (I2C1)
 *  GP28       → BOOTSEL  (segurar 2 s → modo UF2)
 *  GP25       → LED onboard
 *
 * BOTÕES C1–C7 (fora do menu)
 * ───────────────────────────────────────────────────────────────────────
 *  C1 (GP22) → Seleciona modo Cromático
 *  C2 (GP21) → Seleciona modo Acorde
 *  C3 (GP20) → Seleciona modo Harpejo
 *  C4 (GP19) → Sustain touch: pressionar → CC64=127, soltar → CC64=0
 *  C5 (GP18) → Oitava +1  (máx +2)
 *  C6 (GP17) → Oitava −1  (mín −2)
 *  C7 (GP16) → Play/Pause DAW  (CC 29 canal 1, alterna 127↔0)
 *
 * BOTÕES C1–C7 (dentro do menu)
 * ───────────────────────────────────────────────────────────────────────
 *  C1 (GP22) → Cursor ▲
 *  C2 (GP21) → Cursor ▼
 *  C3 (GP20) → Ajuste −  (decrementa valor na pág Config)
 *  C4 (GP19) → Ajuste + / Confirmar seleção
 *  C5 (GP18) → Próxima página (cicla 1→2→3→4→5→1)
 *  C6 (GP17) → Back / fechar menu
 *  C7 (GP16) → Salvar preset no slot da página atual
 *
 * MENU (4 pads do topo GP12–GP15 simultâneos)
 * ───────────────────────────────────────────────────────────────────────
 *  Pág 1 – Escalas     (8 itens) — afeta modos Scaled e Harpejo
 *  Pág 2 – Acordes     (8 itens) — afeta modos Acorde e Harpejo
 *  Pág 3 – Modos       (6 itens: Scaled / Cromático / Acorde /
 *                                Harpejo / Generativo / OmniChord)
 *  Pág 4 – Config      (7 itens: Vel / Oitava / Transpose /
 *                                Ch Pad / Ch Clip / Root Note / Sustain Fix)
 *  Pág 5 – Presets     (4 slots)
 *
 * HARPEJO
 * ───────────────────────────────────────────────────────────────────────
 *  Cada pad (0–15) dispara um harpejo ascendente das notas do voicing
 *  correspondente (mesma tabela de acordes), com BPM sincronizado ao
 *  tempo definido no menu Config.  O harpejo continua enquanto o pad
 *  estiver pressionado e para (nota off) ao soltar.
 *  O tipo de escala/voicing é o mesmo da seleção de Acordes (página 2).
 *
 * SUSTAIN TOUCH (C4, fora do menu)
 * ───────────────────────────────────────────────────────────────────────
 *  Pressionar → envia CC 64 = 127.  Soltar → envia CC 64 = 0.
 *  Independente do "Sustain Fix" configurável no menu.
 *
 * PLAY/PAUSE DAW (C7, fora do menu)
 * ───────────────────────────────────────────────────────────────────────
 *  Alterna CC 29 (canal 1) entre 127 e 0 a cada pressão.
 *  Exibe símbolo ▶ ou ⏸ na tela de performance.
 *
 * BOOTSEL
 * ───────────────────────────────────────────────────────────────────────
 *  Segurar GP28 por 2 s → reset_usb_boot() → modo UF2 para reflash
 */

#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/bootrom.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLED ──────────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET      -1
#define OLED_ADDRESS  0x3C
#define OLED_SDA        26
#define OLED_SCL        27
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

// ── MIDI ──────────────────────────────────────────────────────────────────────
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// ── PINOS ─────────────────────────────────────────────────────────────────────
static const byte PAD_PINS[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
#define LED_PIN 25

// C1=GP22, C2=GP21, C3=GP20, C4=GP19, C5=GP18, C6=GP17, C7=GP16
static const byte CLIP_PINS[7] = {22, 21, 20, 19, 18, 17, 16};
#define NUM_CLIPS 7

#define RESET_PIN     28
#define RESET_HOLD_MS 2000UL

// CC para play/pause DAW
#define CC_DAW_PLAY 29

// ── MODOS DE PLAY ─────────────────────────────────────────────────────────────
enum PlayStyle : byte {
  STYLE_SCALED = 0,
  STYLE_CHROMATIC,
  STYLE_CHORD,
  STYLE_ARPEGIO,
  STYLE_GENERATIVE,
  STYLE_OMNI_CHORD
};
#define NUM_STYLES 6

// ── ESCALAS ───────────────────────────────────────────────────────────────────
static const int8_t SCALES[8][16] PROGMEM = {
  {0,2,4,5,7,9,11,12,14,16,17,19,21,23,24,26},   // Major
  {0,2,3,5,7,8,10,12,14,15,17,19,20,22,24,26},   // Minor
  {0,3,5,6,7,10,12,15,17,18,19,22,24,27,29,30},  // Blues
  {0,2,3,5,7,8,11,12,14,15,17,19,20,23,24,26},   // Harmonic
  {0,2,4,7,9,12,14,16,19,21,24,26,28,31,33,36},  // Pent Maj
  {0,3,5,7,10,12,15,17,19,22,24,27,29,31,34,36}, // Pent Min
  {0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26},   // Lydian
  {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}        // Chromatic
};
static const char* const SCALE_NAMES[8] PROGMEM = {
  "Major","Minor","Blues","Harmonic",
  "Pent Maj","Pent Min","Lydian","Chromatic"
};
static const byte CHORD_MODE_SCALES[8] PROGMEM = {0,1,2,3,0,1,0,0};

// ── VOICINGS DE ACORDE / HARPEJO ─────────────────────────────────────────────
// [chordMode][pad 0-7][nota 0-4]  — valor 127 = vazio
static const int8_t CHORD_VOICINGS[8][8][5] PROGMEM = {
  {{-12,7,12,14,16},{-10,5,7,12,16},{-8,7,11,12,16},{-7,7,9,12,16},
   {-5,7,11,12,14},{-3,7,11,12,16},{-1,7,11,12,14},{0,7,12,14,16}},
  {{-12,3,7,12,15},{-10,5,10,14,17},{-9,3,7,12,15},{-7,5,10,12,17},
   {-5,7,12,14,19},{-4,3,8,12,15},{-2,5,10,14,17},{0,7,12,15,19}},
  {{0,4,7,10,127},{2,5,9,12,127},{4,7,11,14,127},{5,9,12,15,127},
   {7,11,14,17,127},{9,12,16,19,127},{10,14,17,20,127},{0,4,7,10,127}},
  {{0,3,7,12,127},{2,5,8,12,127},{3,7,11,14,127},{5,8,12,15,127},
   {7,11,14,17,127},{8,12,15,19,127},{11,14,17,20,127},{3,7,12,15,127}},
  {{-12,0,7,16,19},{-10,2,9,17,21},{-8,4,11,19,23},{-7,5,12,21,24},
   {-5,7,14,23,26},{-3,9,16,24,28},{-1,12,19,26,31},{0,12,19,28,31}},
  {{0,7,12,15,127},{2,10,14,17,127},{3,7,10,15,127},{5,8,12,17,127},
   {7,10,14,19,127},{3,8,12,15,127},{5,10,14,17,127},{7,12,15,19,127}},
  {{4,11,14,19,127},{5,12,14,21,127},{7,11,16,19,127},{9,12,16,21,127},
   {11,16,17,23,127},{12,16,19,24,127},{14,17,21,26,127},{16,19,23,28,127}},
  {{0,4,7,12,127},{2,5,9,14,127},{4,7,11,16,127},{5,9,12,17,127},
   {7,11,14,19,127},{9,12,16,21,127},{11,14,17,23,127},{12,16,19,24,127}}
};

static const int8_t OMNI_CHORD_VOICINGS[8][8] PROGMEM = {
  {-12,0,4,7,12,16,19,24},{-10,2,5,9,14,17,21,26},{-8,4,7,11,16,19,23,28},
  {-7,5,9,12,17,21,24,29},{-5,7,11,14,19,23,26,31},{-3,9,12,16,21,24,28,33},
  {-1,11,14,18,23,26,30,35},{0,12,16,19,24,28,31,36}
};

static const char* const CHORD_NAMES[8] PROGMEM = {
  "Major","Minor","Blues","Harmonic",
  "Open","Minor2","Jazz 7th","Diatonic"
};
static const char* const STYLE_NAMES[NUM_STYLES] PROGMEM = {
  "Scaled","Chromatic","Chord","Arpegio","Generative","OmniChord"
};

// ── CONFIGURAÇÕES GLOBAIS ─────────────────────────────────────────────────────
static PlayStyle playStyle    = STYLE_SCALED;
static byte  currentScale     = 0;
static byte  currentChordMode = 0;
static int8_t transpose       = 0;
static int8_t octave          = -1;
static byte  velocity         = 100;
static byte  midiChannel      = 1;
static byte  clipChannel      = 1;   // mantido para compatibilidade menu
static byte  rootNote         = 60;  // C4
static bool  sustainFixed     = false;
static bool  dawPlaying       = false; // estado play/pause DAW

#define NO_NOTE ((int8_t)127)

// ── PRESETS / FLASH ───────────────────────────────────────────────────────────
struct Preset {
  PlayStyle style;
  byte  scale;
  byte  chordMode;
  int8_t transpose;
  int8_t octave;
  byte  velocity;
  bool  sustainFixed;
  byte  rootNote;
  byte  midiChannel;
};
static Preset presets[4];

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC 0x4D494450u   // 'MIDP' — novo magic força defaults ao trocar versão
struct FlashData { uint32_t magic; Preset presets[4]; };

// ── HARPEJO ───────────────────────────────────────────────────────────────────
// Para cada pad pode haver um harpejo ativo
struct ArpState {
  bool     active;          // pad está pressionado
  byte     padIdx;          // índice do pad (0–7 para voicings, 8-15 para escala)
  byte     noteCount;       // quantas notas válidas no voicing
  byte     notes[5];        // notas MIDI calculadas
  byte     step;            // próximo step do harpejo
  byte     lastNote;        // última nota tocada (para NoteOff)
  unsigned long nextTick;   // millis() do próximo disparo
};

#define ARP_MAX_PADS 16
static ArpState arpState[ARP_MAX_PADS]; // um por pad

// Velocidade do harpejo: bpm configurável (padrão 120 bpm = 500ms/beat / 4 = 125ms/16th)
static uint16_t arpBPM      = 120;
static byte     arpDivision = 4;   // divisão da batida (4 = semicolcheia)

// Calcula intervalo em ms entre steps do harpejo
static inline unsigned long arpInterval() {
  // (60000ms / BPM) / divisão
  return (unsigned long)(60000UL / arpBPM / arpDivision);
}

// ── GENERATIVO ────────────────────────────────────────────────────────────────
static unsigned long lastGenNoteTime  = 0;
static unsigned long nextGenNoteDelay = 0;
static byte currentGenNote            = 255;
#define MIN_GEN_DELAY 800UL
#define MAX_GEN_DELAY 3000UL
#define GEN_TRIGGER_CHANCE 70

// ── OMNI CHORD ────────────────────────────────────────────────────────────────
static byte omniChordIndex    = 0;
static byte omniChordNotes[8] = {255,255,255,255,255,255,255,255};

// ── MENU ─────────────────────────────────────────────────────────────────────
#define NUM_PAGES 5
static bool  inMenu      = false;
static byte  currentPage = 1;
static byte  menuCursor  = 0;

// Itens por página: Escalas(8) Acordes(8) Modos(6) Config(9) Presets(4)
static const byte PAGE_ITEMS[NUM_PAGES] PROGMEM = {8, 8, 6, 9, 4};

// ── SUSTAIN TOUCH (C4) ───────────────────────────────────────────────────────
static bool sustainTouchActive = false;

// ── PADS ─────────────────────────────────────────────────────────────────────
static bool padStates[16]                 = {false};
static bool lastPadStates[16]             = {false};
static unsigned long lastDebounceTime[16] = {0};
#define DEBOUNCE_DELAY 20

static const byte TOP_ROW[4]            = {12,13,14,15};
static unsigned long topRowPressTime[4] = {0,0,0,0};
static bool topRowPressed               = false;

// ── CLIPS / C1–C7 ────────────────────────────────────────────────────────────
static bool clipStates[NUM_CLIPS]           = {false};
static bool lastClipStates[NUM_CLIPS]       = {false};
static unsigned long clipDebounce[NUM_CLIPS]= {0};
#define CLIP_DEBOUNCE 25

// ── RESET (GP28) ──────────────────────────────────────────────────────────────
static bool resetBtnState         = false;
static bool lastResetBtnState     = false;
static unsigned long resetDebounce  = 0;
static unsigned long resetHoldStart = 0;
static bool resetArmed            = false;

// ── DISPLAY DIRTY FLAG ────────────────────────────────────────────────────────
static bool displayDirty = true;

// ─────────────────────────────────────────────────────────────────────────────
//  PROTÓTIPOS
// ─────────────────────────────────────────────────────────────────────────────
void updateDisplay();
void flushDisplay();
void drawPerformanceScreen();
void drawMenuPage();
void drawMenuHeader(const char* title, const char* hint = nullptr);
void drawListItem(int y, bool selected, bool active, const char* name);
void toggleMenu();
void handleClipPerformance(byte idx, bool pressed);
void handleClipInMenu(byte idx);
void handlePadPress(byte pad, unsigned long now);
void handlePadRelease(byte pad, unsigned long now);
void playNote(byte pad, bool noteOn);
void playChord(byte pad, bool noteOn);
void playOmniChord(byte pad, bool noteOn);
void startArpPad(byte pad, unsigned long now);
void stopArpPad(byte pad);
void updateArpegio(unsigned long now);
void updateGenerative(unsigned long now);
void menuConfirm();
void menuAdjust(int8_t dir);
void menuSavePreset();
void allNotesOff();
void loadPresetsFromFlash();
void savePresetsToFlash();
void loadPreset(byte idx);
void savePreset(byte idx);

// ── Helpers PROGMEM ──────────────────────────────────────────────────────────
static inline int8_t scaleNote(byte scl, byte idx) {
  return (int8_t)pgm_read_byte(&SCALES[scl][idx]);
}
static inline int8_t chordVoice(byte mode, byte pad, byte note) {
  return (int8_t)pgm_read_byte(&CHORD_VOICINGS[mode][pad][note]);
}
static inline int8_t omniVoice(byte idx, byte note) {
  return (int8_t)pgm_read_byte(&OMNI_CHORD_VOICINGS[idx][note]);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DISPLAY – PERFORMANCE
// ─────────────────────────────────────────────────────────────────────────────
/*
  Layout 128×64 (performance):
  ┌──────────────────────────────────────────┐
  │y= 0 │ ▶/⏸  [   MODO   ]  [  OITAVA  ]  │  h=16 (2 linhas grandes)
  │y= 8 │                                    │
  ├──────────────────────────────────────────┤  y=16 linha
  │y=18 │  ROOT: C4   ESCALA: Major          │  h=8
  │y=27 │  ACORDE: Major                     │  h=8  (só em Acorde/Harpejo)
  ├──────────────────────────────────────────┤  y=36 linha
  │y=38 │  Vel:100  Tr:+0  Ch:1             │  h=8
  │y=47 │  Sus:OFF  BPM:120 Div:4           │  h=8  (BPM só em harpejo)
  ├──────────────────────────────────────────┤  y=56 linha
  │y=57 │  4pads=MENU                        │  h=7
  └──────────────────────────────────────────┘
*/
// Nomes de notas para exibição
static const char* const NOTE_NAMES[12] = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static void noteLabel(byte midi, char* buf, byte buflen) {
  int oct = (int)(midi / 12) - 1;
  snprintf(buf, buflen, "%s%d", NOTE_NAMES[midi % 12], oct);
}

void drawPerformanceScreen() {
  char buf[24];
  display.setTextColor(SSD1306_WHITE);

  // ── Linha topo: símbolo play, modo, oitava ──
  // Símbolo play/pause  (tamanho 2 = 12×16)
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(dawPlaying ? ">" : "||");   // '>' = play, '||' = pause

  // Modo (caixa alta, tamanho 2, centrado)
  const char* sname = STYLE_NAMES[playStyle];
  display.setCursor(28, 0);
  display.print(sname);

  // Oitava no padrão piano: octave=-1 → "Oct:-1", etc.
  // Canto direito
  snprintf(buf, sizeof(buf), "O%+d", octave);
  display.setCursor(104, 0);
  display.print(buf);

  display.setTextSize(1);
  display.drawFastHLine(0, 17, 128, SSD1306_WHITE);

  // ── Root note e escala ──
  char rootBuf[5];
  noteLabel(rootNote, rootBuf, sizeof(rootBuf));
  snprintf(buf, sizeof(buf), "Root:%-4s  Esc:%-8s", rootBuf, SCALE_NAMES[currentScale]);
  display.setCursor(0, 19);
  display.print(buf);

  // ── Acorde (só relevante em Chord / Arpegio / OmniChord) ──
  if (playStyle == STYLE_CHORD || playStyle == STYLE_ARPEGIO ||
      playStyle == STYLE_OMNI_CHORD) {
    snprintf(buf, sizeof(buf), "Acorde:%-10s", CHORD_NAMES[currentChordMode]);
    display.setCursor(0, 28);
    display.print(buf);
  } else {
    // Linha em branco
    display.setCursor(0, 28);
    display.print("                     ");
  }

  display.drawFastHLine(0, 37, 128, SSD1306_WHITE);

  // ── Linha de parâmetros 1 ──
  snprintf(buf, sizeof(buf), "Vel:%-3d Tr:%+d Ch:%-2d", velocity, transpose, midiChannel);
  display.setCursor(0, 39);
  display.print(buf);

  // ── Linha de parâmetros 2 ──
  if (playStyle == STYLE_ARPEGIO) {
    snprintf(buf, sizeof(buf), "Sus:%-3s BPM:%-3d Div:%d",
             (sustainFixed || sustainTouchActive) ? "ON" : "OFF",
             arpBPM, arpDivision);
  } else {
    snprintf(buf, sizeof(buf), "Sus:%-3s",
             (sustainFixed || sustainTouchActive) ? "ON" : "OFF");
  }
  display.setCursor(0, 48);
  display.print(buf);

  display.drawFastHLine(0, 57, 128, SSD1306_WHITE);

  // ── Rodapé ──
  display.setCursor(0, 59);
  display.print("C1-3:modo C5/6:oct MENU");
}

// ─────────────────────────────────────────────────────────────────────────────
//  DISPLAY – MENU
// ─────────────────────────────────────────────────────────────────────────────
/*
  Layout menu 128×64:
  y=0   Header (título + pág N/5)         h=9
  y=9   linha
  y=11  item 0                             h=9
  y=20  item 1
  y=29  item 2
  y=38  item 3
  y=47  item 4  (máx 5 visíveis)
  y=56  linha
  y=57  hint navegação                     h=7
*/
void drawListItem(int y, bool selected, bool active, const char* name) {
  if (selected) {
    display.fillRect(0, y, 128, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(0, y + 1);
  display.print(active ? '*' : ' ');
  display.print(name);
  display.setTextColor(SSD1306_WHITE);
}

void drawMenuHeader(const char* title, const char* hint) {
  char buf[22];
  snprintf(buf, sizeof(buf), "%-15s%d/%d", title, currentPage, NUM_PAGES);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(buf);
  display.drawFastHLine(0, 9, 128, SSD1306_WHITE);
  display.drawFastHLine(0, 56, 128, SSD1306_WHITE);
  display.setCursor(0, 57);
  display.print(hint ? hint : "^v=-+ C5=pag C6=X C7=sv");
}

void drawMenuPage() {
  byte pageItems = pgm_read_byte(&PAGE_ITEMS[currentPage - 1]);
  byte firstVisible = (menuCursor > 4) ? menuCursor - 4 : 0;

  if (currentPage == 1) {
    // ── Escalas ──────────────────────────────────────────────────────────
    drawMenuHeader("Escalas", "^v=nav  +=sel  C6=X");
    for (byte i = 0; i < 5 && (firstVisible + i) < pageItems; i++) {
      byte idx = firstVisible + i;
      drawListItem(11 + i * 9, idx == menuCursor, idx == currentScale,
                   SCALE_NAMES[idx]);
    }

  } else if (currentPage == 2) {
    // ── Acordes ──────────────────────────────────────────────────────────
    drawMenuHeader("Acordes", "^v=nav  +=sel  C6=X");
    for (byte i = 0; i < 5 && (firstVisible + i) < pageItems; i++) {
      byte idx = firstVisible + i;
      drawListItem(11 + i * 9, idx == menuCursor, idx == currentChordMode,
                   CHORD_NAMES[idx]);
    }

  } else if (currentPage == 3) {
    // ── Modos ────────────────────────────────────────────────────────────
    drawMenuHeader("Modos", "^v=nav  +=sel  C6=X");
    for (byte i = 0; i < pageItems; i++) {
      drawListItem(11 + i * 9, i == menuCursor,
                   (byte)i == (byte)playStyle,
                   STYLE_NAMES[i]);
    }

  } else if (currentPage == 4) {
    // ── Config ───────────────────────────────────────────────────────────
    drawMenuHeader("Config", "^v=item  -/+=valor  C6=X");
    static const char* const cfgLabels[] = {
      "Velocidade","Oitava","Transpose","Ch Pad",
      "Ch Clip","Root Note","Sustain Fix","Arp BPM","Arp Div"
    };
    char nbuf[5];
    for (byte i = 0; i < 5 && (firstVisible + i) < pageItems; i++) {
      byte idx = firstVisible + i;
      char vbuf[8];
      switch (idx) {
        case 0: snprintf(vbuf, 8, "%d",   velocity);           break;
        case 1: snprintf(vbuf, 8, "%+d",  octave);             break;
        case 2: snprintf(vbuf, 8, "%+d",  transpose);          break;
        case 3: snprintf(vbuf, 8, "%d",   midiChannel);        break;
        case 4: snprintf(vbuf, 8, "%d",   clipChannel);        break;
        case 5: noteLabel(rootNote, nbuf, sizeof(nbuf));
                snprintf(vbuf, 8, "%s", nbuf);                 break;
        case 6: snprintf(vbuf, 8, "%s",   sustainFixed ? "ON":"OFF"); break;
        case 7: snprintf(vbuf, 8, "%d",   arpBPM);             break;
        case 8: snprintf(vbuf, 8, "%d",   arpDivision);        break;
      }
      char line[22];
      snprintf(line, sizeof(line), "%-11s%s", cfgLabels[idx], vbuf);
      drawListItem(11 + i * 9, idx == menuCursor, false, line);
    }

  } else if (currentPage == 5) {
    // ── Presets ──────────────────────────────────────────────────────────
    drawMenuHeader("Presets", "+=load  C7=save  C6=X");
    const char* pnames[] = {"Preset 1","Preset 2","Preset 3","Preset 4"};
    for (byte i = 0; i < pageItems; i++) {
      drawListItem(11 + i * 9, i == menuCursor, false, pnames[i]);
    }
  }
}

void updateDisplay() { displayDirty = true; }

void flushDisplay() {
  if (!displayDirty) return;
  displayDirty = false;
  display.clearDisplay();
  display.setTextSize(1);
  if (!inMenu) drawPerformanceScreen();
  else         drawMenuPage();
  display.display();
}

// ─────────────────────────────────────────────────────────────────────────────
//  MENU: AÇÕES
// ─────────────────────────────────────────────────────────────────────────────
void menuConfirm() {
  if (currentPage == 1) {
    currentScale = menuCursor;
    // Não altera modo automaticamente: usuário já escolheu no menu Modos
  } else if (currentPage == 2) {
    currentChordMode = menuCursor;
    currentScale = pgm_read_byte(&CHORD_MODE_SCALES[menuCursor]);
  } else if (currentPage == 3) {
    playStyle = (PlayStyle)menuCursor;
    if (playStyle == STYLE_OMNI_CHORD) currentScale = 0;
  } else if (currentPage == 4) {
    // Config: C4 = incrementa
    menuAdjust(+1);
    return;
  } else if (currentPage == 5) {
    loadPreset(menuCursor);
  }
  updateDisplay();
}

void menuAdjust(int8_t dir) {
  if (currentPage == 4) {
    switch (menuCursor) {
      case 0: velocity    = (byte)constrain((int)velocity   + dir * 5, 10, 127); break;
      case 1: octave      = (int8_t)constrain((int)octave   + dir, -2, 2);       break;
      case 2: transpose   = (int8_t)constrain((int)transpose+ dir, -12, 12);     break;
      case 3: midiChannel = (byte)((midiChannel - 1 + dir + 16) % 16 + 1);      break;
      case 4: clipChannel = (byte)((clipChannel  - 1 + dir + 16) % 16 + 1);     break;
      case 5: rootNote    = (byte)constrain((int)rootNote   + dir, 0, 127);      break;
      case 6:
        sustainFixed = !sustainFixed;
        MIDI.sendControlChange(64, sustainFixed ? 127 : 0, midiChannel);         break;
      case 7: arpBPM   = (uint16_t)constrain((int)arpBPM + dir * 5, 40, 300);   break;
      case 8: arpDivision = (byte)constrain((int)arpDivision + dir, 1, 16);      break;
    }
    updateDisplay();
  }
}

void menuSavePreset() {
  byte slot = (currentPage <= 4) ? currentPage - 1 : menuCursor;
  savePreset(slot);
  for (int i = 0; i < 6; i++) { digitalWrite(LED_PIN, i % 2); delay(40); }
  updateDisplay();
}

void handleClipInMenu(byte idx) {
  byte pageItems = pgm_read_byte(&PAGE_ITEMS[currentPage - 1]);
  switch (idx) {
    case 0: // C1 ▲
      menuCursor = (menuCursor == 0) ? pageItems - 1 : menuCursor - 1;
      updateDisplay();
      break;
    case 1: // C2 ▼
      menuCursor = (menuCursor + 1) % pageItems;
      updateDisplay();
      break;
    case 2: // C3 −
      if (currentPage == 4) menuAdjust(-1);
      else updateDisplay();
      break;
    case 3: // C4 + / confirmar
      menuConfirm();
      break;
    case 4: // C5 próxima página
      currentPage = (currentPage % NUM_PAGES) + 1;
      menuCursor  = 0;
      updateDisplay();
      break;
    case 5: // C6 back / fechar
      toggleMenu();
      break;
    case 6: // C7 salvar preset
      menuSavePreset();
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  BOTÕES C1–C7 (performance)
// ─────────────────────────────────────────────────────────────────────────────
void handleClipPerformance(byte idx, bool pressed) {
  switch (idx) {
    case 0: // C1 → Modo Cromático
      if (pressed) {
        allNotesOff();
        playStyle = STYLE_CHROMATIC;
        updateDisplay();
      }
      break;

    case 1: // C2 → Modo Acorde
      if (pressed) {
        allNotesOff();
        playStyle = STYLE_CHORD;
        updateDisplay();
      }
      break;

    case 2: // C3 → Modo Harpejo
      if (pressed) {
        allNotesOff();
        playStyle = STYLE_ARPEGIO;
        updateDisplay();
      }
      break;

    case 3: // C4 → Sustain touch
      sustainTouchActive = pressed;
      MIDI.sendControlChange(64, pressed ? 127 : 0, midiChannel);
      updateDisplay();
      break;

    case 4: // C5 → Oitava +
      if (pressed) {
        octave = (int8_t)constrain((int)octave + 1, -2, 2);
        updateDisplay();
      }
      break;

    case 5: // C6 → Oitava −
      if (pressed) {
        octave = (int8_t)constrain((int)octave - 1, -2, 2);
        updateDisplay();
      }
      break;

    case 6: // C7 → Play/Pause DAW
      if (pressed) {
        dawPlaying = !dawPlaying;
        MIDI.sendControlChange(CC_DAW_PLAY, dawPlaying ? 127 : 0, 1);
        updateDisplay();
      }
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  MENU TOGGLE
// ─────────────────────────────────────────────────────────────────────────────
void toggleMenu() {
  inMenu = !inMenu;
  digitalWrite(LED_PIN, inMenu ? HIGH : LOW);
  if (inMenu) {
    allNotesOff();
    currentPage = 1;
    menuCursor  = 0;
  } else {
    if (sustainFixed) MIDI.sendControlChange(64, 127, midiChannel);
  }
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Wire1.setSDA(OLED_SDA);
  Wire1.setSCL(OLED_SCL);
  Wire1.begin();

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(14, 20);
  display.print("Macropad MIDI v2.3");
  display.setCursor(28, 33);
  display.print("Inicializando...");
  display.display();

  usb_midi.setStringDescriptor("Macro MIDI Pad");
  MIDI.begin(MIDI_CHANNEL_OMNI);

  pinMode(LED_PIN, OUTPUT);
  for (byte i = 0; i < 16; i++)       pinMode(PAD_PINS[i],   INPUT_PULLUP);
  for (byte i = 0; i < NUM_CLIPS; i++) pinMode(CLIP_PINS[i], INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);

  // Inicializa estados do harpejo
  for (byte i = 0; i < ARP_MAX_PADS; i++) {
    arpState[i].active   = false;
    arpState[i].lastNote = 255;
    arpState[i].step     = 0;
  }

  loadPresetsFromFlash();
  loadPreset(0);

  for (byte i = 0; i < 4; i++) {
    digitalWrite(LED_PIN, HIGH); delay(80);
    digitalWrite(LED_PIN, LOW);  delay(80);
  }
  delay(300);
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  flushDisplay();

  if (playStyle == STYLE_GENERATIVE && !inMenu) updateGenerative(now);
  if (playStyle == STYLE_ARPEGIO    && !inMenu) updateArpegio(now);

  // ── RESET (GP28) ─────────────────────────────────────────────────────────
  {
    bool reading = (digitalRead(RESET_PIN) == LOW);
    if (reading != lastResetBtnState) resetDebounce = now;
    if ((now - resetDebounce) > 25) {
      if (reading != resetBtnState) {
        resetBtnState = reading;
        if (reading) { resetHoldStart = now; resetArmed = true; }
        else resetArmed = false;
      }
    }
    if (resetArmed && resetBtnState && (now - resetHoldStart) >= RESET_HOLD_MS) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(16, 28);
      display.print("Entrando BOOTSEL...");
      display.display();
      delay(300);
      reset_usb_boot(0, 0);
    }
    lastResetBtnState = reading;
  }

  // ── C1–C7 ────────────────────────────────────────────────────────────────
  for (byte i = 0; i < NUM_CLIPS; i++) {
    bool reading = (digitalRead(CLIP_PINS[i]) == LOW);
    if (reading != lastClipStates[i]) clipDebounce[i] = now;
    if ((now - clipDebounce[i]) > CLIP_DEBOUNCE) {
      if (reading != clipStates[i]) {
        clipStates[i] = reading;
        if (inMenu) {
          if (reading) handleClipInMenu(i);   // só on-press no menu
        } else {
          handleClipPerformance(i, reading);  // on-press e on-release (sustain touch)
        }
      }
    }
    lastClipStates[i] = reading;
  }

  // ── PADS ─────────────────────────────────────────────────────────────────
  for (byte i = 0; i < 16; i++) {
    bool reading = (digitalRead(PAD_PINS[i]) == LOW);
    if (reading != lastPadStates[i]) lastDebounceTime[i] = now;
    if ((now - lastDebounceTime[i]) > DEBOUNCE_DELAY) {
      if (reading != padStates[i]) {
        padStates[i] = reading;
        if (reading) handlePadPress(i, now);
        else         handlePadRelease(i, now);
      }
    }
    lastPadStates[i] = reading;
  }

  MIDI.read();
}

// ─────────────────────────────────────────────────────────────────────────────
//  PADS
// ─────────────────────────────────────────────────────────────────────────────
void handlePadPress(byte pad, unsigned long now) {
  for (byte i = 0; i < 4; i++)
    if (pad == TOP_ROW[i]) { topRowPressTime[i] = now; break; }

  // Ativação do menu pelos 4 pads do topo simultâneos (janela 150 ms)
  if (padStates[TOP_ROW[0]] && padStates[TOP_ROW[1]] &&
      padStates[TOP_ROW[2]] && padStates[TOP_ROW[3]]) {
    unsigned long minT = topRowPressTime[0], maxT = topRowPressTime[0];
    for (byte i = 1; i < 4; i++) {
      if (topRowPressTime[i] < minT) minT = topRowPressTime[i];
      if (topRowPressTime[i] > maxT) maxT = topRowPressTime[i];
    }
    if ((maxT - minT) < 150UL) { toggleMenu(); topRowPressed = true; return; }
  }

  if (inMenu) return;
  playNote(pad, true);
}

void handlePadRelease(byte pad, unsigned long now) {
  (void)now;
  if (topRowPressed) {
    for (byte i = 0; i < 4; i++) {
      if (pad == TOP_ROW[i]) {
        bool allReleased = true;
        for (byte j = 0; j < 4; j++)
          if (padStates[TOP_ROW[j]]) { allReleased = false; break; }
        if (allReleased) topRowPressed = false;
        return;
      }
    }
  }

  if (inMenu) {
    for (byte i = 0; i < 4; i++) {
      if (pad == TOP_ROW[i]) {
        currentPage = i + 1;
        menuCursor  = 0;
        updateDisplay();
        return;
      }
    }
    return;
  }

  // Com sustain fixo ou touch ativo, suprime NoteOff (exceto harpejo, que gerencia sozinho)
  if (playStyle == STYLE_ARPEGIO) {
    stopArpPad(pad);
  } else if (!sustainFixed && !sustainTouchActive) {
    playNote(pad, false);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  NOTAS
// ─────────────────────────────────────────────────────────────────────────────
void playNote(byte pad, bool noteOn) {
  if (playStyle == STYLE_GENERATIVE) return;
  if (playStyle == STYLE_ARPEGIO) {
    // Harpejo é gerenciado por startArpPad/stopArpPad
    if (noteOn) startArpPad(pad, millis());
    return;
  }

  switch (playStyle) {
    case STYLE_SCALED: {
      byte note = (byte)constrain(
        (int)rootNote + scaleNote(currentScale, pad)
        + transpose + octave * 12, 0, 127);
      if (noteOn) MIDI.sendNoteOn(note, velocity, midiChannel);
      else        MIDI.sendNoteOff(note, 0, midiChannel);
    } break;

    case STYLE_CHROMATIC: {
      byte note = (byte)constrain(
        (int)rootNote + pad + transpose + octave * 12, 0, 127);
      if (noteOn) MIDI.sendNoteOn(note, velocity, midiChannel);
      else        MIDI.sendNoteOff(note, 0, midiChannel);
    } break;

    case STYLE_CHORD:
      playChord(pad, noteOn);
      break;

    case STYLE_OMNI_CHORD:
      playOmniChord(pad, noteOn);
      break;

    default: break;
  }
}

void playChord(byte pad, bool noteOn) {
  if (pad < 8) {
    for (byte i = 0; i < 5; i++) {
      int8_t v = chordVoice(currentChordMode, pad, i);
      if (v == NO_NOTE) continue;
      byte note = (byte)constrain(
        (int)rootNote + v + transpose + octave * 12, 0, 127);
      if (noteOn) MIDI.sendNoteOn(note, velocity, midiChannel);
      else        MIDI.sendNoteOff(note, 0, midiChannel);
    }
  } else {
    byte note = (byte)constrain(
      (int)rootNote + 12
      + scaleNote(pgm_read_byte(&CHORD_MODE_SCALES[currentChordMode]), pad - 8)
      + transpose + octave * 12, 0, 127);
    if (noteOn) MIDI.sendNoteOn(note, velocity, midiChannel);
    else        MIDI.sendNoteOff(note, 0, midiChannel);
  }
}

void playOmniChord(byte pad, bool noteOn) {
  if (pad < 8) {
    if (noteOn) omniChordIndex = pad;
  } else {
    byte si = pad - 8;
    if (noteOn) {
      byte note = (byte)constrain(
        (int)rootNote + omniVoice(omniChordIndex, si)
        + transpose + octave * 12, 0, 127);
      omniChordNotes[si] = note;
      MIDI.sendNoteOn(note, velocity, midiChannel);
    } else {
      if (omniChordNotes[si] != 255) {
        MIDI.sendNoteOff(omniChordNotes[si], 0, midiChannel);
        omniChordNotes[si] = 255;
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HARPEJO
// ─────────────────────────────────────────────────────────────────────────────
// Monta a lista de notas do voicing para um pad e inicia o harpejo
void startArpPad(byte pad, unsigned long now) {
  ArpState& a = arpState[pad];
  a.active    = true;
  a.padIdx    = pad;
  a.step      = 0;
  a.lastNote  = 255;
  a.noteCount = 0;

  if (pad < 8) {
    // Usa voicing de acorde (igual ao modo Chord)
    for (byte i = 0; i < 5; i++) {
      int8_t v = chordVoice(currentChordMode, pad, i);
      if (v == NO_NOTE) continue;
      byte note = (byte)constrain(
        (int)rootNote + v + transpose + octave * 12, 0, 127);
      a.notes[a.noteCount++] = note;
    }
  } else {
    // Pads 8–15: usa a escala atual (uma nota por pad)
    byte note = (byte)constrain(
      (int)rootNote + 12
      + scaleNote(pgm_read_byte(&CHORD_MODE_SCALES[currentChordMode]), pad - 8)
      + transpose + octave * 12, 0, 127);
    a.notes[0]  = note;
    a.noteCount = 1;
  }

  a.nextTick = now; // dispara imediatamente no primeiro tick
}

void stopArpPad(byte pad) {
  ArpState& a = arpState[pad];
  if (!a.active) return;
  a.active = false;
  if (a.lastNote != 255) {
    MIDI.sendNoteOff(a.lastNote, 0, midiChannel);
    a.lastNote = 255;
  }
}

// Chamado no loop(); dispara os steps de harpejo pendentes
void updateArpegio(unsigned long now) {
  for (byte p = 0; p < ARP_MAX_PADS; p++) {
    ArpState& a = arpState[p];
    if (!a.active || a.noteCount == 0) continue;
    if (now < a.nextTick) continue;

    // NoteOff da nota anterior
    if (a.lastNote != 255) {
      MIDI.sendNoteOff(a.lastNote, 0, midiChannel);
    }

    // NoteOn do step atual
    byte note  = a.notes[a.step % a.noteCount];
    a.lastNote = note;
    MIDI.sendNoteOn(note, velocity, midiChannel);

    a.step     = (a.step + 1) % a.noteCount;
    a.nextTick = now + arpInterval();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  GENERATIVO
// ─────────────────────────────────────────────────────────────────────────────
void updateGenerative(unsigned long now) {
  if (now - lastGenNoteTime < nextGenNoteDelay) return;
  if (currentGenNote != 255) {
    MIDI.sendNoteOff(currentGenNote, 0, midiChannel);
    currentGenNote = 255;
  }
  if ((byte)random(0, 100) < GEN_TRIGGER_CHANCE) {
    byte note = (byte)constrain(
      (int)rootNote + (int)random(0, 3) * 12
      + scaleNote(0, random(0, 8))
      + transpose + octave * 12, 0, 127);
    currentGenNote = note;
    MIDI.sendNoteOn(currentGenNote, (byte)random(60, 90), midiChannel);
  }
  nextGenNoteDelay = (unsigned long)random(MIN_GEN_DELAY, MAX_GEN_DELAY);
  lastGenNoteTime  = now;
}

// ─────────────────────────────────────────────────────────────────────────────
//  UTILIDADES
// ─────────────────────────────────────────────────────────────────────────────
void allNotesOff() {
  // Para todos os harpejos ativos
  for (byte i = 0; i < ARP_MAX_PADS; i++) stopArpPad(i);
  // Silencia tudo no canal
  MIDI.sendControlChange(123, 0, midiChannel); // All Notes Off CC
  if (!sustainFixed && !sustainTouchActive)
    MIDI.sendControlChange(64, 0, midiChannel);
  currentGenNote = 255;
}

// ─────────────────────────────────────────────────────────────────────────────
//  PRESETS / FLASH
// ─────────────────────────────────────────────────────────────────────────────
void loadPresetsFromFlash() {
  const FlashData* fd = (const FlashData*)(XIP_BASE + FLASH_TARGET_OFFSET);
  if (fd->magic == FLASH_MAGIC) {
    for (byte i = 0; i < 4; i++) presets[i] = fd->presets[i];
  } else {
    presets[0] = {STYLE_SCALED,     0, 0, 0, -1, 100, false, 60, 1};
    presets[1] = {STYLE_CHROMATIC,  7, 0, 0, -1, 100, false, 60, 1};
    presets[2] = {STYLE_CHORD,      0, 0, 0, -1, 100, false, 60, 1};
    presets[3] = {STYLE_ARPEGIO,    0, 0, 0, -1,  90, false, 60, 1};
  }
}

void savePresetsToFlash() {
  FlashData data;
  data.magic = FLASH_MAGIC;
  for (byte i = 0; i < 4; i++) data.presets[i] = presets[i];

  static uint8_t buf[FLASH_PAGE_SIZE];
  memset(buf, 0xFF, sizeof(buf));
  memcpy(buf, &data, sizeof(FlashData));

  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(FLASH_TARGET_OFFSET, buf, FLASH_PAGE_SIZE);
  restore_interrupts(ints);
}

void loadPreset(byte idx) {
  if (idx >= 4) return;
  allNotesOff();
  playStyle        = presets[idx].style;
  currentScale     = presets[idx].scale;
  currentChordMode = presets[idx].chordMode;
  transpose        = presets[idx].transpose;
  octave           = presets[idx].octave;
  velocity         = presets[idx].velocity;
  sustainFixed     = presets[idx].sustainFixed;
  rootNote         = presets[idx].rootNote;
  midiChannel      = presets[idx].midiChannel;
  MIDI.sendControlChange(64, (sustainFixed || sustainTouchActive) ? 127 : 0, midiChannel);
  updateDisplay();
}

void savePreset(byte idx) {
  if (idx >= 4) return;
  presets[idx] = {playStyle, currentScale, currentChordMode,
                  transpose, octave, velocity, sustainFixed, rootNote, midiChannel};
  savePresetsToFlash();
}
