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
