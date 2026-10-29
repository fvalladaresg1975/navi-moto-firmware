/*
   Navi Moto: firmware update over WiFi from the phone app (0xCB).

   Phone -> watch over BLE:
     sub 0x00 start   [ssid length u8][ssid][password length u8][password]
     sub 0x01 cancel
   The board opens that WPA2 access point (the phone makes up a new password each time)
   and listens on OTA_PORT. The phone joins it and sends, big-endian,
     "NMOT" [size u32][MD5 of the image, 16 bytes] and then the image,
   and the board answers one byte (OTA_TCP_*). On success it restarts into the new image.
   An image built for the other board (its "NAVIMOTO-HW" tag) is refused mid-transfer.
   Watch -> phone {AB 00 04 FF CB 80 status}: OTA_* below.

   Why it can't leave the board unusable:
   - partitions_16M.csv has two app slots; the image goes to the one not running, and
     the update library checks its MD5 and the image itself before making it the boot slot.
   - The new firmware only counts as good once the phone has stayed connected to it for
     OTA_VALID_AFTER_MS (otaLoop) - BLE is what the next update needs. Until then any
     restart (a crash, or switching the board off and on) makes the bootloader go back to
     the previous firmware (the Arduino core's bootloader has
     CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE; verifyRollbackLater() below keeps the core from
     approving the new firmware by itself at startup).
   - USB flashing keeps working as always, whatever is in the slots.

   Runs on the LVGL task: the transfer blocks hal_loop() and pumps LVGL itself for the
   progress screen. The BLE task only records the request (otaOnPacket).
*/

#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <Preferences.h>
#include <ChronosESP32.h>
#include <lvgl.h>
#include "esp_ota_ops.h"
#include "navi_ota.h"
#include "ui/ui.h"
#include "common/navi_face.h"

extern ChronosESP32 watch;
extern Preferences prefs;
void screenBrightness(uint8_t value);
void screen_on(long extra);
void screenPowerSync(void); // wakes the panel (and the CPU clock) after screen_on()
int halBatteryPercent(void);  // app_hal.cpp; -1 = unknown
bool halExternalPower(void);  // charger / USB plugged in

#define OTA_PORT 3232
#define OTA_WAIT_PHONE_MS 180000UL // access point up, phone not connected yet
#define OTA_STALL_MS 20000UL       // no bytes from the phone this long mid-transfer
#define OTA_MIN_BATTERY 30         // %, unless charging: a flash write must not brown out
#define OTA_VALID_AFTER_MS 10000UL // connected to the phone this long: the firmware is good
#define OTA_PREF_TARGET "ota_to"   // slot label the last update was written to

enum
{
  OTA_READY = 1,       // access point up, waiting for the phone
  OTA_ERR_BATTERY = 2, // below OTA_MIN_BATTERY and not charging
  OTA_ERR_WIFI = 3,    // the access point would not start
  OTA_ERR_TIMEOUT = 4, // the phone never joined
  OTA_ERR_FAILED = 5,  // transfer or image bad (the TCP answer says which)
  OTA_UPDATED = 6,     // after the restart: running the new firmware
  OTA_ROLLED_BACK = 7, // after the restart: the new one failed, back on the previous
  OTA_CANCELLED = 8
};

enum
{
  OTA_TCP_OK = 0,
  OTA_TCP_BAD_HEADER = 1,
  OTA_TCP_NO_SPACE = 2, // bigger than the slot, or the slot couldn't be prepared
  OTA_TCP_WRITE = 3,    // flash write failed
  OTA_TCP_VERIFY = 4,   // MD5 or image check failed
  OTA_TCP_STALLED = 5,  // the phone stopped sending / disconnected / cancelled
  OTA_TCP_WRONG_BOARD = 6 // built for the other board (otaWrongBoard says which)
};

static char otaSsid[33];
static char otaPass[64];
static volatile bool otaStartPending = false;
static volatile bool otaCancelPending = false;
static volatile bool otaBusy = false;
static volatile bool otaWaiting = false; // access point up, phone not connected yet

