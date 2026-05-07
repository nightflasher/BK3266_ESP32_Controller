// ============================================================
//  logo.h — SSD1306 128x64 Bootlogo
//
//  Ersetze diesen Platzhalter mit deinem eigenen Logo:
//  1. logo_converter.html im Browser öffnen
//  2. Bild laden oder direkt zeichnen
//  3. "Generieren" -> "logo.h speichern"
//  4. Diese Datei ersetzen
//
//  Verwendung in main.cpp (bereits eingebaut):
//    display.drawBitmap(0, 0, logo_bmp, 128, 64, SSD1306_WHITE);
// ============================================================

#pragma once
#include <pgmspace.h>

// Platzhalter: leeres Display (alle Pixel aus)
// Wird durch showSplash() Fallback-Text ersetzt solange
// LOGO_BMP_DEFINED nicht definiert ist.

// Sobald du ein echtes Logo hast:
// 1. Den Kommentar unten entfernen
// 2. #define LOGO_BMP_DEFINED einkommentieren
// 3. Das generierte Array hier einfügen

// #define LOGO_BMP_DEFINED
// static const uint8_t PROGMEM logo_bmp[] = {
//   0x00, 0x00, ...
// };
