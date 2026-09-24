// Home Assistant touch remote with a picture-frame screensaver
//
// ESP32-WROOM-32 + Hosyond 4.0" 480x320 ST7796S touch display with SD slot.
//
// Three pages, picked with the tabs along the bottom:
//   Weather   - the Sainlogic SA68 station (through the 433 MHz receiver)
//   Driveway  - the driveway alarm and the lightning detector
//   Controls  - six buttons that turn lights and switches on and off
//
// After a few minutes without a touch it becomes a picture frame, showing the
// JPEG photos in the /photos folder of the SD card (use tools/prepare_photos to
// shrink them first). A tap brings the dashboard back. When the driveway alarm
// goes off, the remote wakes up on the Driveway page.
//
// It talks to Home Assistant over its REST API with a long-lived access token
// (see secrets.h). Which entities it shows is set in config.h.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>       // TFT_eSPI and TJpg_Decoder expect it to be found
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>
#include <vector>
#include "secrets.h"
#include "config.h"

// ---------- Pins (display, touch and SD share SCK 18 / MOSI 23 / MISO 19) ----------

const int SD_CS = 22;
const int BACKLIGHT_PIN = 32;
const int BACKLIGHT_CH = 0;

// ---------- Screen ----------

TFT_eSPI tft;
const int W = 480, H = 320;
const int HEADER_H = 40;
const int TABS_Y = 276;

uint16_t C_BG, C_PANEL, C_TEXT, C_MUTED, C_ACCENT, C_GREEN, C_RED, C_AMBER, C_LINE;

void setupColors() {
  C_BG     = tft.color565(14, 18, 26);
  C_PANEL  = tft.color565(30, 38, 50);
  C_LINE   = tft.color565(46, 55, 68);
  C_TEXT   = tft.color565(230, 235, 242);
  C_MUTED  = tft.color565(150, 162, 178);
  C_ACCENT = tft.color565(76, 195, 217);
  C_GREEN  = tft.color565(46, 125, 50);
  C_RED    = tft.color565(211, 47, 47);
  C_AMBER  = tft.color565(242, 184, 36);
}

void setBacklight(uint8_t level) { ledcWrite(BACKLIGHT_CH, level); }

// ---------- Home Assistant ----------

struct Entity {
  const char* id;
  char state[40] = "";
  char unit[8] = "";
  time_t changed = 0;     // last_changed, UTC
  bool ok = false;        // got a valid reply
};

enum {
  EN_TEMP, EN_HUM, EN_WIND, EN_GUST, EN_DIR, EN_RAIN, EN_WEATHER,
  EN_DRIVEWAY, EN_LNEAR, EN_LMI, EN_STRIKES,
  EN_CTRL0, EN_COUNT = EN_CTRL0 + 6
};
Entity ents[EN_COUNT];

WiFiClient haNet;
String haBase;            // "http://192.168.1.20:8123"
bool haReachable = false;
uint32_t lastHaResolve = 0;

bool haConfigured() { return HA_TOKEN[0] != 0; }

// homeassistant.local needs an mDNS lookup; the ESP32 can't resolve .local by itself.
void resolveHa() {
  lastHaResolve = millis();
  String host = HA_HOST;
  if (host.endsWith(".local")) {
    IPAddress ip = MDNS.queryHost(host.substring(0, host.length() - 6), 2000);
    if (ip == IPAddress()) {
      Serial.printf("Could not find %s on the network\n", HA_HOST);
      haBase = "";
      return;
    }
    host = ip.toString();
  }
  haBase = "http://" + host + ":" + HA_PORT;
  Serial.printf("Home Assistant at %s\n", haBase.c_str());
}

// Turns "2026-09-24T14:05:03.123+00:00" (always UTC from HA) into a time_t.
time_t parseIsoUtc(const char* s) {
  int y, mo, d, h, mi, se;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  // days from civil (Howard Hinnant's algorithm)
  y -= mo <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (time_t)(days * 86400L + h * 3600L + mi * 60L + se);
}

