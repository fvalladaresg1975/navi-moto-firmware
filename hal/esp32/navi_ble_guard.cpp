/* Bluetooth watch (v2.7).

   On 2026-11-23 the 1.75C kept advertising but refused every connection from the phone
   (the phone logged CONNECTION_FAILED_ESTABLISHMENT / GATT 133) until it was power-cycled
   with PWR. Nothing on the serial port said why. This module:

   1. Logs every connect / disconnect / end of advertising with its NimBLE code, from a
      GAP event listener (NimBLEDevice::setCustomGapHandler), so the vendored ChronosESP32
      library stays untouched. The listener runs in the NimBLE host task, so it only
      queues the events; hal_loop prints them.
   2. Once a minute prints "[BLE] conn=.. adv=.. rx=..s ..." next to [HEAP] / [POWER].
   3. With no connection: if advertising is off for 10 s it is started again, and every
      5 minutes without a connection advertising is stopped and started. Both are the
      library's normal calls and cost microseconds; the board advertises anyway while it
      waits for the phone.
   4. Connected but nothing received for 30 minutes (the app sends the time every 10):
      only logged, as a possible "ghost" link - no action until a log shows it is real.

   Nothing here deinitialises BLE or restarts the board. */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "navi_ble_guard.h"

static const uint32_t CHECK_MS = 10000UL;
static const uint32_t ADV_OFF_GRACE_MS = 10000UL;
static const uint32_t ADV_RESTART_MS = 5UL * 60UL * 1000UL;
static const uint32_t SILENT_WARN_MS = 30UL * 60UL * 1000UL;
static const uint32_t SUMMARY_MS = 60000UL;

enum : uint8_t { EV_CONNECT = 1, EV_LINK = 2, EV_DISCONNECT = 3, EV_ADV_END = 4 };

struct BleEvent
{
  uint8_t type;
  int code;
  uint16_t handle;
  uint32_t ms;
};

static portMUX_TYPE evMux = portMUX_INITIALIZER_UNLOCKED;
static BleEvent evRing[8];
static uint8_t evHead = 0, evCount = 0;
static uint32_t evDropped = 0;

static volatile uint32_t lastRxMs = 0;
static uint32_t connectsOk = 0, connectsFailed = 0, disconnects = 0;
static int lastReason = 0;
static uint32_t advStarts = 0, advRestarts = 0;

static void pushEvent(uint8_t type, int code, uint16_t handle)
{
  portENTER_CRITICAL(&evMux);
  if (evCount < sizeof(evRing) / sizeof(evRing[0]))
  {
    evRing[(evHead + evCount) % (sizeof(evRing) / sizeof(evRing[0]))] = {type, code, handle, millis()};
    evCount++;
  }
  else
  {
    evDropped++;
  }
  portEXIT_CRITICAL(&evMux);
}

/* NimBLE host task. Must return 0 and stay short. */
static int gapListener(ble_gap_event *e, void *)
{
  switch (e->type)
  {
  case BLE_GAP_EVENT_CONNECT:
    pushEvent(EV_CONNECT, e->connect.status, e->connect.conn_handle);
    break;
  case BLE_GAP_EVENT_LINK_ESTAB:
    pushEvent(EV_LINK, e->link_estab.status, e->link_estab.conn_handle);
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    pushEvent(EV_DISCONNECT, e->disconnect.reason, e->disconnect.conn.conn_handle);
    break;
  case BLE_GAP_EVENT_ADV_COMPLETE:
    pushEvent(EV_ADV_END, e->adv_complete.reason, 0);
    break;
  default:
    break;
  }
  return 0;
}

/* NimBLE codes as the phone-side logs name them. 0x2xx = HCI error 0xxx. */
static const char *reasonName(int code)
{
  switch (code)
  {
  case 0:
    return "ok";
  case 0x208:
    return "supervision timeout";
  case 0x213:
    return "phone closed it";
  case 0x216:
    return "board closed it";
  case 0x222:
    return "LL response timeout";
  case 0x23B:
    return "unacceptable conn params";
  case 0x23E:
    return "failed to establish";
  default:
    return "";
  }
}

void bleGuardBegin(void)
{
  // A NimBLE re-init (watch.begin() after watch.stop()) empties the listener list, so
  // this is called after every begin(); registering twice is harmless (EALREADY).
  if (!NimBLEDevice::setCustomGapHandler(gapListener))
  {
    Serial.println("[BLE] could not register the event listener");
  }
}

void bleGuardRx(void)
{
  lastRxMs = millis();
}

