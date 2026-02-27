/*
  Family Calendar V2 (Known-good baseline)
  - ESP32-S3 + 4.2" 400x300 B/W ePaper (GxEPD2 style)
  - wake -> fetch -> render -> deep sleep
  - footer: Updated + Next update times
  - per-event: centered time line + subject (up to 2 lines), no location
  - stale-data safe: on fetch failure, DO NOT clear screen; keep last image
  - RTC memory tracks last successful update epoch

  Libraries:
    - GxEPD2
    - Adafruit GFX (comes with GxEPD2 usage)
    - ArduinoJson (7.x)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "secrets.h"

// ------------ Display: set this to your current 4.2" B/W panel -------------
// You MUST set the correct panel include + constructor for your hardware.
// Common example for 4.2" 400x300 b/w:
//   GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(GxEPD2_420(CS, DC, RST, BUSY));
//
// If yours differs, swap to match your working build.
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

// Example wiring pins (update to match your current working wiring)
static const int PIN_CS   = 10;
static const int PIN_DC   = 9;
static const int PIN_RST  = 8;
static const int PIN_BUSY = 7;

// 4.2" 400x300 BW
GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(GxEPD2_420(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));

// ---------------------------- Behavior knobs ------------------------------
static const uint32_t UPDATE_INTERVAL_MIN = 60; // periodic refresh interval
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const uint32_t HTTP_TIMEOUT_MS = 15000;

static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.nist.gov";

// RTC memory: persists across deep sleep
RTC_DATA_ATTR time_t rtc_last_success_epoch = 0;

// ---------------------------- Event model --------------------------------
struct EventItem {
  time_t startEpoch = 0;
  char title[128] = {0};
};

static const int MAX_EVENTS = 20;
EventItem events[MAX_EVENTS];
int eventCount = 0;

// ---------------------------- Utilities ----------------------------------
static String twoDigits(int v) {
  if (v < 10) return "0" + String(v);
  return String(v);
}

static String formatTimeHM(time_t t) {
  struct tm tmv;
  localtime_r(&t, &tmv);
  return twoDigits(tmv.tm_hour) + ":" + twoDigits(tmv.tm_min);
}

static String formatDateTimeFooter(time_t t) {
  if (t == 0) return String("--:--");
  struct tm tmv;
  localtime_r(&t, &tmv);
  // e.g. 26 Feb 14:05
  static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  String s;
  s += String(tmv.tm_mday);
  s += " ";
  s += months[tmv.tm_mon];
  s += " ";
  s += twoDigits(tmv.tm_hour);
  s += ":";
  s += twoDigits(tmv.tm_min);
  return s;
}

// measure text width for centering
static int16_t textWidth(const String& s, const GFXfont* font) {
  display.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

// Wrap a string into up to maxLines lines within maxWidth pixels.
// Very simple greedy wrapping on spaces.
static int wrapText(const String& text, String* outLines, int maxLines, int maxWidth, const GFXfont* font) {
  String t = text;
  t.trim();
  if (t.length() == 0) return 0;

  int lineCount = 0;
  while (t.length() > 0 && lineCount < maxLines) {
    // if whole fits, take it
    if (textWidth(t, font) <= maxWidth) {
      outLines[lineCount++] = t;
      break;
    }

    // otherwise find break point
    int breakPos = -1;
    for (int i = 0; i < (int)t.length(); i++) {
      if (t[i] == ' ') {
        String candidate = t.substring(0, i);
        candidate.trim();
        if (textWidth(candidate, font) <= maxWidth) {
          breakPos = i;
        } else {
          break;
        }
      }
    }

    if (breakPos < 0) {
      // single long word; hard cut
      int cut = t.length();
      while (cut > 1 && textWidth(t.substring(0, cut), font) > maxWidth) cut--;
      outLines[lineCount++] = t.substring(0, cut);
      t = t.substring(cut);
      t.trim();
    } else {
      outLines[lineCount++] = t.substring(0, breakPos);
      t = t.substring(breakPos + 1);
      t.trim();
    }
  }
  return lineCount;
}

// ------------------------ Networking / Time ------------------------------
static bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }
  return (WiFi.status() == WL_CONNECTED);
}

static bool syncTimeNTP() {
  configTime(TZ_OFFSET_SEC, 0, NTP1, NTP2);
  // Wait briefly for time
  for (int i = 0; i < 20; i++) {
    time_t now = time(nullptr);
    if (now > 1700000000) return true; // sanity threshold (approx 2023+)
    delay(250);
  }
  return false;
}

static int parseEventsJson(const String& json) {
  // Expected JSON shape (example):
  // { "events": [ {"start": 1700000000, "title":"..."}, ... ] }
  // or a bare array: [ {"start":..., "title":"..."} ]
  StaticJsonDocument<20 * 1024> doc; // adjust if needed
  DeserializationError err = deserializeJson(doc, json);
  if (err) return -1;

  eventCount = 0;

  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
      if (eventCount >= MAX_EVENTS) break;
      events[eventCount].startEpoch = (time_t)(o["start"] | 0);
      const char* t = o["title"] | "";
      strlcpy(events[eventCount].title, t, sizeof(events[eventCount].title));
      eventCount++;
    }
  } else if (doc.is<JsonObject>()) {
    JsonArray arr = doc["events"].as<JsonArray>();
    for (JsonObject o : arr) {
      if (eventCount >= MAX_EVENTS) break;
      events[eventCount].startEpoch = (time_t)(o["start"] | 0);
      const char* t = o["title"] | "";
      strlcpy(events[eventCount].title, t, sizeof(events[eventCount].title));
      eventCount++;
    }
  } else {
    return -2;
  }

  return eventCount;
}

static bool fetchEvents(String& outJson) {
  WiFiClientSecure client;
  client.setInsecure(); // simplest for Apps Script endpoints; tighten later if desired

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, EVENTS_URL)) return false;

  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }

  outJson = http.getString();
  http.end();
  return true;
}

// ---------------------------- Rendering ----------------------------------
static void drawHeader() {
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSans12pt7b);
  String title = "Family Calendar";
  int16_t w = textWidth(title, &FreeSans12pt7b);
  int16_t x = (display.width() - w) / 2;
  display.setCursor(x, 28);
  display.print(title);

  // separator line
  display.drawLine(10, 40, display.width() - 10, 40, GxEPD_BLACK);
}

static void drawFooter(time_t nowEpoch) {
  int16_t y = display.height() - 10;
  display.setFont(&FreeSans9pt7b);

  String updated = "Updated: " + formatDateTimeFooter(rtc_last_success_epoch);
  time_t nextEpoch = nowEpoch + (UPDATE_INTERVAL_MIN * 60);
  String nextu = "Next: " + formatDateTimeFooter(nextEpoch);

  display.setTextColor(GxEPD_BLACK);
  display.setCursor(10, y);
  display.print(updated);

  int16_t w = textWidth(nextu, &FreeSans9pt7b);
  display.setCursor(display.width() - 10 - w, y);
  display.print(nextu);
}

static void drawEventsList() {
  // Layout region between header line and footer
  const int16_t topY = 50;
  const int16_t bottomY = display.height() - 30;
  const int16_t leftX = 12;
  const int16_t rightX = display.width() - 12;
  const int16_t maxW = rightX - leftX;

  int16_t y = topY;

  for (int i = 0; i < eventCount; i++) {
    if (y > bottomY) break;

    // Time centered, larger font
    String timeStr = formatTimeHM(events[i].startEpoch);
    display.setFont(&FreeSans18pt7b);
    int16_t tw = textWidth(timeStr, &FreeSans18pt7b);
    int16_t tx = (display.width() - tw) / 2;
    y += 26;
    display.setCursor(tx, y);
    display.print(timeStr);

    // Subject up to 2 lines, medium font, centered per line
    display.setFont(&FreeSans12pt7b);
    String lines[2];
    int n = wrapText(String(events[i].title), lines, 2, maxW, &FreeSans12pt7b);

    for (int li = 0; li < n; li++) {
      y += 22;
      int16_t lw = textWidth(lines[li], &FreeSans12pt7b);
      int16_t lx = (display.width() - lw) / 2;
      display.setCursor(lx, y);
      display.print(lines[li]);
    }

    // small gap + divider
    y += 12;
    display.drawLine(leftX, y, rightX, y, GxEPD_BLACK);
    y += 10;
  }
}

static void renderFullScreen(time_t nowEpoch) {
  display.setRotation(1); // adjust for your mounting orientation
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHeader();
    drawEventsList();
    drawFooter(nowEpoch);
  } while (display.nextPage());
}

// ---------------------------- Sleep --------------------------------------
static void goToDeepSleep() {
  uint64_t sleepUs = (uint64_t)UPDATE_INTERVAL_MIN * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}

// -------------------------------- Main -----------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  display.init(115200);

  bool wifiOk = connectWiFi();
  bool timeOk = false;
  time_t nowEpoch = 0;

  if (wifiOk) {
    timeOk = syncTimeNTP();
    nowEpoch = time(nullptr);
  }

  // If time failed, still create a best-effort "now" for footer
  if (!timeOk) {
    nowEpoch = (rtc_last_success_epoch > 0) ? (rtc_last_success_epoch + (UPDATE_INTERVAL_MIN * 60)) : 0;
  }

  String json;
  bool fetched = false;

  if (wifiOk) {
    fetched = fetchEvents(json);
  }

  if (fetched) {
    int parsed = parseEventsJson(json);
    if (parsed >= 0) {
      // Successful update: render and store last success
      rtc_last_success_epoch = time(nullptr);
      renderFullScreen(nowEpoch);
    } else {
      // Parse failed: do nothing (keeps last image)
      Serial.println("Parse failed; keeping last screen.");
    }
  } else {
    // Fetch failed: do nothing (keeps last image)
    Serial.println("Fetch failed; keeping last screen.");
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  goToDeepSleep();
}

void loop() {
  // never reached
}