// Reads one entity's state. Returns true if the display needs redrawing.
bool haFetch(Entity& e) {
  if (!haConfigured() || WiFi.status() != WL_CONNECTED) return false;
  if (haBase.isEmpty()) {
    if (millis() - lastHaResolve > 30000) resolveHa();
    if (haBase.isEmpty()) return false;
  }
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(3000);
  http.begin(haNet, haBase + "/api/states/" + e.id);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  int code = http.GET();

  char oldState[sizeof(e.state)];
  strlcpy(oldState, e.state, sizeof(oldState));
  bool wasOk = e.ok;

  if (code == 200) {
    JsonDocument filter;
    filter["state"] = true;
    filter["last_changed"] = true;
    filter["attributes"]["unit_of_measurement"] = true;
    JsonDocument doc;
    if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
      strlcpy(e.state, doc["state"] | "", sizeof(e.state));
      strlcpy(e.unit, doc["attributes"]["unit_of_measurement"] | "", sizeof(e.unit));
      e.changed = parseIsoUtc(doc["last_changed"] | "");
      e.ok = strcmp(e.state, "unavailable") != 0 && strcmp(e.state, "unknown") != 0;
    }
    haReachable = true;
  } else if (code == 404) {
    e.ok = false;          // no such entity: shows "--"
    haReachable = true;
  } else {
    if (code < 0) haBase = "";   // connection failed: look the address up again
    haReachable = false;
    if (code == 401) Serial.println("Home Assistant says the token is wrong (401)");
  }
  http.end();
  return e.ok != wasOk || strcmp(oldState, e.state) != 0;
}

bool haToggle(const char* entity) {
  if (!haConfigured() || haBase.isEmpty()) return false;
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(haNet, haBase + "/api/services/homeassistant/toggle");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(String("{\"entity_id\":\"") + entity + "\"}");
  http.end();
  return code == 200;
}

// ---------- Values for display ----------

float num(int i) { return atof(ents[i].state); }
bool isOn(int i) { return ents[i].ok && strcmp(ents[i].state, "on") == 0; }

// "72" or "--"
String rounded(int i) {
  if (!ents[i].ok) return "--";
  return String((int)lroundf(num(i)));
}

String relativeTime(time_t t) {
  time_t now = time(nullptr);
  if (t == 0 || now < 1700000000) return "--";
  long s = (long)(now - t);
  if (s < 60) return "just now";
  if (s < 3600) return String(s / 60) + " min ago";
  if (s < 86400) return String(s / 3600) + " h ago";
  return String(s / 86400) + " days ago";
}

String compass(float deg) {
  const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return dirs[((int)lroundf(deg / 45.0f)) & 7];
}

// "partlycloudy" -> "Partly cloudy", "clear-night" -> "Clear"
String weatherText(const char* s) {
  struct { const char* k; const char* v; } map[] = {
    {"clear-night", "Clear"}, {"cloudy", "Cloudy"}, {"exceptional", "Severe weather"},
    {"fog", "Fog"}, {"hail", "Hail"}, {"lightning", "Thunderstorms"},
    {"lightning-rainy", "Thunderstorms and rain"}, {"partlycloudy", "Partly cloudy"},
    {"pouring", "Heavy rain"}, {"rainy", "Rain"}, {"snowy", "Snow"},
    {"snowy-rainy", "Sleet"}, {"sunny", "Sunny"}, {"windy", "Windy"},
    {"windy-variant", "Windy and cloudy"}};
  for (auto& m : map)
    if (strcmp(s, m.k) == 0) return m.v;
  return s;
}