static char otaWrongBoard[16]; // board an image was built for, when it isn't this one
static uint8_t otaBootResult = 0; // OTA_UPDATED / OTA_ROLLED_BACK, reported once connected
static bool otaValid = false;
static unsigned long otaConnectedSince = 0;
static unsigned long otaMessageUntil = 0;

extern "C" bool verifyRollbackLater() { return true; }

/* "NAVIMOTO-FW ota1 <date>" and "NAVIMOTO-HW <board>", hal/esp32/navi_build.c - generated
   on every build. */
extern "C" const char navi_build_stamp[];
extern "C" const char navi_board_tag[];
#define OTA_TAG_PREFIX_LEN 12 // "NAVIMOTO-HW "
/* Images from before the board tag (up to v2.2) were all built for the 1.75. */
#define OTA_UNTAGGED_BOARD "1.75"

const char *otaBuildDate(void)
{
  return navi_build_stamp + sizeof("NAVIMOTO-FW ota1 ") - 1;
}

const char *otaBoardName(void)
{
  return navi_board_tag + OTA_TAG_PREFIX_LEN;
}

/* Finds the board tag in the image as it streams in. The prefix is read from
   navi_board_tag itself, so no second copy of it ends up in the image to be found. */
struct OtaTagScan
{
  int matched = 0;  // prefix bytes matched so far
  int len = -1;     // >= 0: copying the board name
  bool done = false;
  char board[16];

  void feed(const uint8_t *p, size_t n)
  {
    for (size_t i = 0; i < n && !done; i++)
    {
      char c = (char)p[i];
      if (len >= 0)
      {
        if (c == 0 || len == (int)sizeof(board) - 1)
        {
          board[len] = 0;
          done = true;
        }
        else
          board[len++] = c;
      }
      else if (c == navi_board_tag[matched])
      {
        if (++matched == OTA_TAG_PREFIX_LEN)
          len = 0;
      }
      else
        matched = c == navi_board_tag[0] ? 1 : 0; // 'N' only starts the prefix
    }
  }
};

static void otaReport(uint8_t status)
{
  uint8_t cmd[] = {0xAB, 0x00, 0x04, 0xFF, 0xCB, 0x80, status};
  watch.sendCommand(cmd, 7);
}

void otaOnPacket(const uint8_t *data, int length)
{
  if (length < 7 || data[0] != 0xAB || data[4] != 0xCB)
    return;
  if (data[5] == 0x01)
  {
    otaCancelPending = true;
    return;
  }
  // A new request while still waiting for the phone (it gave up and retried): start over
  // with the new password. Mid-transfer one is ignored.
  if (data[5] != 0x00 || (otaBusy && !otaWaiting) || otaStartPending)
    return;
  const uint8_t *q = &data[6];
  int n = length - 7; // payload, data[6] .. data[length - 2]
  if (n < 2)
    return;
  int sl = q[0];
  if (sl < 1 || sl > 32 || 2 + sl > n)
    return;
  int pl = q[1 + sl];
  if (pl < 8 || pl > 63 || 2 + sl + pl > n)
    return;
  memcpy(otaSsid, q + 1, sl);
  otaSsid[sl] = 0;
  memcpy(otaPass, q + 2 + sl, pl);
  otaPass[pl] = 0;
  __sync_synchronize(); // the strings before the flag hal_loop() looks at
  if (otaWaiting)
    otaCancelPending = true; // ends the current wait; otaLoop() then starts this one
  otaStartPending = true;
}

/* ---- Screen: full-screen panel on the top layer, over whatever is showing ---- */

static lv_obj_t *otaPanel = NULL;
static lv_obj_t *otaTitle, *otaText, *otaArc, *otaPct;

