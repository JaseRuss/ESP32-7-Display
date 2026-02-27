#pragma once

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "CalendarTypes.h"

static const int PIN_CS = 10;
static const int PIN_DC = 9;
static const int PIN_RST = 8;
static const int PIN_BUSY = 7;

using CalendarDisplay = GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT>;

extern CalendarDisplay display;

void renderFullScreen(
  CalendarDisplay& display,
  time_t nowEpoch,
  time_t lastSuccessEpoch,
  uint32_t updateIntervalMin,
  const EventItem* events,
  int eventCount
);