// Heat index in °F (NWS formula), only when it's hot and humid.
float feelsLikeF(float t, float h) {
  if (t < 80 || h < 40) return t;
  return -42.379f + 2.04901523f * t + 10.14333127f * h - 0.22475541f * t * h -
         0.00683783f * t * t - 0.05481717f * h * h + 0.00122874f * t * t * h +
         0.00085282f * t * h * h - 0.00000199f * t * t * h * h;
}

// ---------- Drawing helpers ----------

enum Page { PG_WEATHER, PG_DRIVEWAY, PG_CONTROLS, PG_COUNT };
Page page = PG_WEATHER;
const char* PAGE_NAMES[] = {"Weather", "Driveway", "Controls"};

void text(const String& s, int x, int y, const GFXfont* font, uint16_t color,
          uint8_t datum = TL_DATUM, uint16_t bg = 0, bool fillBg = false) {
  tft.setFreeFont(font);
  if (fillBg) tft.setTextColor(color, bg); else tft.setTextColor(color);
  tft.setTextDatum(datum);
  tft.drawString(s, x, y);
}

void drawHeader() {
  tft.fillRect(0, 0, W, HEADER_H, C_BG);
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char clock[12], date[20];
    strftime(clock, sizeof(clock), "%I:%M %p", &t);
    strftime(date, sizeof(date), "%a %b %d", &t);
    text(clock[0] == '0' ? String(clock + 1) : String(clock), 14, 10, &FreeSansBold12pt7b, C_TEXT);
    text(date, 330, 12, &FreeSans9pt7b, C_MUTED, TR_DATUM);
  }
  // Connection dot: green = Home Assistant answering, red = not
  uint16_t dot = !haConfigured() ? C_AMBER : (haReachable ? C_GREEN : C_RED);
  tft.fillCircle(W - 18, 20, 6, dot);
  text(PAGE_NAMES[page], W - 32, 12, &FreeSans9pt7b, C_MUTED, TR_DATUM);
  tft.drawFastHLine(0, HEADER_H - 1, W, C_LINE);
}

void drawTabs() {
  int tw = W / PG_COUNT;
  for (int i = 0; i < PG_COUNT; i++) {
    bool active = i == page;
    tft.fillRect(i * tw, TABS_Y, tw, H - TABS_Y, active ? C_ACCENT : C_PANEL);
    tft.drawFastVLine(i * tw, TABS_Y, H - TABS_Y, C_BG);
    text(PAGE_NAMES[i], i * tw + tw / 2, TABS_Y + (H - TABS_Y) / 2, &FreeSansBold12pt7b,
         active ? C_BG : C_TEXT, MC_DATUM);
  }
}

void clearContent() { tft.fillRect(0, HEADER_H, W, TABS_Y - HEADER_H, C_BG); }

void drawNoToken() {
  text("Home Assistant isn't set up yet", W / 2, 120, &FreeSansBold12pt7b, C_AMBER, MC_DATUM);
  text("Add your access token to secrets.h", W / 2, 160, &FreeSans12pt7b, C_MUTED, MC_DATUM);
  text("(the picture frame works without it)", W / 2, 195, &FreeSans9pt7b, C_MUTED, MC_DATUM);
}

// ---------- Pages ----------

