/*
 * ═══════════════════════════════════════════════════════════════════
 *  Cronômetro com Voltas
 *  Arduino UNO · OLED 128×64 SSD1306 · 3 botões · buzzer
 * ═══════════════════════════════════════════════════════════════════
 *  Bibliotecas: Adafruit SSD1306 + Adafruit GFX
 *
 *  Pinout:
 *    BTN_START_STOP → D2  (INPUT_PULLUP)
 *    BTN_LAP        → D3  (INPUT_PULLUP)
 *    BTN_RESET      → D4  (INPUT_PULLUP)
 *    BUZZER         → D8
 *    OLED SDA       → A4
 *    OLED SCL       → A5
 *
 *  Controles:
 *    [BTN1] PRONTO  → Inicia
 *    [BTN1] RODANDO → Pausa
 *    [BTN1] PARADO  → Retoma
 *    [BTN2] RODANDO → Registra volta
 *    [BTN3] PARADO  → Reseta tudo
 * ═══════════════════════════════════════════════════════════════════
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Pinos ────────────────────────────────────────────────────────────
#define BTN_START_STOP  2
#define BTN_LAP         3
#define BTN_RESET       4
#define BUZZER_PIN      8

// ── OLED ─────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── Configurações ────────────────────────────────────────────────────
const uint8_t  MAX_VOLTAS   = 5;
const uint8_t  DEBOUNCE_MS  = 50;
const uint8_t  DISPLAY_RATE = 50;   // ms entre frames (~20 FPS)

// ── Máquina de estados ───────────────────────────────────────────────
enum Estado : uint8_t { PRONTO, RODANDO, PARADO };
Estado estado = PRONTO;

// ── Timing ───────────────────────────────────────────────────────────
unsigned long tempoBase = 0;          // millis() ao iniciar/retomar
unsigned long tempoAcum = 0;          // ms acumulados antes de pausas
unsigned long lapTimes[MAX_VOLTAS];   // tempo cumulativo de cada volta
uint8_t       numLaps   = 0;

// ── Debounce com detecção de borda ───────────────────────────────────
struct Botao {
  uint8_t       pino;
  bool          prevState;
  bool          ready;
  unsigned long lastDebounce;
};

Botao btnStart = { BTN_START_STOP, HIGH, true, 0 };
Botao btnLap   = { BTN_LAP,        HIGH, true, 0 };
Botao btnReset = { BTN_RESET,      HIGH, true, 0 };

// Retorna true apenas na borda de descida confirmada pelo debounce
bool lerBotao(Botao &b) {
  bool leitura = digitalRead(b.pino);
  if (leitura != b.prevState) {
    b.lastDebounce = millis();
    b.prevState    = leitura;
  }
  if ((millis() - b.lastDebounce) > DEBOUNCE_MS) {
    if (leitura == LOW && b.ready) { b.ready = false; return true; }
    if (leitura == HIGH)             b.ready = true;
  }
  return false;
}

// ── Buzzer ───────────────────────────────────────────────────────────
void beepStart() { tone(BUZZER_PIN, 1000,  100); }
void beepLap()   { tone(BUZZER_PIN, 2000,   60); }
void beepStop()  {
  tone(BUZZER_PIN, 700, 80);
  delay(130);
  tone(BUZZER_PIN, 700, 80);
}
void beepReset() { tone(BUZZER_PIN,  400, 220); }

// ── Utilitários de tempo ─────────────────────────────────────────────
unsigned long tempoAtual() {
  return (estado == RODANDO)
    ? tempoAcum + (millis() - tempoBase)
    : tempoAcum;
}

// Tempo individual da volta i (split), calculado a partir dos acumulados
unsigned long splitTime(uint8_t i) {
  return (i == 0) ? lapTimes[0] : lapTimes[i] - lapTimes[i - 1];
}

// Formata ms em "MM:SS.mmm"
void formatTime(unsigned long ms, char* buf) {
  uint8_t  min = (uint8_t)(ms / 60000UL);
  uint8_t  sec = (uint8_t)((ms % 60000UL) / 1000UL);
  uint16_t mil = (uint16_t)(ms % 1000UL);
  sprintf(buf, "%02u:%02u.%03u", (unsigned)min, (unsigned)sec, (unsigned)mil);
}

// ── Renderização OLED ────────────────────────────────────────────────
//
//  Layout (128×64) — OLED bicolor: y=0..15 AMARELO | y=16..63 AZUL
//  ┌────────────────────────────────┐
//  │ RODANDO           LAP:3        │  y= 0  Status (font 1) ── AMARELO
//  ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤  y=16  ── fronteira hardware ──
//  │ 00:12.345                      │  y=16  Tempo total (font 2) ── AZUL
//  ├────────────────────────────────┤  y=33  separador
//  │ V1  00:04.234                  │  y=36  Splits das voltas (font 1)
//  │ V2  00:05.111                  │  y=46
//  │ V3  00:03.000                  │  y=56
//  └────────────────────────────────┘
//
void atualizarDisplay() {
  display.clearDisplay();
  char buf[12];

  // ─ Linha de status ─
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  if      (estado == RODANDO) display.print("RODANDO");
  else if (estado == PARADO)  display.print(" PARADO");
  else                        display.print(" PRONTO");

  if (numLaps > 0) {
    char lapBuf[8];
    sprintf(lapBuf, "LAP:%u", (unsigned)numLaps);
    display.setCursor(86, 0);
    display.print(lapBuf);
  }

  // ─ Tempo total (fonte 2) — inicia em y=16 para ficar na zona azul ─
  formatTime(tempoAtual(), buf);
  display.setTextSize(2);
  display.setCursor(2, 16);
  display.print(buf);

  // ─ Divisor horizontal ─
  display.drawFastHLine(0, 33, 128, SSD1306_WHITE);

  // ─ Histórico de voltas ou hint de controles ─
  display.setTextSize(1);

  if (numLaps == 0) {
    // Mostra dica de controles quando não há voltas registradas
    display.setCursor(4, 38);
    display.print("[1] Start / Stop");
    display.setCursor(4, 50);
    display.print("[2] Volta   [3] Reset");
  } else {
    // Mostra as últimas 3 voltas (split time por volta) — 10px de espaçamento
    int8_t inicio = (int8_t)numLaps - 3;
    if (inicio < 0) inicio = 0;
    for (uint8_t i = (uint8_t)inicio; i < numLaps; i++) {
      char linha[20];
      formatTime(splitTime(i), buf);
      sprintf(linha, "V%u  %s", (unsigned)(i + 1), buf);
      display.setCursor(2, 36 + (i - inicio) * 10);
      display.print(linha);
    }
  }

  display.display();
}

// ── Ações da máquina de estados ──────────────────────────────────────
void acaoStartStop() {
  if (estado == RODANDO) {
    // Pausa: salva tempo acumulado antes de chamar o beep
    tempoAcum += millis() - tempoBase;
    estado = PARADO;
    beepStop();
  } else {
    // Inicia ou retoma
    tempoBase = millis();
    estado = RODANDO;
    beepStart();
  }
}

void acaoLap() {
  if (estado != RODANDO)    return;
  if (numLaps >= MAX_VOLTAS) return;
  lapTimes[numLaps++] = tempoAtual();
  beepLap();
}

void acaoReset() {
  if (estado == RODANDO) return;  // ignora reset com o cronômetro ativo
  tempoAcum = 0;
  tempoBase = 0;
  numLaps   = 0;
  estado    = PRONTO;
  beepReset();
}

// ── Setup ─────────────────────────────────────────────────────────────
void setup() {
  pinMode(BTN_START_STOP, INPUT_PULLUP);
  pinMode(BTN_LAP,        INPUT_PULLUP);
  pinMode(BTN_RESET,      INPUT_PULLUP);
  pinMode(BUZZER_PIN,     OUTPUT);

  // Falha de inicialização do display: sinaliza com beeps indefinidos
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
      tone(BUZZER_PIN, 440, 300);
      delay(500);
    }
  }

  display.clearDisplay();
  display.display();
  atualizarDisplay();
}

// ── Loop ──────────────────────────────────────────────────────────────
void loop() {
  bool atualizar = false;

  if (lerBotao(btnStart)) { acaoStartStop(); atualizar = true; }
  if (lerBotao(btnLap))   { acaoLap();       atualizar = true; }
  if (lerBotao(btnReset)) { acaoReset();     atualizar = true; }

  // Refresh contínuo quando rodando, limitado a ~20 FPS
  static unsigned long ultimoFrame = 0;
  if (estado == RODANDO && (millis() - ultimoFrame) >= DISPLAY_RATE) {
    ultimoFrame = millis();
    atualizar   = true;
  }

  if (atualizar) atualizarDisplay();
}
