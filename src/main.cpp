// ============================================================
//  BK3266 / F-6988 V3.1 Controller
//
//  Hardware:
//    - Heltec WiFi Kit 32 (ESP32, OLED 128x64 SSD1306 onboard)
//    - BK3266 / F-6988 V3.1 Bluetooth Audio Transmitter
//    - 3 Taster (nackt, kein Board)
//
//  Pinbelegung:
//    OLED SDA        -> GPIO4  (Heltec intern)
//    OLED SCL        -> GPIO15 (Heltec intern)
//    OLED RST        -> GPIO16 (Heltec intern)
//
//    BK3266 RX       -> GPIO17 (ESP TX -> BK RX, 3.3V direkt)
//    BK3266 TX       -> GPIO18 (BK TX  -> ESP RX)
//
//    Taster UP       -> GPIO25  (gegen GND, INPUT_PULLUP)
//    Taster DOWN     -> GPIO26  (gegen GND, INPUT_PULLUP)
//    Taster ENTER    -> GPIO27  (gegen GND, INPUT_PULLUP)
//
//  BK3266 UART: 9600 Baud, 8N1, 3.3V, Befehle mit \r\n
//
//  Bedienung:
//    UP              -> Cursor hoch / Wert erhöhen
//    DOWN            -> Cursor runter / Wert verringern
//    ENTER kurz      -> Bestätigen / Auswählen
//    ENTER lang      -> Zurück / Abbrechen / Speichern
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "logo.h"

// ---- OLED ----
#define OLED_SDA      4
#define OLED_SCL      15
#define OLED_RST      16
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

// ---- UART BK3266 ----
#define BK_RX_PIN  18
#define BK_TX_PIN  17
HardwareSerial bkSerial(2);

// ---- Buttons ----
#define BTN_UP    25
#define BTN_DOWN  26
#define BTN_ENTER 27

#define DEBOUNCE_MS      50
#define LONG_PRESS_MS   800
#define REPEAT_DELAY_MS 400
#define REPEAT_RATE_MS  150

struct Button {
  uint8_t       pin;
  bool          lastRaw;
  bool          state;
  unsigned long pressTime;
  bool          longFired;
  bool          evShort;
  bool          evLong;
  bool          evDown;
  unsigned long repeatNext;
};

Button btnUp    = { BTN_UP };
Button btnDown  = { BTN_DOWN };
Button btnEnter = { BTN_ENTER };

// ---- Zustandsmaschine ----
enum AppState {
  STATE_IDLE,
  STATE_PAIRING,
  STATE_MAIN_MENU,
  STATE_VOLUME,
  STATE_EQ,
  STATE_SETTINGS_MENU,
  STATE_RENAME,
};
AppState appState = STATE_IDLE;

// ---- BT Status ----
bool          isConnected      = false;
bool          autoReconnect    = true;
bool          beepOn           = true;
int           currentVolume    = 8;
String        btName           = "BT-TX";
unsigned long pairingStartTime = 0;
#define PAIRING_TIMEOUT_MS 30000

// ---- Hauptmenü ----
enum MainMenuItem {
  MM_PAIR, MM_CONNECT, MM_DISCONNECT, MM_VOLUME, MM_EQ, MM_SETTINGS, MM_COUNT
};
const char* mainMenuLabels[] = {
  "Pairing", "Verbinden", "Trennen", "Lautstaerke", "EQ", "Einstellungen"
};
int mainMenuIndex = 0;

// ---- EQ ----
const char* eqPresets[] = {
  "NORMAL", "BOOST", "TREBLE", "POP", "ROCK", "CLASSIC", "JAZZ", "DANCE", "R&P"
};
const int EQ_COUNT = 9;
int currentEQ     = 0;
int eqSelectIndex = 0;

// ---- Einstellungen ----
enum SettingsMenuItem { SM_AUTORECONNECT, SM_BEEP, SM_RENAME, SM_COUNT };
const char* settingsMenuLabels[] = { "Auto-Reconnect", "Beep", "BT-Name" };
int settingsMenuIndex = 0;

// ---- Rename ----
char       renameBuffer[17]    = "BT-TX";
int        renameCursor        = 0;
int        renameLen           = 5;
bool       renameEditing       = false;
const char RENAME_CHARS[]      = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";
const int  RENAME_CHAR_COUNT   = 39;
int        renameCharIndex     = 0;

// ---- BK Parser ----
String        bkBuffer          = "";
unsigned long lastStatusRequest = 0;