void drawWeather() {
  // Big temperature
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(TL_DATUM);
  String t = rounded(EN_TEMP);
  int tw;
  if (ents[EN_TEMP].ok) {
    tw = tft.drawString(t, 18, 56, 8);            // font 8: 75 px digits
    tft.drawCircle(18 + tw + 10, 64, 7, C_TEXT);  // degree sign
    tft.drawCircle(18 + tw + 10, 64, 6, C_TEXT);
  } else {
    text("--", 18, 70, &FreeSansBold24pt7b, C_TEXT);
  }
  bool metric = strcmp(ents[EN_TEMP].unit, "°C") == 0;
  float feels = metric ? num(EN_TEMP) : feelsLikeF(num(EN_TEMP), num(EN_HUM));
  text("Feels " + (ents[EN_TEMP].ok ? String((int)lroundf(feels)) + "°" : String("--")),
       20, 150, &FreeSans12pt7b, C_MUTED);
  text("Humidity " + rounded(EN_HUM) + "%", 20, 182, &FreeSans12pt7b, C_MUTED);

  // Wind and rain column
  const int x = 262;
  tft.drawFastVLine(x - 16, 56, 170, C_LINE);
  text("WIND", x, 54, &FreeSans9pt7b, C_MUTED);
  text(rounded(EN_WIND) + " " + (ents[EN_WIND].unit[0] ? ents[EN_WIND].unit : "mph"),
       x, 74, &FreeSansBold18pt7b, C_TEXT);
  String gust = "Gust " + rounded(EN_GUST);
  if (ents[EN_DIR].ok) gust += "  from " + compass(num(EN_DIR));
  text(gust, x, 114, &FreeSans12pt7b, C_MUTED);
  text("RAIN TODAY", x, 154, &FreeSans9pt7b, C_MUTED);
  String rain = ents[EN_RAIN].ok ? String(num(EN_RAIN), 2) : String("--");
  text(rain + " " + (ents[EN_RAIN].unit[0] ? ents[EN_RAIN].unit : "in"), x, 174,
       &FreeSansBold18pt7b, C_TEXT);

  // Conditions from the National Weather Service
  if (ents[EN_WEATHER].ok)
    text(weatherText(ents[EN_WEATHER].state), 20, 236, &FreeSansBold12pt7b, C_ACCENT);
}

void drawDriveway() {
  bool motion = isOn(EN_DRIVEWAY);
  uint16_t box = !ents[EN_DRIVEWAY].ok ? C_PANEL : (motion ? C_RED : C_GREEN);
  tft.fillRoundRect(16, 52, W - 32, 80, 12, box);
  const char* label = !ents[EN_DRIVEWAY].ok ? "Driveway: no signal"
                      : motion ? "MOTION IN DRIVEWAY" : "Driveway clear";
  text(label, W / 2, 92, &FreeSansBold18pt7b, C_TEXT, MC_DATUM);
  text("Last motion: " + (ents[EN_DRIVEWAY].ok ? relativeTime(ents[EN_DRIVEWAY].changed) : String("--")),
       20, 146, &FreeSans12pt7b, C_MUTED);

  if (isOn(EN_LNEAR)) {
    text("Lightning " + rounded(EN_LMI) + " mi away!", 20, 186, &FreeSansBold18pt7b, C_RED);
  } else {
    text(ents[EN_LNEAR].ok ? "Lightning: none nearby" : "Lightning: --", 20, 186,
         &FreeSansBold18pt7b, C_TEXT);
  }
  int strikes = ents[EN_STRIKES].ok ? (int)num(EN_STRIKES) : 0;
  if (strikes > 0)
    text(String(strikes) + (strikes == 1 ? " strike" : " strikes") + " in the last hour",
         20, 232, &FreeSans12pt7b, C_AMBER);
}

// Control buttons: 3 across, 2 down.
const int BTN_W = 148, BTN_H = 104;
int btnX(int i) { return 10 + (i % 3) * (BTN_W + 8); }
int btnY(int i) { return 52 + (i / 3) * (BTN_H + 8); }

void drawButton(int i) {
  Entity& e = ents[EN_CTRL0 + i];
  bool on = isOn(EN_CTRL0 + i);
  int x = btnX(i), y = btnY(i);
  uint16_t fill = on ? C_AMBER : C_PANEL;
  uint16_t fg = on ? C_BG : C_TEXT;
  tft.fillRoundRect(x, y, BTN_W, BTN_H, 12, fill);
  if (!e.ok) tft.drawRoundRect(x, y, BTN_W, BTN_H, 12, C_LINE);
  text(CONTROLS[i].label, x + BTN_W / 2, y + BTN_H / 2 - 12, &FreeSansBold12pt7b, fg, MC_DATUM);
  text(!e.ok ? "--" : (on ? "ON" : "OFF"), x + BTN_W / 2, y + BTN_H / 2 + 20,
       &FreeSans9pt7b, on ? C_BG : C_MUTED, MC_DATUM);
}

