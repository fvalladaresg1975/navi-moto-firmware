#ifndef NAVI_CRASH_H
#define NAVI_CRASH_H

/* Crash detection with the QMI8658 IMU (0xCC): an impact followed by the bike lying down
   starts a countdown on the screen; if nobody cancels it, the phone texts the rider's
   emergency contacts with the location. See navi_crash.cpp. */

#include <stdint.h>

void crashSetup(void);                                 // hal_setup, once the I2C bus is up
void crashLoop(void);                                  // hal_loop, LVGL task
void crashOnPacket(const uint8_t *data, int length);   // dataCallback, NimBLE task

#endif