// ---- Forward declarations ----
void drawIdle();
void drawMainMenu();
void drawPairing();
void drawVolume();
void drawEQ();
void drawSettingsMenu();
void drawRename();
void drawInfo(const String& line1, const String& line2);
void onUp();
void onDown();
void onEnterShort();
void onEnterLong();
void sendBK(const String& cmd);
void parseBKLine(const String& line);

// ============================================================
//  BUTTONS
// ============================================================
void initButton(Button& b) {
  pinMode(b.pin, INPUT_PULLUP);
  b.lastRaw    = HIGH;
  b.state      = HIGH;
  b.longFired  = false;
  b.evShort    = false;
  b.evLong     = false;
  b.evDown     = false;
  b.repeatNext = 0;
}

void updateButton(Button& b) {
  bool raw     = digitalRead(b.pin);
  b.lastRaw    = raw;
  bool pressed = (raw == LOW);

  if (pressed && b.state == HIGH) {
    b.state      = LOW;
    b.pressTime  = millis();
    b.longFired  = false;
    b.evDown     = true;
    b.repeatNext = millis() + REPEAT_DELAY_MS;
  } else if (!pressed && b.state == LOW) {
    b.state = HIGH;
    if (!b.longFired) b.evShort = true;
    b.longFired = false;
  } else if (pressed && b.state == LOW) {
    if (!b.longFired && millis() - b.pressTime >= LONG_PRESS_MS) {
      b.longFired = true;
      b.evLong    = true;
    }
  }
}

int getRepeat(Button& b) {
  if (b.state == LOW && !b.longFired &&
      millis() - b.pressTime >= REPEAT_DELAY_MS &&
      millis() >= b.repeatNext) {
    b.repeatNext = millis() + REPEAT_RATE_MS;
    return 1;
  }
  return 0;
}

void pollButtons() {
  updateButton(btnUp);
  updateButton(btnDown);
  updateButton(btnEnter);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED fehlt!");
    while (true) {}
  }
  display.setTextColor(SSD1306_WHITE);
  showSplash();

  // BK3266
  bkSerial.begin(9600, SERIAL_8N1, BK_RX_PIN, BK_TX_PIN);

  // Buttons
  initButton(btnUp);
  initButton(btnDown);
  initButton(btnEnter);

  delay(1500);

  sendBK("COM+MBT");    delay(200);
  sendBK("COM+GV");     delay(200);
  sendBK("COM+MEQ");    delay(200);
  sendBK("COM+MGOBACK"); delay(200);
  sendBK("COM+MTONE");

  appState = STATE_IDLE;
  drawIdle();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  pollButtons();
  handleBKSerial();
  handleEvents();

  if (appState == STATE_PAIRING &&
      millis() - pairingStartTime > PAIRING_TIMEOUT_MS) {
    appState = STATE_IDLE;
    drawIdle();
  }

  if (isConnected && millis() - lastStatusRequest > 5000) {
    sendBK("COM+GV");
    lastStatusRequest = millis();
  }
}

// ============================================================
//  EVENTS
// ============================================================
void handleEvents() {
  if (btnUp.evShort)    { btnUp.evShort    = false; onUp();         }
  if (getRepeat(btnUp))                              onUp();

  if (btnDown.evShort)  { btnDown.evShort  = false; onDown();       }
  if (getRepeat(btnDown))                            onDown();

  if (btnEnter.evShort) { btnEnter.evShort = false; onEnterShort(); }
  if (btnEnter.evLong)  { btnEnter.evLong  = false; onEnterLong();  }
}

// ============================================================
//  UP
// ============================================================
void onUp() {
  switch (appState) {
    case STATE_MAIN_MENU:
      mainMenuIndex = (mainMenuIndex - 1 + MM_COUNT) % MM_COUNT;
      drawMainMenu();
      break;
    case STATE_VOLUME:
      currentVolume = constrain(currentVolume + 1, 0, 16);
      sendBK("COM+V" + zeroPad(currentVolume));
      drawVolume();
      break;
    case STATE_EQ:
      eqSelectIndex = (eqSelectIndex - 1 + EQ_COUNT) % EQ_COUNT;
      drawEQ();
      break;
    case STATE_SETTINGS_MENU:
      settingsMenuIndex = (settingsMenuIndex - 1 + SM_COUNT) % SM_COUNT;
      drawSettingsMenu();
      break;
    case STATE_RENAME:
      if (renameEditing) {
        renameCharIndex = (renameCharIndex - 1 + RENAME_CHAR_COUNT) % RENAME_CHAR_COUNT;
        renameBuffer[renameCursor] = RENAME_CHARS[renameCharIndex];
      } else {
        renameCursor = constrain(renameCursor - 1, 0, renameLen);
      }
      drawRename();
      break;
    default: break;
  }
}