void drawControls() {
  for (int i = 0; i < 6; i++) drawButton(i);
}

void drawContent() {
  clearContent();
  if (!haConfigured() && page != PG_CONTROLS) { drawNoToken(); return; }
  switch (page) {
    case PG_WEATHER:  drawWeather(); break;
    case PG_DRIVEWAY: drawDriveway(); break;
    case PG_CONTROLS: haConfigured() ? drawControls() : drawNoToken(); break;
    default: break;
  }
}

void drawDashboard() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawContent();
  drawTabs();
}

// Entities shown on each page, so a change only redraws when it's visible.
bool onPage(int i, Page p) {
  switch (p) {
    case PG_WEATHER:  return i <= EN_WEATHER;
    case PG_DRIVEWAY: return i >= EN_DRIVEWAY && i <= EN_STRIKES;
    case PG_CONTROLS: return i >= EN_CTRL0;
    default: return false;
  }
}

// ---------- Picture frame ----------

std::vector<String> photos;
size_t photoIndex = 0;
bool sdOk = false;

bool isJpeg(const String& n) {
  String l = n;
  l.toLowerCase();
  return (l.endsWith(".jpg") || l.endsWith(".jpeg")) && !l.startsWith("._");
}

// Lists the photos and shuffles them, so each round is a different order.
void scanPhotos() {
  photos.clear();
  if (!sdOk) return;
  File dir = SD.open(PHOTO_FOLDER);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f && photos.size() < 1500; f = dir.openNextFile()) {
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (!f.isDirectory() && isJpeg(name)) photos.push_back(String(PHOTO_FOLDER) + "/" + name);
    f.close();
  }
  dir.close();
  for (size_t i = photos.size(); i > 1; i--) std::swap(photos[i - 1], photos[random(i)]);
  photoIndex = 0;
  Serial.printf("%u photos on the SD card\n", (unsigned)photos.size());
}

bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= H) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

// Small clock and temperature in the corner of each photo.
void drawPhotoOverlay() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  char clock[12];
  strftime(clock, sizeof(clock), "%I:%M", &t);
  String s = clock[0] == '0' ? String(clock + 1) : String(clock);
  if (ents[EN_TEMP].ok) s += "   " + rounded(EN_TEMP) + "°";
  tft.setFreeFont(&FreeSansBold12pt7b);
  int w = tft.textWidth(s) + 24;
  tft.fillRoundRect(W - w - 8, H - 42, w, 34, 10, C_BG);
  text(s, W - w / 2 - 8, H - 25, &FreeSansBold12pt7b, C_TEXT, MC_DATUM);
}

void showNextPhoto() {
  for (int tries = 0; tries < 5 && !photos.empty(); tries++) {
    if (photoIndex >= photos.size()) {
      scanPhotos();
      if (photos.empty()) break;
    }
    const String& path = photos[photoIndex++];
    uint16_t w = 0, h = 0;
    if (TJpgDec.getSdJpgSize(&w, &h, path) != JDR_OK || w == 0) continue;
    // Shrink big photos by 2, 4 or 8 so they fit (prepare_photos.py does it better).
    uint8_t scale = 1;
    while (scale < 8 && (w / scale > W || h / scale > H)) scale *= 2;
    TJpgDec.setJpgScale(scale);
    int dw = w / scale, dh = h / scale;
    tft.fillScreen(TFT_BLACK);
    TJpgDec.drawSdJpg((W - dw) / 2, (H - dh) / 2, path);
    drawPhotoOverlay();
    return;
  }
  // No photos: a big clock instead.
  tft.fillScreen(TFT_BLACK);
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char clock[8];
    strftime(clock, sizeof(clock), "%I:%M", &t);
    tft.setTextColor(C_MUTED);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(clock[0] == '0' ? clock + 1 : clock, W / 2, H / 2 - 10, 8);
  }
  text(sdOk ? "Put JPEG photos in /photos on the SD card" : "No SD card", W / 2, H - 30,
       &FreeSans9pt7b, C_LINE, MC_DATUM);
}

