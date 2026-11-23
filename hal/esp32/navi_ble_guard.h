#ifndef NAVI_BLE_GUARD_H
#define NAVI_BLE_GUARD_H

/* Bluetooth watch: logs connect/disconnect events with their reason on the serial port
   ("[BLE] ..." lines, plus a summary once a minute), and restarts advertising when the
   board has gone a long time without a connection. See navi_ble_guard.cpp. */

void bleGuardBegin(void);              // after every watch.begin()
void bleGuardRx(void);                 // a packet arrived from the phone
void bleGuardLoop(bool bleStopped);    // hal_loop; bleStopped = BLE switched off on purpose

#endif