// ============================================================
//  DOWN
// ============================================================
void onDown() {
  switch (appState) {
    case STATE_MAIN_MENU:
      mainMenuIndex = (mainMenuIndex + 1) % MM_COUNT;
      drawMainMenu();
      break;
    case STATE_VOLUME:
      currentVolume = constrain(currentVolume - 1, 0, 16);
      sendBK("COM+V" + zeroPad(currentVolume));
      drawVolume();
      break;
    case STATE_EQ:
      eqSelectIndex = (eqSelectIndex + 1) % EQ_COUNT;
      drawEQ();
      break;
    case STATE_SETTINGS_MENU:
      settingsMenuIndex = (settingsMenuIndex + 1) % SM_COUNT;
      drawSettingsMenu();
      break;
    case STATE_RENAME:
      if (renameEditing) {
        renameCharIndex = (renameCharIndex + 1) % RENAME_CHAR_COUNT;
        renameBuffer[renameCursor] = RENAME_CHARS[renameCharIndex];
      } else {
        renameCursor = constrain(renameCursor + 1, 0, min(renameLen, 15));
      }
      drawRename();
      break;
    default: break;
  }
}

// ============================================================
//  ENTER KURZ
// ============================================================
void onEnterShort() {
  switch (appState) {
    case STATE_IDLE:
      mainMenuIndex = 0;
      appState      = STATE_MAIN_MENU;
      drawMainMenu();
      break;
    case STATE_MAIN_MENU:
      executeMainMenu();
      break;
    case STATE_EQ:
      currentEQ = eqSelectIndex;
      sendBK("COM+SETEQ" + String(eqPresets[currentEQ]));
      appState = STATE_IDLE;
      drawIdle();
      break;
    case STATE_SETTINGS_MENU:
      executeSettingsMenu();
      break;
    case STATE_RENAME:
      if (!renameEditing) {
        if (renameCursor == renameLen && renameLen < 16) {
          renameBuffer[renameLen] = 'A';
          renameLen++;
          renameBuffer[renameLen] = '\0';
        }
        renameEditing   = true;
        renameCharIndex = 0;
        char c = renameBuffer[renameCursor];
        for (int i = 0; i < RENAME_CHAR_COUNT; i++) {
          if (RENAME_CHARS[i] == c) { renameCharIndex = i; break; }
        }
      } else {
        renameEditing = false;
        if (renameCursor < renameLen - 1) renameCursor++;
      }
      drawRename();
      break;
    case STATE_PAIRING:
      sendBK("BT+DC");
      appState = STATE_IDLE;
      drawIdle();
      break;
    default: break;
  }
}

// ============================================================
//  ENTER LANG  ->  Zurück / Speichern
// ============================================================
void onEnterLong() {
  switch (appState) {
    case STATE_IDLE:
      startPairing();
      break;
    case STATE_MAIN_MENU:
      appState = STATE_IDLE;
      drawIdle();
      break;
    case STATE_VOLUME:
    case STATE_EQ:
      appState = STATE_IDLE;
      drawIdle();
      break;
    case STATE_SETTINGS_MENU:
      appState = STATE_MAIN_MENU;
      drawMainMenu();
      break;
    case STATE_RENAME:
      if (renameEditing) {
        renameEditing = false;
        drawRename();
      } else {
        commitRename();
      }
      break;
    case STATE_PAIRING:
      sendBK("BT+DC");
      appState = STATE_IDLE;
      drawIdle();
      break;
    default:
      appState = STATE_IDLE;
      drawIdle();
      break;
  }
}

// ============================================================
//  MENÜ-AKTIONEN
// ============================================================
void executeMainMenu() {
  switch ((MainMenuItem)mainMenuIndex) {
    case MM_PAIR:
      startPairing();
      break;
    case MM_CONNECT:
      sendBK("BT+AC");
      drawInfo("Verbinde...", "Letztes Geraet");
      delay(1000);
      appState = STATE_IDLE;
      drawIdle();
      break;
    case MM_DISCONNECT:
      sendBK("BT+DC");
      isConnected = false;
      appState    = STATE_IDLE;
      drawIdle();
      break;
    case MM_VOLUME:
      appState = STATE_VOLUME;
      drawVolume();
      break;
    case MM_EQ:
      eqSelectIndex = currentEQ;
      appState      = STATE_EQ;
      drawEQ();
      break;
    case MM_SETTINGS:
      settingsMenuIndex = 0;
      appState          = STATE_SETTINGS_MENU;
      drawSettingsMenu();
      break;
  }
}