// ---------- Touch ----------

Preferences prefs;

void calibrateTouch() {
  uint16_t cal[5];
  tft.fillScreen(C_BG);
  text("Touch calibration", W / 2, 110, &FreeSansBold18pt7b, C_TEXT, MC_DATUM);
  text("Tap each corner arrow as it appears", W / 2, 160, &FreeSans12pt7b, C_MUTED, MC_DATUM);
  delay(2000);
  tft.fillScreen(C_BG);
  tft.calibrateTouch(cal, C_ACCENT, C_BG, 20);
  prefs.putBytes("cal", cal, sizeof(cal));
  tft.fillScreen(C_BG);
  text("Saved", W / 2, H / 2, &FreeSansBold18pt7b, C_GREEN, MC_DATUM);
  delay(800);
}

void setupTouch() {
  uint16_t cal[5];
  // Hold a finger on the screen while it starts up to calibrate again.
  uint16_t z = tft.getTouchRawZ();
  if (prefs.getBytes("cal", cal, sizeof(cal)) != sizeof(cal) || z > 600) {
    calibrateTouch();
  } else {
    tft.setTouch(cal);
  }
}

// ---------- Modes ----------

enum Mode { DASHBOARD, FRAME };
Mode mode = DASHBOARD;
uint32_t lastTouch = 0;
uint32_t lastPhoto = 0;
bool touchHeld = false;

void wake(Page p) {
  mode = DASHBOARD;
  page = p;
  lastTouch = millis();
  drawDashboard();
}

void startFrame() {
  mode = FRAME;
  if (photos.empty()) scanPhotos();
  showNextPhoto();
  lastPhoto = millis();
}

void handleTap(int x, int y) {
  lastTouch = millis();
  if (mode == FRAME) { wake(page); return; }

  if (y >= TABS_Y) {
    Page p = (Page)constrain(x / (W / PG_COUNT), 0, PG_COUNT - 1);
    if (p != page) { page = p; drawDashboard(); }
    return;
  }
  if (page == PG_CONTROLS && haConfigured()) {
    for (int i = 0; i < 6; i++) {
      if (x >= btnX(i) && x < btnX(i) + BTN_W && y >= btnY(i) && y < btnY(i) + BTN_H) {
        Entity& e = ents[EN_CTRL0 + i];
        // Flash the button, send the toggle, then read the real state back.
        tft.drawRoundRect(btnX(i), btnY(i), BTN_W, BTN_H, 12, C_ACCENT);
        tft.drawRoundRect(btnX(i) + 1, btnY(i) + 1, BTN_W - 2, BTN_H - 2, 12, C_ACCENT);
        if (haToggle(e.id)) {
          delay(250);
          haFetch(e);
        }
        drawButton(i);
        return;
      }
    }
  }
}

void pollTouch() {
  uint16_t x, y;
  bool pressed = tft.getTouch(&x, &y, 450);
  if (pressed && !touchHeld) handleTap(x, y);
  if (pressed) lastTouch = millis();
  touchHeld = pressed;
}

// ---------- Polling Home Assistant ----------

int pollNext = 0;
uint32_t lastPoll = 0, lastDrivewayPoll = 0;

