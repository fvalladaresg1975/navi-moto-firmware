#ifndef NAVI_BATTERY_H
#define NAVI_BATTERY_H

/* Low battery: warns on the screen, and powers the board off before the battery runs flat.
   See navi_battery.cpp. */

void batteryLoop(void); // hal_loop, LVGL task

#endif