void executeSettingsMenu() {
  switch ((SettingsMenuItem)settingsMenuIndex) {
    case SM_AUTORECONNECT:
      autoReconnect = !autoReconnect;
      sendBK(autoReconnect ? "COM+GOBACKON" : "COM+GOBACKOFF");
      drawSettingsMenu();
      break;
    case SM_BEEP:
      beepOn = !beepOn;
      sendBK(beepOn ? "COM+TONEON" : "COM+TONEOFF");
      drawSettingsMenu();
      break;
    case SM_RENAME:
      strncpy(renameBuffer, btName.c_str(), 16);
      renameBuffer[16] = '\0';
      renameLen        = btName.length();
      renameCursor     = 0;
      renameEditing    = false;
      appState         = STATE_RENAME;
      drawRename();
      break;
  }
}

void startPairing() {
  pairingStartTime = millis();
  appState         = STATE_PAIRING;
  sendBK("BT+PR");
  drawPairing();
}

void commitRename() {
  renameBuffer[renameLen] = '\0';
  btName = String(renameBuffer);
  sendBK("COM+SNAME+" + btName);
  delay(200);
  sendBK("COM+REBOOT");
  drawInfo("Name gespeichert", "Neustart...");
  delay(2000);
  appState = STATE_IDLE;
  drawIdle();
}

// ============================================================
//  BK3266
// ============================================================
void sendBK(const String& cmd) {
  Serial.print("[BK TX] "); Serial.println(cmd);
  bkSerial.print(cmd + "\r\n");
}

String zeroPad(int val) {
  return (val < 10 ? "0" : "") + String(val);
}

void handleBKSerial() {
  while (bkSerial.available()) {
    char c = bkSerial.read();
    if (c == '\n') {
      bkBuffer.trim();
      if (bkBuffer.length() > 0) parseBKLine(bkBuffer);
      bkBuffer = "";
    } else if (c != '\r') {
      bkBuffer += c;
    }
  }
}

void parseBKLine(const String& line) {
  Serial.print("[BK RX] "); Serial.println(line);

  if (line.startsWith("BT_AC")) {
    isConnected = true;
    if (appState == STATE_PAIRING) appState = STATE_IDLE;
    drawIdle();
  }
  if (line.startsWith("BT_WC")) {
    isConnected = false;
    drawIdle();
  }
  if (line == "SY_PO") {
    sendBK("COM+GV");
    sendBK("COM+MGOBACK");
  }
  if (line.startsWith("COM_V") && line.length() >= 7) {
    int vol = line.substring(5).toInt();
    if (vol >= 0 && vol <= 16) {
      currentVolume = vol;
      if (appState == STATE_VOLUME) drawVolume();
      if (appState == STATE_IDLE)   drawIdle();
    }
  }
  if (line == "GOBACKON")  autoReconnect = true;
  if (line == "GOBACKOFF") autoReconnect = false;
  if (line == "TOMEON")    beepOn = true;
  if (line == "TOMEOFF")   beepOn = false;
  for (int i = 0; i < EQ_COUNT; i++) {
    if (line == String(eqPresets[i])) { currentEQ = i; break; }
  }
  if (appState == STATE_PAIRING) drawPairing();
}