static void otaUiOpen(const char *title, const char *text, bool progress)
{
  if (!otaPanel)
  {
    otaPanel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(otaPanel);
    lv_obj_set_size(otaPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(otaPanel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(otaPanel, LV_OPA_COVER, 0);
    lv_obj_add_flag(otaPanel, LV_OBJ_FLAG_CLICKABLE); // swallow touches meant for the screen below
    lv_obj_remove_flag(otaPanel, LV_OBJ_FLAG_SCROLLABLE);

    otaArc = lv_arc_create(otaPanel);
    lv_obj_set_size(otaArc, 300, 300);
    lv_obj_center(otaArc);
    lv_arc_set_rotation(otaArc, 270);
    lv_arc_set_bg_angles(otaArc, 0, 360);
    lv_arc_set_range(otaArc, 0, 100);
    lv_obj_remove_style(otaArc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(otaArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(otaArc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(otaArc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(otaArc, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_arc_color(otaArc, lv_color_hex(0x00997F), LV_PART_INDICATOR);

    otaPct = lv_label_create(otaPanel);
    lv_obj_set_style_text_font(otaPct, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(otaPct, lv_color_white(), 0);
    lv_obj_center(otaPct);

    otaTitle = lv_label_create(otaPanel);
    lv_obj_set_style_text_font(otaTitle, &nm_text_28, 0);
    lv_obj_set_style_text_color(otaTitle, lv_color_hex(0xFFC24D), 0);
    lv_obj_align(otaTitle, LV_ALIGN_TOP_MID, 0, 40);

    otaText = lv_label_create(otaPanel);
    lv_obj_set_width(otaText, 330);
    lv_label_set_long_mode(otaText, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(otaText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(otaText, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(otaText, lv_color_hex(0xD0D0D0), 0);
    lv_obj_align(otaText, LV_ALIGN_BOTTOM_MID, 0, -50);
  }
  lv_label_set_text(otaTitle, title);
  lv_label_set_text(otaText, text);
  if (progress)
  {
    lv_obj_remove_flag(otaArc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(otaPct, LV_OBJ_FLAG_HIDDEN);
  }
  else
  {
    lv_obj_add_flag(otaArc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(otaPct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(otaText, LV_ALIGN_CENTER, 0, 0);
  }
}

static void otaUiProgress(int pct)
{
  lv_arc_set_value(otaArc, pct);
  lv_label_set_text_fmt(otaPct, "%d%%", pct);
}

static void otaUiClose(void)
{
  if (otaPanel)
  {
    lv_obj_delete(otaPanel);
    otaPanel = NULL;
  }
}

/* hal_loop() is blocked meanwhile: keep the screen lit and LVGL drawing. */
static void otaUiPump(void)
{
  screen_on(0);
  screenPowerSync();
  screenBrightness(lv_slider_get_value(ui_brightnessSlider));
  lv_timer_handler();
}

static void otaUiHold(unsigned long ms)
{
  unsigned long t = millis();
  while (millis() - t < ms)
  {
    otaUiPump();
    delay(20);
  }
}

/* ---- Transfer ---- */

static bool otaReadExact(WiFiClient &c, uint8_t *buf, size_t n, unsigned long timeoutMs)
{
  size_t got = 0;
  unsigned long last = millis();
  while (got < n)
  {
    if (c.available() > 0)
    {
      int r = c.read(buf + got, n - got);
      if (r > 0)
      {
        got += r;
        last = millis();
        continue;
      }
    }
    if (!c.connected() || millis() - last > timeoutMs)
      return false;
    delay(2);
  }
  return true;
}

static uint8_t otaReceive(WiFiClient &c)
{
  uint8_t h[24];
  if (!otaReadExact(c, h, sizeof(h), 10000) || memcmp(h, "NMOT", 4) != 0)
    return OTA_TCP_BAD_HEADER;
  uint32_t size = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) | ((uint32_t)h[6] << 8) | h[7];
  char md5[33];
  for (int i = 0; i < 16; i++)
    sprintf(md5 + 2 * i, "%02x", h[8 + i]);
  if (size == 0 || !Update.begin(size, U_FLASH))
    return OTA_TCP_NO_SPACE;
  Update.setMD5(md5);

  static uint8_t buf[4096];
  uint32_t got = 0;
  unsigned long last = millis(), lastUi = 0;
  int shown = -1;
  OtaTagScan tag;
  while (got < size)
  {
    if (tag.done && strcmp(tag.board, otaBoardName()) != 0)
    {
      strlcpy(otaWrongBoard, tag.board, sizeof(otaWrongBoard));
      Update.abort();
      return OTA_TCP_WRONG_BOARD;
    }
    int a = c.available();
    if (a > 0)
    {
      size_t want = size - got;
      if (want > sizeof(buf))
        want = sizeof(buf);
      if (want > (size_t)a)
        want = a;
      int r = c.read(buf, want);
      if (r > 0)
      {
        if (Update.write(buf, r) != (size_t)r)
        {
          Update.abort();
          return OTA_TCP_WRITE;
        }
        tag.feed(buf, r);
        got += r;
        last = millis();
      }
    }
    else
    {
      if (otaCancelPending || !c.connected() || millis() - last > OTA_STALL_MS)
      {
        Update.abort();
        return OTA_TCP_STALLED;
      }
      delay(1);
    }
    if (millis() - lastUi > 200)
    {
      lastUi = millis();
      int p = (int)((uint64_t)got * 100 / size);
      if (p != shown)
      {
        shown = p;
        otaUiProgress(p);
      }
      otaUiPump();
    }
  }
  const char *board = tag.done ? tag.board : OTA_UNTAGGED_BOARD;
  if (strcmp(board, otaBoardName()) != 0)
  {
    strlcpy(otaWrongBoard, board, sizeof(otaWrongBoard));
    Update.abort();
    return OTA_TCP_WRONG_BOARD;
  }
  otaUiProgress(100);
  otaUiPump();
  if (!Update.end())
    return OTA_TCP_VERIFY;
  return OTA_TCP_OK;
}

static void otaWifiOff(void)
{
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void otaSession(void)
{
  otaBusy = true;
  otaCancelPending = false;
  int pct = halBatteryPercent();
  if (!halExternalPower() && pct >= 0 && pct < OTA_MIN_BATTERY)
  {
    otaReport(OTA_ERR_BATTERY);
    otaUiOpen("Batería baja", "Carga la placa (al menos 30 %) para actualizar el firmware.", false);
    otaUiHold(5000);
    otaUiClose();
    otaBusy = false;
    return;
  }

  otaUiOpen("Actualización", "Abriendo la red WiFi de la placa...", true);
  otaUiProgress(0);
  otaUiPump();
  WiFi.persistent(false);
  bool up = WiFi.mode(WIFI_AP) && WiFi.softAP(otaSsid, otaPass, 6, 0, 1);
  if (!up)
  {
    Serial.println("[OTA] access point failed");
    otaReport(OTA_ERR_WIFI);
    otaWifiOff();
    otaUiOpen("Actualización", "No se pudo abrir la red WiFi de la placa.", false);
    otaUiHold(4000);
    otaUiClose();
    otaBusy = false;
    return;
  }
  WiFiServer server(OTA_PORT);
  server.begin();
  Serial.printf("[OTA] AP %s up at %s, heap %u\n", otaSsid, WiFi.softAPIP().toString().c_str(),
                (unsigned)ESP.getFreeHeap());
  otaReport(OTA_READY);
  lv_label_set_text(otaText, "Esperando al celular...\nAcepta la conexión WiFi en la app");

  WiFiClient client;
  unsigned long t0 = millis();
  uint8_t fail = 0;
  bool joinedShown = false;
  otaWaiting = true;
  while (!client)
  {
    if (!joinedShown && WiFi.softAPgetStationNum() > 0)
    {
      joinedShown = true;
      Serial.println("[OTA] phone joined the access point");
      lv_label_set_text(otaText, "Celular conectado a la red de la placa...");
    }
    client = server.available();
    if (client)
      break;
    if (otaCancelPending)
    {
      fail = OTA_CANCELLED;
      break;
    }
    if (millis() - t0 > OTA_WAIT_PHONE_MS)
    {
      fail = OTA_ERR_TIMEOUT;
      break;
    }
    otaUiPump();
    delay(20);
  }
  otaWaiting = false;

  if (fail == OTA_CANCELLED && otaStartPending)
  {
    // The phone retried: straight into the new session (otaLoop), no message in between.
    server.stop();
    otaWifiOff();
    otaBusy = false;
    Serial.println("[OTA] phone retried, restarting the access point");
    return;
  }

  if (fail == 0)
  {
    client.setNoDelay(true);
    lv_label_set_text(otaText, "Recibiendo el firmware...\nNo apagues la placa");
    otaUiPump();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    uint8_t r = otaReceive(client);
    Serial.printf("[OTA] transfer result %d\n", r);
    client.write(&r, 1);
    client.flush();
    delay(300);
    client.stop();
    server.stop();
    if (r == OTA_TCP_OK && target)
    {
      prefs.putString(OTA_PREF_TARGET, target->label);
      lv_label_set_text(otaText, "Firmware recibido.\nReiniciando...");
      otaUiHold(1500);
      otaWifiOff();
      ESP.restart();
    }
    fail = OTA_ERR_FAILED;
    char wrong[96];
    snprintf(wrong, sizeof(wrong), "Ese firmware es para la placa %s; esta es una %s.", otaWrongBoard,
             otaBoardName());
    const char *why = r == OTA_TCP_VERIFY        ? "El archivo llegó dañado o no es un firmware válido."
                      : r == OTA_TCP_NO_SPACE    ? "El firmware no cabe en la placa."
                      : r == OTA_TCP_WRITE       ? "Error al escribir en la memoria."
                      : r == OTA_TCP_WRONG_BOARD ? wrong
                                                 : "Se cortó la transferencia.";
    otaUiOpen("Actualización", why, false);
  }
  else
  {
    server.stop();
    otaUiOpen("Actualización",
              fail == OTA_CANCELLED ? "Actualización cancelada." : "El celular no se conectó a la red de la placa.",
              false);
  }
  otaReport(fail);
  otaWifiOff();
  otaUiHold(4000);
  otaUiClose();
  Serial.printf("[OTA] ended (%d), heap %u\n", fail, (unsigned)ESP.getFreeHeap());
  otaBusy = false;
}

/* ---- After a restart ---- */

void otaBoot(void)
{
  String to = prefs.getString(OTA_PREF_TARGET, "");
  if (to.length() == 0)
    return;
  const esp_partition_t *run = esp_ota_get_running_partition();
  if (run && to == run->label)
    return; // the new firmware: it says so once it has proved itself (otaLoop)
  // Restarted on the other slot: the new firmware failed and the bootloader went back.
  prefs.remove(OTA_PREF_TARGET);
  otaBootResult = OTA_ROLLED_BACK;
  Serial.println("[OTA] the update failed, running the previous firmware");
}

void otaLoop(void)
{
  if (otaStartPending)
  {
    otaStartPending = false;
    otaSession();
  }

  if (!otaValid)
  {
    if (!watch.isConnected())
    {
      otaConnectedSince = 0;
    }
    else if (otaConnectedSince == 0)
    {
      otaConnectedSince = millis();
    }
    else if (millis() - otaConnectedSince > OTA_VALID_AFTER_MS)
    {
      otaValid = true;
      const esp_partition_t *run = esp_ota_get_running_partition();
      esp_ota_img_states_t st;
      if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY)
      {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA] new firmware confirmed");
      }
      if (prefs.getString(OTA_PREF_TARGET, "").length() > 0)
      {
        prefs.remove(OTA_PREF_TARGET);
        otaBootResult = OTA_UPDATED;
      }
      if (otaBootResult)
      {
        otaReport(otaBootResult);
        if (otaBootResult == OTA_UPDATED)
          otaUiOpen("Actualización", "Firmware actualizado.", false);
        else
          otaUiOpen("Actualización", "La actualización falló.\nVolvió la versión anterior.", false);
        otaMessageUntil = millis() + 5000;
        screen_on(0);
        otaBootResult = 0;
      }
    }
  }

  if (otaMessageUntil && (long)(millis() - otaMessageUntil) > 0)
  {
    otaMessageUntil = 0;
    otaUiClose();
  }
}
