#include "DisplayCustomisation.h"

CalendarDisplay display(GxEPD2_420(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));

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

  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

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

static int16_t textWidth(CalendarDisplay& display, const String& s, const GFXfont* font) {
  display.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

static int wrapText(CalendarDisplay& display, const String& text, String* outLines, int maxLines, int maxWidth, const GFXfont* font) {
  String t = text;
  t.trim();
  if (t.length() == 0) return 0;

  int lineCount = 0;
  while (t.length() > 0 && lineCount < maxLines) {
    if (textWidth(display, t, font) <= maxWidth) {
      outLines[lineCount++] = t;
      break;
    }

    int breakPos = -1;
    for (int i = 0; i < (int)t.length(); i++) {
      if (t[i] == ' ') {
        String candidate = t.substring(0, i);
        candidate.trim();
        if (textWidth(display, candidate, font) <= maxWidth) {
          breakPos = i;
        } else {
          break;
        }
      }
    }

    if (breakPos < 0) {
      int cut = t.length();
      while (cut > 1 && textWidth(display, t.substring(0, cut), font) > maxWidth) cut--;
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

static void drawHeader(CalendarDisplay& display) {
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSans12pt7b);

  String title = "Family Calendar";
  int16_t w = textWidth(display, title, &FreeSans12pt7b);
  int16_t x = (display.width() - w) / 2;
  display.setCursor(x, 28);
  display.print(title);

  display.drawLine(10, 40, display.width() - 10, 40, GxEPD_BLACK);
}

static void drawFooter(CalendarDisplay& display, time_t nowEpoch, time_t lastSuccessEpoch, uint32_t updateIntervalMin) {
  int16_t y = display.height() - 10;
  display.setFont(&FreeSans9pt7b);

  String updated = "Updated: " + formatDateTimeFooter(lastSuccessEpoch);
  time_t nextEpoch = nowEpoch + (updateIntervalMin * 60);
  String nextu = "Next: " + formatDateTimeFooter(nextEpoch);

  display.setTextColor(GxEPD_BLACK);
  display.setCursor(10, y);
  display.print(updated);

  int16_t w = textWidth(display, nextu, &FreeSans9pt7b);
  display.setCursor(display.width() - 10 - w, y);
  display.print(nextu);
}

static void drawEventsList(CalendarDisplay& display, const EventItem* events, int eventCount) {
  const int16_t topY = 50;
  const int16_t bottomY = display.height() - 30;
  const int16_t leftX = 12;
  const int16_t rightX = display.width() - 12;
  const int16_t maxW = rightX - leftX;

  int16_t y = topY;

  for (int i = 0; i < eventCount; i++) {
    if (y > bottomY) break;

    String timeStr = formatTimeHM(events[i].startEpoch);
    display.setFont(&FreeSans18pt7b);
    int16_t tw = textWidth(display, timeStr, &FreeSans18pt7b);
    int16_t tx = (display.width() - tw) / 2;
    y += 26;
    display.setCursor(tx, y);
    display.print(timeStr);

    display.setFont(&FreeSans12pt7b);
    String lines[2];
    int n = wrapText(display, String(events[i].title), lines, 2, maxW, &FreeSans12pt7b);

    for (int li = 0; li < n; li++) {
      y += 22;
      int16_t lw = textWidth(display, lines[li], &FreeSans12pt7b);
      int16_t lx = (display.width() - lw) / 2;
      display.setCursor(lx, y);
      display.print(lines[li]);
    }

    y += 12;
    display.drawLine(leftX, y, rightX, y, GxEPD_BLACK);
    y += 10;
  }
}

void renderFullScreen(
  CalendarDisplay& display,
  time_t nowEpoch,
  time_t lastSuccessEpoch,
  uint32_t updateIntervalMin,
  const EventItem* events,
  int eventCount
) {
  display.setRotation(1);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHeader(display);
    drawEventsList(display, events, eventCount);
    drawFooter(display, nowEpoch, lastSuccessEpoch, updateIntervalMin);
  } while (display.nextPage());
}