// ============================================================
//  DISPLAY
// ============================================================
void drawCentered(const String& text, int y, int sz = 1) {
  display.setTextSize(sz);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

void drawDivider(int y) {
  display.drawLine(0, y, SCREEN_WIDTH - 1, y, SSD1306_WHITE);
}

void drawMenuList(const char** labels, int count, int selected, int yStart) {
  // 128x64: 5 Einträge sichtbar à 10px
  int visible = 5;
  int start   = max(0, selected - 2);
  if (start + visible > count) start = max(0, count - visible);

  for (int i = 0; i < visible && (start + i) < count; i++) {
    int idx = start + i;
    int y   = yStart + i * 10;
    if (idx == selected) {
      display.fillRect(0, y - 1, SCREEN_WIDTH, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setTextSize(1);
    display.setCursor(4, y);
    display.print(labels[idx]);
  }
  display.setTextColor(SSD1306_WHITE);
}

void showSplash() {
  display.clearDisplay();
#ifdef LOGO_BMP_DEFINED
  display.drawBitmap(0, 0, logo_bmp, 128, 64, SSD1306_WHITE);
#else
  drawCentered("BT Audio TX", 20, 2);
  drawDivider(38);
  drawCentered("BK3266 F-6988", 44, 1);
#endif
  display.display();
}

void drawIdle() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("BT Audio TX");
  display.setCursor(84, 0);
  display.print(isConnected ? "[ON] " : "[OFF]");
  drawDivider(10);
  if (isConnected) {
    drawCentered("Verbunden", 16, 2);
    // Lautstärke-Balken
    int barWidth = map(currentVolume, 0, 16, 0, 100);
    display.setCursor(0, 50);
    display.print("Vol:");
    display.fillRect(28, 51, barWidth, 7, SSD1306_WHITE);
    display.drawRect(28, 51, 100, 7, SSD1306_WHITE);
    display.setCursor(112, 50);
    display.print(currentVolume);
  } else {
    drawCentered("ENT = Menue", 22, 1);
    drawCentered("ENT lang = Pairing", 36, 1);
  }
  display.display();
}

void drawMainMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("MENUE");
  display.setCursor(90, 0);
  display.print(isConnected ? "[ON]" : "[--]");
  drawDivider(10);
  drawMenuList(mainMenuLabels, MM_COUNT, mainMenuIndex, 13);
  display.display();
}

void drawPairing() {
  display.clearDisplay();
  drawCentered("PAIRING...", 8, 2);
  drawDivider(26);
  unsigned long elapsed  = millis() - pairingStartTime;
  int           progress = map(
    min(elapsed, (unsigned long)PAIRING_TIMEOUT_MS),
    0, PAIRING_TIMEOUT_MS, 0, SCREEN_WIDTH - 2);
  display.drawRect(0, 34, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.fillRect(1, 35, progress, 10, SSD1306_WHITE);
  drawCentered("ENT = Abbruch", 52, 1);
  display.display();
}

void drawVolume() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("LAUTSTAERKE");
  display.setCursor(96, 0);
  display.print("UP/DN");
  drawDivider(10);
  // Große Zahl
  drawCentered(String(currentVolume), 16, 4);
  // Balken
  int barWidth = map(currentVolume, 0, 16, 0, SCREEN_WIDTH - 2);
  display.drawRect(0, 56, SCREEN_WIDTH, 8, SSD1306_WHITE);
  display.fillRect(1, 57, barWidth, 6, SSD1306_WHITE);
  display.display();
}

void drawEQ() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("EQ  Akt: ");
  display.print(eqPresets[currentEQ]);
  drawDivider(10);
  drawMenuList(eqPresets, EQ_COUNT, eqSelectIndex, 13);
  display.display();
}

void drawSettingsMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("EINSTELLUNGEN");
  drawDivider(10);
  int start = max(0, settingsMenuIndex - 2);
  for (int i = 0; i < 5 && (start + i) < SM_COUNT; i++) {
    int  idx    = start + i;
    int  y      = 13 + i * 10;
    bool active = (idx == SM_AUTORECONNECT) ? autoReconnect
                : (idx == SM_BEEP)          ? beepOn : false;
    if (idx == settingsMenuIndex) {
      display.fillRect(0, y - 1, SCREEN_WIDTH, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y);
    display.print(settingsMenuLabels[idx]);
    if (idx != SM_RENAME) {
      display.setCursor(104, y);
      display.print(active ? "EIN" : "AUS");
    }
  }
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void drawRename() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("BT-NAME");
  display.setCursor(64, 0);
  display.print(renameEditing ? "[Zeichen]" : "[Cursor] ");
  drawDivider(10);

  // Name in doppelter Größe
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print(renameBuffer);

  // Cursor/Edit-Highlight
  int charX = renameCursor * 12;
  if (renameEditing) {
    display.fillRect(charX, 13, 12, 17, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(charX, 14);
    display.print(renameBuffer[renameCursor]);
    display.setTextColor(SSD1306_WHITE);
  } else {
    display.drawLine(charX, 30, charX + 11, 30, SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print(renameEditing ? "ENT:bestaetigen  EL:Exit"
                              : "ENT:editieren  EL:Spch.");
  display.display();
}

void drawInfo(const String& line1, const String& line2) {
  display.clearDisplay();
  drawCentered(line1, 18, 2);
  drawDivider(36);
  drawCentered(line2, 44, 1);
  display.display();
}