static void printEvents(void)
{
  for (;;)
  {
    BleEvent ev;
    uint32_t dropped;
    portENTER_CRITICAL(&evMux);
    if (evCount == 0)
    {
      portEXIT_CRITICAL(&evMux);
      break;
    }
    ev = evRing[evHead];
    evHead = (evHead + 1) % (sizeof(evRing) / sizeof(evRing[0]));
    evCount--;
    dropped = evDropped;
    evDropped = 0;
    portEXIT_CRITICAL(&evMux);

    if (dropped)
    {
      Serial.printf("[BLE] %lu event(s) not logged (queue full)\n", (unsigned long)dropped);
    }
    switch (ev.type)
    {
    case EV_CONNECT:
      if (ev.code == 0)
      {
        connectsOk++;
        Serial.printf("[BLE] connect h=%u\n", ev.handle);
      }
      else
      {
        connectsFailed++;
        Serial.printf("[BLE] connect failed status=0x%X %s\n", ev.code, reasonName(ev.code));
      }
      break;
    case EV_LINK:
      if (ev.code != 0)
      {
        connectsFailed++;
        Serial.printf("[BLE] link failed h=%u status=0x%X %s\n", ev.handle, ev.code, reasonName(ev.code));
      }
      break;
    case EV_DISCONNECT:
      disconnects++;
      lastReason = ev.code;
      Serial.printf("[BLE] disconnect h=%u reason=0x%X %s\n", ev.handle, ev.code, reasonName(ev.code));
      break;
    case EV_ADV_END:
      // 0 = a phone connected; anything else means advertising stopped on its own.
      if (ev.code != 0)
      {
        Serial.printf("[BLE] advertising ended reason=0x%X\n", ev.code);
      }
      break;
    }
  }
}

void bleGuardLoop(bool bleStopped)
{
  printEvents();

  static uint32_t lastCheck = 0;
  static uint32_t lastSummary = 0;
  static uint32_t disconnectedSince = 0;
  static uint32_t advOffSince = 0;
  static uint32_t lastAdvRestart = 0;
  static bool silentWarned = false;

  uint32_t now = millis();
  if (now - lastCheck < CHECK_MS)
  {
    return;
  }
  lastCheck = now;

  NimBLEServer *srv = NimBLEDevice::isInitialized() ? NimBLEDevice::getServer() : nullptr;
  if (bleStopped || srv == nullptr)
  {
    // BLE switched off on purpose (long inactivity on battery): nothing to watch.
    disconnectedSince = 0;
    advOffSince = 0;
    return;
  }

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  uint8_t conns = srv->getConnectedCount();
  bool advOn = adv->isAdvertising();
  uint32_t rx = lastRxMs;

  if (conns > 0)
  {
    disconnectedSince = 0;
    advOffSince = 0;
    if (rx != 0 && now - rx >= SILENT_WARN_MS)
    {
      if (!silentWarned)
      {
        Serial.printf("[BLE] connected (%u) but nothing received for %lu min - possible ghost link\n",
                      conns, (unsigned long)((now - rx) / 60000UL));
        silentWarned = true;
      }
    }
    else
    {
      silentWarned = false;
    }
  }
  else
  {
    if (disconnectedSince == 0)
    {
      disconnectedSince = now;
      lastAdvRestart = now;
    }
    if (!advOn)
    {
      if (advOffSince == 0)
      {
        advOffSince = now;
      }
      else if (now - advOffSince >= ADV_OFF_GRACE_MS)
      {
        bool ok = adv->start();
        advStarts++;
        advOffSince = 0;
        lastAdvRestart = now;
        Serial.printf("[BLE] no connection and advertising was off - started (%s)\n", ok ? "ok" : "failed");
      }
    }
    else
    {
      advOffSince = 0;
      if (now - lastAdvRestart >= ADV_RESTART_MS)
      {
        adv->stop();
        bool ok = adv->start();
        advRestarts++;
        lastAdvRestart = now;
        Serial.printf("[BLE] no connection for %lu min - advertising restarted (%s)\n",
                      (unsigned long)((now - disconnectedSince) / 60000UL), ok ? "ok" : "failed");
      }
    }
  }

  if (now - lastSummary >= SUMMARY_MS)
  {
    lastSummary = now;
    Serial.printf("[BLE] conn=%u adv=%d rx=%lds connects=%lu failed=%lu disc=%lu lastReason=0x%X advStarts=%lu advRestarts=%lu\n",
                  conns, advOn ? 1 : 0, rx ? (long)((now - rx) / 1000UL) : -1L,
                  (unsigned long)connectsOk, (unsigned long)connectsFailed, (unsigned long)disconnects,
                  lastReason, (unsigned long)advStarts, (unsigned long)advRestarts);
  }
}
