/*
 * Macropad MIDI Controller - v2.7
 * Grand-Pad — Pico firmware
 *
 * PINAGEM
 * ───────────────────────────────────────────────────────────────────────
 *  GP2 –GP15  → 14 pads
 *  GP16       → C7  → Botão coringa (CC livre, definido pelo usuário)
 *  GP17       → C6  → Oitava −
 *  GP18       → C5  → Oitava +
 *  GP19       → C4  → Sustain touch
 *  GP20       → C3  → Modo Harpejo
 *  GP21       → C2  → Modo Acorde
 *  GP22       → C1  → Modo Cromático
 *  GP25       → LED onboard
 *  GP26       → OLED SDA (I2C1)
 *  GP27       → OLED SCL (I2C1)
 *  GP28       → BOOTSEL (segurar 2s → UF2)
 *
 * BOTÃO CORINGA (C7)
 * ───────────────────────────────────────────────────────────────────────
 *  Envia um Control Change configurável (CC_WILDCARD, padrão 30) alternando
 *  entre 127 (pressionado) e 0 (solto). O usuário mapeia essa CC na DAW
 *  para a função que desejar (ex: play/stop, marcador, mute, etc.).
 *
 * NOME DO ACORDE / NOTA ATIVA (OLED)
 * ───────────────────────────────────────────────────────────────────────
 *  Nos modos Acorde e Harpejo, ao pressionar um pad (0–7), o OLED exibe
 *  em fonte grande o nome do acorde tocado (ex: "C", "Am", "G7", "Bdim",
 *  "Cmaj7"), substituindo temporariamente a linha "Acorde: <voicing>".
 *  Pads 8–15 exibem a nota de baixo correspondente (ex: "D (baixo)").
 *  Ao soltar o pad, a tela volta ao normal.
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

// ── MIDI USB ──────────────────────────────────────────────────────────────────
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// ── PINOS ─────────────────────────────────────────────────────────────────────
static const byte PAD_PINS[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
#define NUM_PADS 16
#define LED_PIN  25

// C1=GP22 … C7=GP16
static const byte CLIP_PINS[7] = {19, 18, 17, 16, 20, 22, 21};
#define NUM_CLIPS 7

#define RESET_PIN      28
#define RESET_HOLD_MS  2000UL

// CC do botão coringa (C7) — usuário mapeia na DAW
#define CC_WILDCARD 30

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
  {0,2,4,5,7,9,11,12,14,16,17,19,21,23,24,26},
  {0,2,3,5,7,8,10,12,14,15,17,19,20,22,24,26},
  {0,3,5,6,7,10,12,15,17,18,19,22,24,27,29,30},
  {0,2,3,5,7,8,11,12,14,15,17,19,20,23,24,26},
  {0,2,4,7,9,12,14,16,19,21,24,26,28,31,33,36},
  {0,3,5,7,10,12,15,17,19,22,24,27,29,31,34,36},
  {0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26},
  {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}
};
static const char* const SCALE_NAMES[8] PROGMEM = {
  "Major","Minor","Blues","Harmonic",
  "Pent Maj","Pent Min","Lydian","Chromatic"
};
static const byte CHORD_MODE_SCALES[8] PROGMEM = {0,1,2,3,0,1,0,0};

// ── VOICINGS ──────────────────────────────────────────────────────────────────
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
  "Major","Minor","Blues","Harmonic","Open","Minor2","Jazz 7th","Diatonic"
};
static const char* const STYLE_NAMES[NUM_STYLES] PROGMEM = {
  "Scaled","Chromat","Chord","Arpegio","Generat","OmniChrd"
};
static const char* const NOTE_NAMES[12] = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

// ── QUALIDADE DOS ACORDES POR GRAU (pads 0–7) ─────────────────────────────────
// Para cada chordMode (0–7) e cada pad (0–7), descreve a qualidade do acorde
// formado pelo voicing correspondente em CHORD_VOICINGS.
// Sufixos curtos para caber no display: "", "m", "dim", "aug", "7", "m7", "maj7", "sus4"
static const char* const CHORD_QUALITY[8][8] PROGMEM = {
  // Major  (I, ii, iii, IV, V, vi, vii°, I)
  {"","m","m","","","m","dim",""},
  // Minor  (i, ii°, III, iv, v, VI, VII, i)
  {"m","dim","","m","m","","",""},
  // Blues  (voicings com 7a -> dominante)
  {"7","7","7","7","7","7","7","7"},
  // Harmonic (i, ii°, III+, iv, V, VI, vii°, i)
  {"m","dim","aug","m","","","dim",""},
  // Open (voicings espaçados maj/min alternados)
  {"","m","m","","","m","dim",""},
  // Minor2
  {"m","m","","m","m","","",""},
  // Jazz 7th
  {"maj7","m7","m7","maj7","7","m7","m7","maj7"},
  // Diatonic
  {"","m","m","","","m","dim",""}
};

// ── ESTADO DE EXIBIÇÃO DE ACORDE/NOTA ATIVA ──────────────────────────────────
static char activeChordName[12] = "";  // ex: "C", "Am", "G7", "Bdim"
static bool  chordDisplayActive  = false;

// ── CONFIGURAÇÕES GLOBAIS ─────────────────────────────────────────────────────
static PlayStyle playStyle    = STYLE_SCALED;
static byte  currentScale     = 0;
static byte  currentChordMode = 0;
static int8_t transpose       = 0;
static int8_t octave          = -1;
static byte  velocity         = 100;
static byte  midiChannel      = 1;
static byte  clipChannel      = 1;
static byte  rootNote         = 60;
static bool  sustainFixed     = false;
static bool  wildcardPressed  = false;

#define NO_NOTE ((int8_t)127)


// ── PRESETS / FLASH ───────────────────────────────────────────────────────────
struct Preset {
  PlayStyle style;
  byte  scale, chordMode;
  int8_t transpose, octave;
  byte  velocity;
  bool  sustainFixed;
  byte  rootNote, midiChannel;
};
static Preset presets[4];

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC 0x4D494454u   // 'MIDT' — v2.7
struct FlashData { uint32_t magic; Preset presets[4]; };

// ── HARPEJO ───────────────────────────────────────────────────────────────────
struct ArpState {
  bool  active;
  byte  noteCount, notes[5], step, lastNote;
  unsigned long nextTick;
};
#define ARP_MAX_PADS NUM_PADS
static ArpState arpState[ARP_MAX_PADS];

static uint16_t arpBPM      = 120;
static byte     arpDivision = 4;
static inline unsigned long arpInterval() {
  return 60000UL / arpBPM / arpDivision;
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
static const byte PAGE_ITEMS[NUM_PAGES] PROGMEM = {8,8,6,9,4};

// ── SUSTAIN TOUCH ─────────────────────────────────────────────────────────────
static bool sustainTouchActive = false;

// ── PADS ─────────────────────────────────────────────────────────────────────
static bool padStates[NUM_PADS]               = {false};
static bool lastPadStates[NUM_PADS]           = {false};
static unsigned long lastDebounceTime[NUM_PADS]= {0};
#define DEBOUNCE_DELAY 20

// Pads do topo para menu: agora índices 10–13 (GP12–GP15)
static const byte TOP_PAD_IDX[4]        = {12,13,14,15}; // índice no array = GPIO (GP12..GP15)
static unsigned long topRowPressTime[4] = {0,0,0,0};
static bool topRowPressed               = false;

// ── CLIPS / C1–C7 ────────────────────────────────────────────────────────────
static bool clipStates[NUM_CLIPS]           = {false};
static bool lastClipStates[NUM_CLIPS]       = {false};
static unsigned long clipDebounce[NUM_CLIPS]= {0};
#define CLIP_DEBOUNCE 25


// ── RESET (GP28) ──────────────────────────────────────────────────────────────
static bool resetBtnState          = false;
static bool lastResetBtnState      = false;
static unsigned long resetDebounce  = 0;
static unsigned long resetHoldStart = 0;
static bool resetArmed             = false;

// ── DISPLAY DIRTY FLAG ────────────────────────────────────────────────────────
static bool displayDirty = true;

// ─────────────────────────────────────────────────────────────────────────────
//  HELPERS PROGMEM
// ─────────────────────────────────────────────────────────────────────────────
static inline int8_t scaleNote(byte s, byte i) {
  return (int8_t)pgm_read_byte(&SCALES[s][i]);
}
static inline int8_t chordVoice(byte m, byte p, byte n) {
  return (int8_t)pgm_read_byte(&CHORD_VOICINGS[m][p][n]);
}
static inline int8_t omniVoice(byte i, byte n) {
  return (int8_t)pgm_read_byte(&OMNI_CHORD_VOICINGS[i][n]);
}
static void noteLabel(byte midi, char* buf, byte len) {
  if (midi > 127) { snprintf(buf, len, "---"); return; }
  int oct = (int)(midi / 12) - 1;
  snprintf(buf, len, "%s%d", NOTE_NAMES[midi % 12], oct);
}

// ── NOME DO ACORDE / NOTA ATIVA ───────────────────────────────────────────────
// Monta o nome do acorde tocado pelo pad (0–7) no modo Chord/Arpegio,
// ou o nome da nota de baixo (pad 8–15), e marca para exibição no OLED.
static void setActiveChordDisplay(byte pad) {
  if (pad < 8) {
    // Fundamental do acorde = rootNote + offset[0] do voicing (+ transpose/octave)
    int8_t rootOffset = chordVoice(currentChordMode, pad, 0);
    int    chordRootMidi = (int)rootNote + rootOffset + transpose + octave * 12;
    chordRootMidi = ((chordRootMidi % 12) + 12) % 12; // só interessa a classe de nota
    const char* quality = CHORD_QUALITY[currentChordMode][pad];
    snprintf(activeChordName, sizeof(activeChordName), "%s%s",
             NOTE_NAMES[chordRootMidi], quality);
  } else {
    // Pads 8–15: nota de baixo da escala do modo de acorde
    int8_t deg = scaleNote(pgm_read_byte(&CHORD_MODE_SCALES[currentChordMode]), pad - 8);
    int    midiClass = ((int)rootNote + 12 + deg + transpose) % 12;
    midiClass = (midiClass + 12) % 12;
    snprintf(activeChordName, sizeof(activeChordName), "%s (baixo)", NOTE_NAMES[midiClass]);
  }
  chordDisplayActive = true;
  updateDisplay();
}

static void clearActiveChordDisplay() {
  chordDisplayActive = false;
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DISPLAY — TELA PERFORMANCE
// ─────────────────────────────────────────────────────────────────────────────
/*
  Layout 128×64:
    y= 0..15  Modo (size2) + Oitava               [linha grande]
    y=16      ── separador ──
    y=18..26  Root + Escala                        [linha info 1]
    y=27      ── separador ──
    y=29..45  Zona de acorde:
                - padrão: "Acorde: <voicing>"
                - destaque (pad pressionado): nome do acorde em size2
    y=46      ── separador ──
    y=48..56  Vel / Transpose / Canal
    y=57..64  Sustain / Coringa (C7)
*/
void drawPerformanceScreen() {
  char buf[24];
  display.setTextColor(SSD1306_WHITE);

  // ── Linha topo: modo + oitava ──
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(STYLE_NAMES[playStyle]);
  snprintf(buf, sizeof(buf), "%+d", octave);
  display.setCursor(98, 0);
  display.print(buf);
  display.setTextSize(1);

  display.drawFastHLine(0, 16, 128, SSD1306_WHITE);

  // ── Root + escala ──
  char rootBuf[5];
  noteLabel(rootNote, rootBuf, sizeof(rootBuf));
  snprintf(buf, sizeof(buf), "Root:%-4s  Esc:%-8s", rootBuf, SCALE_NAMES[currentScale]);
  display.setCursor(0, 18);
  display.print(buf);

  display.drawFastHLine(0, 27, 128, SSD1306_WHITE);

  // ── Zona de acorde (dedicada — não sobrepõe Root/Escala) ──
  if (chordDisplayActive &&
      (playStyle == STYLE_CHORD || playStyle == STYLE_ARPEGIO)) {
    // Destaque: acorde tocado agora, em fonte grande, centralizado
    display.setTextSize(2);
    int nlen = strlen(activeChordName);
    int nx   = (128 - nlen * 12) / 2;
    display.setCursor(nx < 0 ? 0 : nx, 30);
    display.print(activeChordName);
    display.setTextSize(1);
  } else if (playStyle == STYLE_CHORD || playStyle == STYLE_ARPEGIO ||
             playStyle == STYLE_OMNI_CHORD) {
    snprintf(buf, sizeof(buf), "Acorde:%-10s", CHORD_NAMES[currentChordMode]);
    display.setCursor(0, 33);
    display.print(buf);
  }
  // Outros modos: zona de acorde permanece vazia

  display.drawFastHLine(0, 46, 128, SSD1306_WHITE);

  // ── Velocidade / Transpose / Canal ──
  snprintf(buf, sizeof(buf), "Vel:%-3d Tr:%+d Ch:%-2d", velocity, transpose, midiChannel);
  display.setCursor(0, 48);
  display.print(buf);

  // ── Sustain / Coringa (C7) ──
  if (playStyle == STYLE_ARPEGIO) {
    snprintf(buf, sizeof(buf), "Sus:%-3s BPM:%-3d D:%d",
      (sustainFixed || sustainTouchActive) ? "ON" : "OFF", arpBPM, arpDivision);
  } else {
    snprintf(buf, sizeof(buf), "Sus:%-3s  C7:%s",
      (sustainFixed || sustainTouchActive) ? "ON" : "OFF",
      wildcardPressed ? "ON" : "OFF");
  }
  display.setCursor(0, 57);
  display.print(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DISPLAY — MENU
// ─────────────────────────────────────────────────────────────────────────────
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

void drawMenuHeader(const char* title, const char* hint = nullptr) {
  char buf[22];
  snprintf(buf, sizeof(buf), "%-15s%d/%d", title, currentPage, NUM_PAGES);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(buf);
  display.drawFastHLine(0,  9, 128, SSD1306_WHITE);
  display.drawFastHLine(0, 56, 128, SSD1306_WHITE);
  display.setCursor(0, 57);
  display.print(hint ? hint : "^v=-+ C5=pag C6=X C7=sv");
}

void drawMenuPage() {
  byte pageItems    = pgm_read_byte(&PAGE_ITEMS[currentPage - 1]);
  byte firstVisible = (menuCursor > 4) ? menuCursor - 4 : 0;

  if (currentPage == 1) {
    drawMenuHeader("Escalas", "^v=nav  +=sel  C6=X");
    for (byte i = 0; i < 5 && (firstVisible + i) < pageItems; i++) {
      byte idx = firstVisible + i;
      drawListItem(11 + i * 9, idx == menuCursor, idx == currentScale, SCALE_NAMES[idx]);
    }
  } else if (currentPage == 2) {
    drawMenuHeader("Acordes", "^v=nav  +=sel  C6=X");
    for (byte i = 0; i < 5 && (firstVisible + i) < pageItems; i++) {
      byte idx = firstVisible + i;
      drawListItem(11 + i * 9, idx == menuCursor, idx == currentChordMode, CHORD_NAMES[idx]);
    }
  } else if (currentPage == 3) {
    drawMenuHeader("Modos", "^v=nav  +=sel  C6=X");
    for (byte i = 0; i < pageItems; i++) {
      drawListItem(11 + i * 9, i == menuCursor, (byte)i == (byte)playStyle, STYLE_NAMES[i]);
    }
  } else if (currentPage == 4) {
    drawMenuHeader("Config", "^v=item  -/+=valor  C6=X");
    static const char* const cfgLabels[] = {
      "Velocidade","Oitava","Transpose","Ch Pad",
      "Ch Clip","Root Note","Sustain","Arp BPM","Arp Div"
    };
    char nb[5];
    for (byte i = 0; i < 5 && (firstVisible + i) < pageItems; i++) {
      byte idx = firstVisible + i;
      char vb[8];
      switch (idx) {
        case 0: snprintf(vb,8,"%d",  velocity);           break;
        case 1: snprintf(vb,8,"%+d", octave);             break;
        case 2: snprintf(vb,8,"%+d", transpose);          break;
        case 3: snprintf(vb,8,"%d",  midiChannel);        break;
        case 4: snprintf(vb,8,"%d",  clipChannel);        break;
        case 5: noteLabel(rootNote, nb, sizeof(nb));
                snprintf(vb,8,"%s",  nb);                 break;
        case 6: snprintf(vb,8,"%s",  sustainFixed?"ON":"OFF"); break;
        case 7: snprintf(vb,8,"%d",  arpBPM);             break;
        case 8: snprintf(vb,8,"%d",  arpDivision);        break;
      }
      char line[22];
      // Largura 12 garante ao menos 1 espaço entre o rótulo e o valor
      snprintf(line, sizeof(line), "%-12s%s", cfgLabels[idx], vb);
      drawListItem(11 + i * 9, idx == menuCursor, false, line);
    }
  } else if (currentPage == 5) {
    drawMenuHeader("Presets", "+=load  C7=save  C6=X");
    const char* pn[] = {"Preset 1","Preset 2","Preset 3","Preset 4"};
    for (byte i = 0; i < pageItems; i++)
      drawListItem(11 + i * 9, i == menuCursor, false, pn[i]);
  }
}

// ── Flush centralizado ────────────────────────────────────────────────────────
void updateDisplay() { displayDirty = true; }

void flushDisplay() {
  if (!displayDirty) return;
  displayDirty = false;
  display.clearDisplay();
  display.setTextSize(1);
  if      (inMenu) drawMenuPage();
  else             drawPerformanceScreen();
  display.display();
}

// ─────────────────────────────────────────────────────────────────────────────
//  UTILITÁRIOS
// ─────────────────────────────────────────────────────────────────────────────
void allNotesOff() {
  for (byte i = 0; i < ARP_MAX_PADS; i++) {
    ArpState& a = arpState[i];
    if (a.active && a.lastNote != 255) {
      MIDI.sendNoteOff(a.lastNote, 0, midiChannel);
    }
    a.active = false; a.lastNote = 255;
  }
  MIDI.sendControlChange(123, 0, midiChannel);
  if (!sustainFixed && !sustainTouchActive) {
    MIDI.sendControlChange(64, 0, midiChannel);
  }
  currentGenNote = 255;
  chordDisplayActive = false;
}

void sendNoteOn(byte note, byte vel) {
  MIDI.sendNoteOn(note, vel, midiChannel);
}
void sendNoteOff(byte note) {
  MIDI.sendNoteOff(note, 0, midiChannel);
}
void sendCC(byte cc, byte val) {
  MIDI.sendControlChange(cc, val, midiChannel);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MENU ACTIONS
// ─────────────────────────────────────────────────────────────────────────────
void menuAdjust(int8_t dir);

void menuConfirm() {
  if (currentPage == 1) {
    currentScale = menuCursor;
  } else if (currentPage == 2) {
    currentChordMode = menuCursor;
    currentScale = pgm_read_byte(&CHORD_MODE_SCALES[menuCursor]);
  } else if (currentPage == 3) {
    playStyle = (PlayStyle)menuCursor;
    if (playStyle == STYLE_OMNI_CHORD) currentScale = 0;
  } else if (currentPage == 4) {
    menuAdjust(+1); return;
  } else if (currentPage == 5) {
    loadPreset(menuCursor);
  }
  updateDisplay();
}

void menuAdjust(int8_t dir) {
  if (currentPage == 4) {
    switch (menuCursor) {
      case 0: velocity    = (byte)constrain((int)velocity   + dir*5, 10,127); break;
      case 1: octave      = (int8_t)constrain((int)octave   + dir, -2,  2);   break;
      case 2: transpose   = (int8_t)constrain((int)transpose+ dir,-12, 12);   break;
      case 3: midiChannel = (byte)((midiChannel-1+dir+16)%16+1);              break;
      case 4: clipChannel = (byte)((clipChannel -1+dir+16)%16+1);             break;
      case 5: rootNote    = (byte)constrain((int)rootNote   + dir,  0,127);   break;
      case 6: sustainFixed= !sustainFixed;
              sendCC(64, sustainFixed ? 127 : 0);                             break;
      case 7: arpBPM      = (uint16_t)constrain((int)arpBPM + dir*5,40,300); break;
      case 8: arpDivision = (byte)constrain((int)arpDivision+ dir,  1, 16);   break;
    }
    updateDisplay();
  }
}

void menuSavePreset() {
  byte slot = (currentPage <= 4) ? currentPage-1 : menuCursor;
  savePreset(slot);
  for (int i = 0; i < 6; i++) { digitalWrite(LED_PIN, i%2); delay(40); }
  updateDisplay();
}

void handleClipInMenu(byte idx) {
  byte pageItems = pgm_read_byte(&PAGE_ITEMS[currentPage-1]);
  switch (idx) {
    case 0: menuCursor = (menuCursor==0) ? pageItems-1 : menuCursor-1; updateDisplay(); break;
    case 1: menuCursor = (menuCursor+1) % pageItems;                   updateDisplay(); break;
    case 2: if (currentPage==4) menuAdjust(-1); else updateDisplay();                   break;
    case 3: menuConfirm(); break;
    case 4: currentPage=(currentPage%NUM_PAGES)+1; menuCursor=0; updateDisplay(); break;
    case 5: toggleMenu(); break;
    case 6: menuSavePreset(); break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  MENU TOGGLE
// ─────────────────────────────────────────────────────────────────────────────
void toggleMenu() {
  inMenu = !inMenu;
  digitalWrite(LED_PIN, inMenu ? HIGH : LOW);
  if (inMenu) { allNotesOff(); currentPage=1; menuCursor=0; }
  else if (sustainFixed) sendCC(64, 127);
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────────────
//  C1–C7 PERFORMANCE
// ─────────────────────────────────────────────────────────────────────────────
void handleClipPerformance(byte idx, bool pressed) {
  switch (idx) {
    case 0: if (pressed) { allNotesOff(); playStyle=STYLE_CHROMATIC; updateDisplay(); } break;
    case 1: if (pressed) { allNotesOff(); playStyle=STYLE_CHORD;     updateDisplay(); } break;
    case 2: if (pressed) { allNotesOff(); playStyle=STYLE_ARPEGIO;   updateDisplay(); } break;
    case 3: // Sustain touch
      sustainTouchActive = pressed;
      sendCC(64, pressed ? 127 : 0);
      updateDisplay();
      break;
    case 4: if (pressed) { octave=(int8_t)constrain((int)octave+1,-2,2); updateDisplay(); } break;
    case 5: if (pressed) { octave=(int8_t)constrain((int)octave-1,-2,2); updateDisplay(); } break;
    case 6: // C7 — Botão coringa (CC livre)
      wildcardPressed = pressed;
      sendCC(CC_WILDCARD, pressed ? 127 : 0);
      updateDisplay();
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  NOTAS
// ─────────────────────────────────────────────────────────────────────────────
void startArpPad(byte pad, unsigned long now);
void stopArpPad(byte pad);

void playNote(byte pad, bool noteOn) {
  if (playStyle == STYLE_GENERATIVE) return;
  if (playStyle == STYLE_ARPEGIO) {
    if (noteOn) startArpPad(pad, millis());
    return;
  }
  switch (playStyle) {
    case STYLE_SCALED: {
      byte note = (byte)constrain(
        (int)rootNote + scaleNote(currentScale, pad) + transpose + octave*12, 0, 127);
      if (noteOn) sendNoteOn(note, velocity);
      else        sendNoteOff(note);
    } break;
    case STYLE_CHROMATIC: {
      byte note = (byte)constrain(
        (int)rootNote + pad + transpose + octave*12, 0, 127);
      if (noteOn) sendNoteOn(note, velocity);
      else        sendNoteOff(note);
    } break;
    case STYLE_CHORD: {
      if (pad < 8) {
        if (noteOn) setActiveChordDisplay(pad);
        for (byte i = 0; i < 5; i++) {
          int8_t v = chordVoice(currentChordMode, pad, i);
          if (v == NO_NOTE) continue;
          byte note = (byte)constrain((int)rootNote + v + transpose + octave*12, 0, 127);
          if (noteOn) sendNoteOn(note, velocity); else sendNoteOff(note);
        }
        if (!noteOn) clearActiveChordDisplay();
      } else {
        if (noteOn) setActiveChordDisplay(pad);
        byte note = (byte)constrain(
          (int)rootNote + 12
          + scaleNote(pgm_read_byte(&CHORD_MODE_SCALES[currentChordMode]), pad-8)
          + transpose + octave*12, 0, 127);
        if (noteOn) sendNoteOn(note, velocity); else sendNoteOff(note);
        if (!noteOn) clearActiveChordDisplay();
      }
    } break;
    case STYLE_OMNI_CHORD: {
      if (pad < 8) { if (noteOn) omniChordIndex = pad; }
      else {
        byte si = pad - 8;
        if (noteOn) {
          byte note = (byte)constrain(
            (int)rootNote + omniVoice(omniChordIndex, si) + transpose + octave*12, 0, 127);
          omniChordNotes[si] = note;
          sendNoteOn(note, velocity);
        } else {
          if (omniChordNotes[si] != 255) { sendNoteOff(omniChordNotes[si]); omniChordNotes[si]=255; }
        }
      }
    } break;
    default: break;
  }
}

void startArpPad(byte pad, unsigned long now) {
  ArpState& a = arpState[pad];
  a.active = true; a.step = 0; a.lastNote = 255; a.noteCount = 0;
  setActiveChordDisplay(pad);
  if (pad < 8) {
    for (byte i = 0; i < 5; i++) {
      int8_t v = chordVoice(currentChordMode, pad, i);
      if (v == NO_NOTE) continue;
      a.notes[a.noteCount++] = (byte)constrain((int)rootNote+v+transpose+octave*12, 0, 127);
    }
  } else {
    a.notes[0]  = (byte)constrain(
      (int)rootNote + 12
      + scaleNote(pgm_read_byte(&CHORD_MODE_SCALES[currentChordMode]), pad-8)
      + transpose + octave*12, 0, 127);
    a.noteCount = 1;
  }
  a.nextTick = now;
}

void stopArpPad(byte pad) {
  ArpState& a = arpState[pad];
  if (!a.active) return;
  a.active = false;
  if (a.lastNote != 255) { sendNoteOff(a.lastNote); a.lastNote = 255; }
  clearActiveChordDisplay();
}

void updateArpegio(unsigned long now) {
  for (byte p = 0; p < ARP_MAX_PADS; p++) {
    ArpState& a = arpState[p];
    if (!a.active || a.noteCount == 0 || now < a.nextTick) continue;
    if (a.lastNote != 255) sendNoteOff(a.lastNote);
    byte note = a.notes[a.step % a.noteCount];
    a.lastNote = note;
    sendNoteOn(note, velocity);
    a.step = (a.step + 1) % a.noteCount;
    a.nextTick = now + arpInterval();
  }
}

void updateGenerative(unsigned long now) {
  if (now - lastGenNoteTime < nextGenNoteDelay) return;
  if (currentGenNote != 255) { sendNoteOff(currentGenNote); currentGenNote = 255; }
  if ((byte)random(0,100) < GEN_TRIGGER_CHANCE) {
    byte note = (byte)constrain(
      (int)rootNote + (int)random(0,3)*12 + scaleNote(0,random(0,8)) + transpose + octave*12, 0, 127);
    currentGenNote = note;
    sendNoteOn(currentGenNote, (byte)random(60,90));
  }
  nextGenNoteDelay = (unsigned long)random(MIN_GEN_DELAY, MAX_GEN_DELAY);
  lastGenNoteTime  = now;
}

// ─────────────────────────────────────────────────────────────────────────────
//  PADS
// ─────────────────────────────────────────────────────────────────────────────
void handlePadPress(byte padIdx, unsigned long now) {
  // Verifica se é um dos 4 pads de topo (índices 12–15 = GP12–GP15)
  for (byte i = 0; i < 4; i++)
    if (padIdx == TOP_PAD_IDX[i]) { topRowPressTime[i] = now; break; }

  // Menu trigger: 4 pads simultâneos em <150ms
  if (padStates[TOP_PAD_IDX[0]] && padStates[TOP_PAD_IDX[1]] &&
      padStates[TOP_PAD_IDX[2]] && padStates[TOP_PAD_IDX[3]]) {
    unsigned long mn = topRowPressTime[0], mx = topRowPressTime[0];
    for (byte i=1;i<4;i++) {
      if (topRowPressTime[i]<mn) mn=topRowPressTime[i];
      if (topRowPressTime[i]>mx) mx=topRowPressTime[i];
    }
    if ((mx-mn) < 150UL) { toggleMenu(); topRowPressed=true; return; }
  }

  if (inMenu) return;
  playNote(padIdx, true);
}

void handlePadRelease(byte padIdx, unsigned long now) {
  (void)now;
  if (topRowPressed) {
    for (byte i=0;i<4;i++) {
      if (padIdx == TOP_PAD_IDX[i]) {
        bool allRel = true;
        for (byte j=0;j<4;j++) if (padStates[TOP_PAD_IDX[j]]) { allRel=false; break; }
        if (allRel) topRowPressed = false;
        return;
      }
    }
  }
  if (inMenu) {
    for (byte i=0;i<4;i++) {
      if (padIdx == TOP_PAD_IDX[i]) { currentPage=i+1; menuCursor=0; updateDisplay(); return; }
    }
    return;
  }
  if (playStyle == STYLE_ARPEGIO) { stopArpPad(padIdx); return; }
  if (!sustainFixed && !sustainTouchActive) playNote(padIdx, false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  PRESETS
// ─────────────────────────────────────────────────────────────────────────────
void loadPresetsFromFlash() {
  const FlashData* fd = (const FlashData*)(XIP_BASE + FLASH_TARGET_OFFSET);
  if (fd->magic == FLASH_MAGIC) {
    for (byte i=0;i<4;i++) presets[i] = fd->presets[i];
  } else {
    presets[0] = {STYLE_SCALED,     0,0, 0,-1,100,false,60,1};
    presets[1] = {STYLE_CHROMATIC,  7,0, 0,-1,100,false,60,1};
    presets[2] = {STYLE_CHORD,      0,0, 0,-1,100,false,60,1};
    presets[3] = {STYLE_ARPEGIO,    0,0, 0,-1, 90,false,60,1};
  }
}

void savePresetsToFlash() {
  FlashData data; data.magic = FLASH_MAGIC;
  for (byte i=0;i<4;i++) data.presets[i] = presets[i];
  static uint8_t buf[FLASH_PAGE_SIZE];
  memset(buf, 0xFF, sizeof(buf));
  memcpy(buf, &data, sizeof(FlashData));
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(FLASH_TARGET_OFFSET, buf, FLASH_PAGE_SIZE);
  restore_interrupts(ints);
}

void loadPreset(byte idx) {
  if (idx>=4) return;
  allNotesOff();
  playStyle=presets[idx].style; currentScale=presets[idx].scale;
  currentChordMode=presets[idx].chordMode; transpose=presets[idx].transpose;
  octave=presets[idx].octave; velocity=presets[idx].velocity;
  sustainFixed=presets[idx].sustainFixed; rootNote=presets[idx].rootNote;
  midiChannel=presets[idx].midiChannel;
  sendCC(64, (sustainFixed||sustainTouchActive) ? 127 : 0);
  updateDisplay();
}

void savePreset(byte idx) {
  if (idx>=4) return;
  presets[idx] = {playStyle,currentScale,currentChordMode,
                  transpose,octave,velocity,sustainFixed,rootNote,midiChannel};
  savePresetsToFlash();
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  // OLED
  Wire1.setSDA(OLED_SDA); Wire1.setSCL(OLED_SCL); Wire1.begin();
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(14,20); display.print("Grand-Pad v2.7");
  display.setCursor(20,33); display.print("Inicializando...");
  display.display();

  // MIDI USB
  usb_midi.setStringDescriptor("Grand-Pad");
  MIDI.begin(MIDI_CHANNEL_OMNI);

  // GPIOs
  pinMode(LED_PIN, OUTPUT);
  for (byte i=0;i<NUM_PADS;i++)  pinMode(PAD_PINS[i],  INPUT_PULLUP);
  for (byte i=0;i<NUM_CLIPS;i++) pinMode(CLIP_PINS[i], INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);

  // Harpejo
  for (byte i=0;i<ARP_MAX_PADS;i++) { arpState[i].active=false; arpState[i].lastNote=255; }

  loadPresetsFromFlash();
  loadPreset(0);

  for (byte i=0;i<4;i++) { digitalWrite(LED_PIN,HIGH); delay(80); digitalWrite(LED_PIN,LOW); delay(80); }
  delay(300);
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  flushDisplay();

  if (playStyle==STYLE_GENERATIVE && !inMenu) updateGenerative(now);
  if (playStyle==STYLE_ARPEGIO    && !inMenu) updateArpegio(now);

  // ── RESET ─────────────────────────────────────────────────────────────────
  {
    bool r = (digitalRead(RESET_PIN)==LOW);
    if (r!=lastResetBtnState) resetDebounce=now;
    if ((now-resetDebounce)>25) {
      if (r!=resetBtnState) {
        resetBtnState=r;
        if (r) { resetHoldStart=now; resetArmed=true; } else resetArmed=false;
      }
    }
    if (resetArmed && resetBtnState && (now-resetHoldStart)>=RESET_HOLD_MS) {
      display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
      display.setCursor(16,28); display.print("Entrando BOOTSEL...");
      display.display(); delay(300);
      reset_usb_boot(0,0);
    }
    lastResetBtnState=r;
  }

  // ── C1–C7 ─────────────────────────────────────────────────────────────────
  for (byte i=0;i<NUM_CLIPS;i++) {
    bool r = (digitalRead(CLIP_PINS[i])==LOW);
    if (r!=lastClipStates[i]) clipDebounce[i]=now;
    if ((now-clipDebounce[i])>CLIP_DEBOUNCE) {
      if (r!=clipStates[i]) {
        clipStates[i]=r;
        if (inMenu) { if (r) handleClipInMenu(i); }
        else        handleClipPerformance(i, r);
      }
    }
    lastClipStates[i]=r;
  }

  // ── PADS ──────────────────────────────────────────────────────────────────
  for (byte i=0;i<NUM_PADS;i++) {
    bool r = (digitalRead(PAD_PINS[i])==LOW);
    if (r!=lastPadStates[i]) lastDebounceTime[i]=now;
    if ((now-lastDebounceTime[i])>DEBOUNCE_DELAY) {
      if (r!=padStates[i]) {
        padStates[i]=r;
        if (r) handlePadPress(i,now);
        else   handlePadRelease(i,now);
      }
    }
    lastPadStates[i]=r;
  }

  MIDI.read();
}
