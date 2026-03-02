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

// ---------------------------- Display utilities ---------------------------
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
