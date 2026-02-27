#pragma once

#include <time.h>

struct EventItem {
  time_t startEpoch = 0;
  char title[128] = {0};
};