void pollHa() {
  if (!haConfigured()) return;
  uint32_t now = millis();

  // The driveway alarm is checked every 2 seconds so the remote reacts quickly.
  if (now - lastDrivewayPoll > 2000) {
    lastDrivewayPoll = now;
    bool was = isOn(EN_DRIVEWAY);
    bool changed = haFetch(ents[EN_DRIVEWAY]);
    if (!was && isOn(EN_DRIVEWAY)) {
      Serial.println("Driveway alarm!");
      wake(PG_DRIVEWAY);
      return;
    }
    if (changed && mode == DASHBOARD && page == PG_DRIVEWAY) drawContent();
  }

  // Everything else, one entity every 400 ms (each is refreshed about every 7 s).
  if (now - lastPoll > 400) {
    lastPoll = now;
    int i = pollNext;
    pollNext = (pollNext + 1) % EN_COUNT;
    if (i == EN_DRIVEWAY) return;
    bool wasReachable = haReachable;
    bool changed = haFetch(ents[i]);
    if (mode != DASHBOARD) return;
    if (changed && onPage(i, page)) {
      if (page == PG_CONTROLS) drawButton(i - EN_CTRL0);
      else drawContent();
    }
    if (wasReachable != haReachable) drawHeader();
  }
}

// ---------- Main ----------

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("ha-remote");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
}

void setup() {
  Serial.begin(115200);

  ledcSetup(BACKLIGHT_CH, 5000, 8);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CH);
  setBacklight(BRIGHTNESS);

  // SD and touch share the bus with the display: keep their chip selects high.
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  tft.init();
  tft.setRotation(1);  // landscape
  setupColors();
  tft.fillScreen(C_BG);
  text("Starting up...", W / 2, H / 2, &FreeSansBold12pt7b, C_MUTED, MC_DATUM);

  prefs.begin("remote");
  setupTouch();

  sdOk = SD.begin(SD_CS, SPI, 20000000);
  Serial.println(sdOk ? "SD card found" : "No SD card");
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tftOutput);
  randomSeed(esp_random());
  scanPhotos();

  for (int i = 0; i < EN_COUNT; i++) ents[i].id = "";
  ents[EN_TEMP].id = E_TEMPERATURE;
  ents[EN_HUM].id = E_HUMIDITY;
  ents[EN_WIND].id = E_WIND;
  ents[EN_GUST].id = E_GUST;
  ents[EN_DIR].id = E_WIND_DIR;
  ents[EN_RAIN].id = E_RAIN_TODAY;
  ents[EN_WEATHER].id = E_WEATHER;
  ents[EN_DRIVEWAY].id = E_DRIVEWAY;
  ents[EN_LNEAR].id = E_LIGHTNING_NEAR;
  ents[EN_LMI].id = E_LIGHTNING_MI;
  ents[EN_STRIKES].id = E_STRIKES_HOUR;
  for (int i = 0; i < 6; i++) ents[EN_CTRL0 + i].id = CONTROLS[i].entity;

  tft.fillScreen(C_BG);
  text("Connecting to Wi-Fi", W / 2, H / 2, &FreeSansBold12pt7b, C_MUTED, MC_DATUM);
  connectWiFi();
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    MDNS.begin("ha-remote");
    if (haConfigured()) resolveHa();
  }

  // Read everything once before showing the dashboard.
  for (int i = 0; i < EN_COUNT; i++) haFetch(ents[i]);
  wake(PG_WEATHER);
}

void loop() {
  pollTouch();
  pollHa();

  uint32_t now = millis();
  if (mode == DASHBOARD) {
    static int lastMinute = -1;
    struct tm t;
    if (getLocalTime(&t, 0) && t.tm_min != lastMinute) {
      lastMinute = t.tm_min;
      drawHeader();
      if (page == PG_DRIVEWAY) drawContent();  // "Last motion: 5 min ago"
    }
    if (now - lastTouch > SCREENSAVER_AFTER_S * 1000UL) startFrame();
  } else if (now - lastPhoto > PHOTO_SECONDS * 1000UL) {
    showNextPhoto();
    lastPhoto = millis();
  }
  delay(10);
}
