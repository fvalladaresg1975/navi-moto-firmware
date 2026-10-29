#ifndef NAVI_OTA_H
#define NAVI_OTA_H

/* Firmware update over WiFi from the phone app (0xCB). The board opens its own access
   point, the phone joins it and pushes the image over TCP; see navi_ota.cpp. */

#include <stdint.h>

void otaBoot(void);                                  // hal_setup, right after prefs.begin()
void otaLoop(void);                                  // hal_loop, LVGL task
void otaOnPacket(const uint8_t *data, int length);   // dataCallback, NimBLE task
const char *otaBuildDate(void);                      // "2026-10-28 15:20", for "Acerca de"

#endif
