/*
   MIT License

  Copyright (c) 2023 Felix Biego

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ______________  _____
  ___  __/___  /_ ___(_)_____ _______ _______
  __  /_  __  __ \__  / _  _ \__  __ `/_  __ \
  _  __/  _  /_/ /_  /  /  __/_  /_/ / / /_/ /
  /_/     /_.___/ /_/   \___/ _\__, /  \____/
                              /____/

*/

#include "Arduino.h"
#include <ChronosESP32.h>
#include <timber.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "app_hal.h"

#include "feedback.h"

#include <lvgl.h>
#include "ui/ui.h"

#include "ui/custom_face.h"
#include "common/api.h"

#include "main.h"
#include "displays/pins.h"
#include "splash.h"


#include "driver/rtc_io.h"

#include "FS.h"
#include "FFat.h"

#ifdef M5_STACK_DIAL
#include "M5Dial.h"
#define tft M5Dial.Display
#elif defined(VIEWE_SMARTRING) || defined(VIEWE_KNOB_15) || defined(VIEWE_S3_1_5) || defined(ESPS3_1_75)|| defined(ESPS3_2_06)
#include "displays/viewe.hpp"
#define SW_ROTATION
#elif defined(VIEWE_ECHO_EAR)
#include "displays/echo_ear.hpp"
#elif defined(ELECROW_S3)
#include "displays/elecrow_s3.hpp"
#elif defined(ESP32_CYD)
#include "displays/cyd_2432.hpp"
#elif defined(VIEWE_2_8)
#include "displays/viewe_2_8.hpp"
#else
#include "displays/generic.hpp"
#endif

#if defined(VIEWE_KNOB_15) || defined(ELECROW_S3)
#include <Encoder.h>
Encoder myEnc(ENCODER_A, ENCODER_B);
#endif

#ifdef ENABLE_APP_QMI8658C
#include "FastIMU.h"
#define QMI_ADDRESS 0x6B
#endif

#ifdef ENABLE_RTC
#include <RtcPCF8563.h>
RtcPCF8563<TwoWire> Rtc(Wire);
#endif

/* The PCF85063 real-time clock is on the same shared I2C bus as the touch panel and the
   AXP2101. ESPS3_1_75 carries the chip too (confirmed by the user against their board),
   so it gets the same treatment as ESPS3_2_06: the watch reads the time from it at boot
   and writes back whenever the phone sends a fresh sync. Without it the clock started
   from zero on every power-up and stayed wrong until a phone connected. */
#if ESPS3_2_06 || ESPS3_1_75
#include <SensorPCF85063.hpp>
SensorPCF85063 rtc;
#endif

#if ESPS3_2_06
#include "esp_sleep.h"
#endif

// Waveshare's ESP32-S3-Touch-AMOLED-1.75 (ESPS3_1_75) also has an onboard AXP2101 PMU
// (confirmed via Waveshare's own product docs), sharing the touch I2C bus like on
// ESPS3_2_06 - so real battery-percent reading applies there too. The rest of
// ESPS3_2_06's PMU-adjacent code (Extreme Power Save, its RTC) stays board-specific
// above/below since it's tuned for that board's own touch driver and sleep circuit.
#if ESPS3_2_06 || ESPS3_1_75
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
XPowersAXP2101 PMU;
int watchBatteryPercent = 100; // last known-good AXP2101 reading, used by watch faces + settings
#endif

#define FLASH FFat
#define F_NAME "FATFS"

#if defined(BUTTON_HOME) && (BUTTON_HOME != -1)
#include "Button2.h"
Button2 btn_home;
#endif

ChronosESP32 watch("Chronos C3");
Preferences prefs;

#ifdef ENABLE_APP_QMI8658C
QMI8658 qmi8658c;
calData calib = {0};
AccelData acc;
GyroData gyro;
#endif

static const uint32_t screenWidth = SCREEN_WIDTH;
static const uint32_t screenHeight = SCREEN_HEIGHT;

// watch.getTime("%A"/"%b") returns English names from the underlying ESP32Time/strftime
// implementation (not something this project can localize), so weekday/month names
// shown on the clock face are looked up here instead, keyed off watch.getDayofWeek()
// (0=Sun..6=Sat) and watch.getMonth() (0=Jan..11=Dec).
static const char *ES_WEEKDAYS[7] = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};
static const char *ES_MONTHS_ABBR[12] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};

const unsigned int lvBufferSize = screenWidth * 80;
// LVGL requires draw buffers aligned to LV_DRAW_BUF_ALIGN (4, per lv_conf.h) - this was
// never explicit, so it only "worked" by the accident of wherever the linker happened
// to place this array relative to other globals. Adding one unrelated bool variable
// elsewhere in this file shifted that placement enough to break the accidental
// alignment, which made lv_display_set_buffers() assert on every boot (a hard hang,
// not a crash - the assert handler loops forever, hence a static frozen splash screen
// no matter how many times the board was reset). Align it explicitly instead of
// depending on layout luck.
uint8_t lvBuffer[2][lvBufferSize] __attribute__((aligned(4)));

bool weatherUpdate = true, notificationsUpdate = true, weatherUpdateFace = true;
// Set from notificationCallback(), which runs on NimBLE's own host task, not the
// loop() task that owns LVGL. showAlert()/feedbackRun() touch LVGL objects directly,
// and LVGL is not thread-safe - calling them straight from the BLE task raced with
// lv_timer_handler() running concurrently in hal_loop() and corrupted LVGL's internal
// state (crashed the board, confirmed via serial backtrace landing inside
// notificationCallback -> showAlert -> lv_obj_invalidate on the NimBLE host task).
// Deferring the actual work to hal_loop() keeps all LVGL access on its owning task.
bool alertPending = false;
// Same rationale as alertPending, for the weather forecast/hourly list rebuild - see
// rebuildWeatherLists() in the notificationCallback/onWeatherLoad area below.
bool weatherListRebuildPending = false;
// Same rationale again, for the BLE connection-state indicator - connectionCallback()
// runs on the NimBLE host task (ChronosESP32::onConnect/onDisconnect), and touching
// LVGL directly from there crashed reliably (confirmed via serial: lv_inv_area asserted
// "Invalidate area is not allowed during rendering", deep in
// connectionCallback -> lv_obj_remove_state -> lv_obj_invalidate, immediately after a
// BLE reconnect). This was rare before Extreme Power Save started cycling BLE on every
// screen sleep/wake - now connect/disconnect events (and this bug) are far more
// frequent.
bool connectionStatePending = false;
bool pendingConnectionState = false;
// Same rationale again, for incoming-call ringer alerts - ringerCallback() is invoked
// from the same BLE receive path (case 0x72's ringer sub-commands in the vendored
// ChronosESP32 library), so it has the identical thread-safety bug even though it
// hasn't been observed crashing yet.
bool ringerPending = false;
bool pendingRingerState = false;
String pendingRingerCaller = "";
// Same rationale again, for the contacts list. configCallback() (NimBLE host task) used
// to call setNoContacts()/clearContactList()/addContact() directly, and all three build
// or destroy LVGL objects - the 4th instance of this exact bug in this file. Symptom seen
// live: the board rebooting right after the phone re-sent its contacts, plus visibly
// interleaved/garbled Serial output during the sync, which is the same two-tasks-at-once
// race showing up in a harmless place.
bool contactsClearPending = false;
bool contactsRebuildPending = false;
int pendingContactCount = 0;
int pendingSosIndex = 0;
/* The call screen is reused for two situations: an INCOMING call (green answer + red
   reject) and a call the user placed from the watch's own contact list, where answering
   makes no sense and only the red hang-up button is shown. Kept at file scope so
   dial_contact() can reconfigure the screen that hal_setup() built. */
lv_obj_t *ui_callAcceptButton = NULL;
lv_obj_t *ui_callRejectButton = NULL;

ChronosTimer screenTimer;
ChronosTimer alertTimer;
ChronosTimer searchTimer;

Navigation nav;
bool navChanged = false;
bool navIcChanged = false;
uint32_t navIcCRC = 0xFFFFFFFF;

lv_obj_t *lastActScr;

// bool circular = false;
bool alertSwitch = false;
bool gameActive = false;
bool readIMU = false;
bool updateSeconds = false;
bool hasUpdatedSec = false;
bool navSwitch = false;
bool extremePowerSave = false;
#if ESPS3_2_06 || ESPS3_1_75
bool on_battery() { return !PMU.isVbusIn(); }
#endif
bool touchAsleep = false; // step 4: tracks whether tft.touch.sleep() was called, so
                          // screen_on() only pays TouchDrvFT6X36::wakeup()'s ~200ms
                          // reset cost when actually waking from a real touch sleep
bool bleAsleep = false;   // step 5: tracks whether watch.stop() was called, so
                          // screen_on() only re-inits BLE (watch.begin()) when it
                          // was actually stopped

// ESPS3_1_75: BLE is no longer stopped the moment the screen times out (that added
// noticeable reconnect lag right when real-time navigation data started arriving,
// forcing the user to keep the screen awake by hand during motorcycle navigation).
// Instead it waits for this much longer, separate stretch of true inactivity - reset by
// any touch/button press via screen_on() - before actually cutting BLE. Navigation being
// active is already fully exempt via gameActive upstream of this check (same as the
// rest of Extreme Power Save), so a long ride never trips this even with the screen off.
unsigned long lastActivityMs = 0;
const unsigned long BLE_SLEEP_INACTIVITY_MS = 60UL * 60UL * 1000UL; // 1 hour

static long oldPosition = 0;

String customFacePaths[15];
int customFaceIndex;
static bool transfer = false;
#ifdef ENABLE_CUSTOM_FACE
#error "Custom Watchface has not been migrated to LVGL 9 yet"
// watchface transfer
int cSize, pos, recv;
uint32_t total, currentRecv;
bool last;
String fName;
uint8_t buf1[1024];
uint8_t buf2[1024];
static bool writeFile = false, wSwitch = true;
static int wLen1 = 0, wLen2 = 0;
bool start = false;
int lastCustom;
#endif

TaskHandle_t gameHandle = NULL;

void showAlert();
bool isDay();
void setTimeout(int i);
void screenBrightness(uint8_t value);

void hal_setup(void);
void hal_loop(void);

void update_faces();
void updateQrLinks();

void flashDrive_cb(lv_event_t *e);
void driveList_cb(lv_event_t *e);

void checkLocal(bool faces = false);
void registerWatchface_cb(const char *name, const lv_image_dsc_t *preview, lv_obj_t **watchface, lv_obj_t **second);
void registerCustomFace(const char *name, const lv_image_dsc_t *preview, lv_obj_t **watchface, String path);

String hexString(uint8_t *arr, size_t len, bool caps = false, String separator = "");

bool loadCustomFace(String file);
bool deleteCustomFace(String file);
bool readDialBytes(const char *path, uint8_t *data, size_t offset, size_t size);
bool isKnown(uint8_t id);
void parseDial(const char *path, bool restart = false);
bool lvImgHeader(uint8_t *byteArray, uint8_t cf, uint16_t w, uint16_t h, uint16_t stride);


lv_display_rotation_t getRotation(uint8_t rotation)
{
    if (rotation > 3) return LV_DISPLAY_ROTATION_0;
    return (lv_display_rotation_t)rotation;
}

/* Display flushing */
void my_disp_flush(lv_display_t *display, const lv_area_t *area, unsigned char *data)
{

  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  lv_draw_sw_rgb565_swap(data, w * h);

#ifdef SW_ROTATION
  lv_display_rotation_t rotation = lv_display_get_rotation(display);
	lv_area_t rotated_area;
  if(rotation != LV_DISPLAY_ROTATION_0) {
    lv_color_format_t cf = lv_display_get_color_format(display);
    /*Calculate the position of the rotated area*/
    rotated_area = *area;
    lv_display_rotate_area(display, &rotated_area);
    /*Calculate the source stride (bytes in a line) from the width of the area*/
    uint32_t src_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), cf);
    /*Calculate the stride of the destination (rotated) area too*/
    uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&rotated_area), cf);
    /*Have a buffer to store the rotated area and perform the rotation*/
    static uint8_t rotated_buf[lvBufferSize];
    lv_draw_sw_rotate(data, rotated_buf, w, h, src_stride, dest_stride, rotation, cf);
    /*Use the rotated area and rotated buffer from now on*/
    area = &rotated_area;
    data = rotated_buf;
  }
#endif

  if (tft.getStartCount() == 0)
  {
    tft.endWrite();
  }

  tft.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (uint16_t *)data);
  lv_display_flush_ready(display); /* tell lvgl that flushing is done */
}

void rounder_event_cb(lv_event_t *e)
{
  lv_area_t *area = lv_event_get_invalidated_area(e);
  uint16_t x1 = area->x1;
  uint16_t x2 = area->x2;

  uint16_t y1 = area->y1;
  uint16_t y2 = area->y2;

  // round the start of coordinate down to the nearest 2M number
  area->x1 = (x1 >> 1) << 1;
  area->y1 = (y1 >> 1) << 1;
  // round the end of coordinate up to the nearest 2N+1 number
  area->x2 = ((x2 >> 1) << 1) + 1;
  area->y2 = ((y2 >> 1) << 1) + 1;
}

/*Read the touchpad*/
void my_touchpad_read(lv_indev_t *indev_driver, lv_indev_data_t *data)
{
  bool touched;
  uint8_t gesture;
  uint16_t touchX, touchY;
  // RemoteTouch rt = watch.getTouch(); // remote touch
  // if (rt.state)
  // {
  //   // use remote touch when active
  //   touched = rt.state;
  //   touchX = rt.x;
  //   touchY = rt.y;
  // }
  // else
  // {
  //   touched = tft.getTouch(&touchX, &touchY);
  // }

#if ESPS3_2_06
  // Step 4: don't poll a sleeping touch chip over I2C every tick - it can't
  // wake itself while extremePowerSave has it in deep sleep, only the GPIO0
  // button can (see screen_on()). Deliberately NOT extended to ESPS3_1_75:
  // LovyanGFX's Touch_CST816S driver (the one this board actually uses,
  // shared with ESPS3_2_06 - see generic.hpp) implements wakeup()/sleep() as
  // empty no-ops, so "sleeping" the touch chip here saved zero real power on
  // this board while making the screen stop responding to taps until the
  // physical button woke it - a pure UX regression with no offsetting
  // benefit. Confirmed live and reverted per the user's choice.
  if (touchAsleep)
  {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
#endif

  touched = tft.getTouch(&touchX, &touchY);

  if (!touched)
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  else
  {
    data->state = LV_INDEV_STATE_PRESSED;

    /*Set the coordinates*/
    data->point.x = touchX;
    data->point.y = touchY;
    screen_on();
  }
}

void screen_on(long extra)
{
  screenTimer.time = millis() + extra;
  screenTimer.active = true;
  lastActivityMs = millis();

#if ESPS3_2_06
  // Only pay the ~200ms touch chip wakeup()/reset() cost when touch was
  // actually put to sleep (step 4, ESPS3_2_06-only - see my_touchpad_read()).
  // A real touch press can't reach this function while asleep in the first
  // place (nothing to read), so this only fires for the button/alarm wake
  // paths, never on the ordinary touch path.
  if (touchAsleep)
  {
    tft.touch.wakeup();
    touchAsleep = false;
  }
#endif

#if ESPS3_2_06 || ESPS3_1_75
  // Step 5: restart BLE the same way - only when it was actually stopped. On
  // ESPS3_1_75 this now only ever happens after the long BLE_SLEEP_INACTIVITY_MS
  // window (see hal_loop()), not on every screen timeout - so this fires far less
  // often here than on ESPS3_2_06, but the restart mechanism itself is identical.
  if (bleAsleep)
  {
    watch.begin();
    bleAsleep = false;
  }
#endif
}

/* Extreme Power Save step 3: while on power (VBUS present) with the switch on,
   suppress the normal auto-timeout so the screen stays on indefinitely - the
   GPIO0 button can still force it off (btn_home_handler's existing force-off
   branch is untouched, it just keeps working since we don't touch
   screenTimer.active/duration here, only skip the expiry check). Genuinely
   irrelevant on boards without the AXP2101 (no VBUS sensing there), so this
   always reads false off ESPS3_2_06/ESPS3_1_75 and every other board's
   behavior is byte-for-byte unchanged. */
bool staying_on_for_charging()
{
#if ESPS3_2_06
  return extremePowerSave && !on_battery();
#else
  /* Deliberately NOT enabled for ESPS3_1_75 any more: keeping this AMOLED lit
     indefinitely while charging made the board run noticeably warm, which isn't a
     trade the user wants. The screen now follows its normal inactivity timeout
     whether charging or on battery. */
  return false;
#endif
}

#if ESPS3_2_06
/* Extreme Power Save step 6, retry #3: timer-only polling instead of a GPIO
   wake source. Attempts #1 (ext0) and #2 (ext1) both proved the RTC-GPIO
   wake circuit itself is unreliable off USB power (confirmed live: ext1
   worked cleanly while connected, failed identically to ext0 once actually
   on battery - almost certainly a clock domain ESP-IDF only keeps alive
   while a USB host is attached, per DEVELOPER_NOTES.txt cont. 5/6). But the
   plain TIMER wake was 100% reliable in every single test across both
   attempts (rc=0, clean, every cycle, no exceptions) - so this version
   doesn't use a GPIO wake source at all. It just polls GPIO0 with a normal
   digitalRead() each time the timer wakes it, on a short interval for
   still-reasonably-snappy response. GPIO0 never switches to RTC-IO function
   this way - it stays in Button2's own digital mode throughout, which
   sidesteps the whole RTC-wake-circuit class of problem entirely, not just
   works around it. */
void deep_idle_loop()
{
  Timber.i("deep_idle_loop: entering (timer-poll)");
  Serial.flush();

  int iterations = 0;
  while (extremePowerSave && touchAsleep && bleAsleep && on_battery())
  {
    iterations++;
    esp_sleep_enable_timer_wakeup(200000); // poll every 200ms
    esp_light_sleep_start();

    if (digitalRead(0) == LOW) // button pressed (active low)
    {
      Timber.i("deep_idle_loop: button seen on wake poll (iteration %d)", iterations);
      Serial.flush();

      // Consume this press entirely here rather than handing it to Button2:
      // wake the screen ourselves right now, then wait out the physical
      // release before returning. Button2's own click-duration timing hadn't
      // polled in seconds (asleep the whole time) - letting it see this
      // still-held wake press as its first reading measured the hold from an
      // arbitrary starting point and misclassified plain wake presses as
      // long-clicks (jumped into the face picker instead of just waking).
      // Waiting for release here means Button2's first post-wake poll always
      // sees a clean "not pressed" state, so only a genuinely new press
      // after this one ever gets classified as a click.
      // The cap must comfortably cover any realistic human hold (a 2s cap
      // was too short - a held-a-bit-longer wake press handed the still-down
      // button to Button2 anyway, which then read it as a fresh press on an
      // already-awake home screen and treated it as the existing
      // press-to-turn-off toggle, instantly undoing the screen_on() just
      // above). 15s is only meant as a safety net against a stuck/shorted
      // pin, not a real expected wait.
      screen_on();
      // screen_on() only sets screenTimer's flag/timestamp - the code that
      // actually restores the physical backlight lives further down in
      // hal_loop(), which we won't reach until this function returns (i.e.
      // not until release, since we're about to block below). Restore the
      // backlight ourselves right now so the screen visibly lights up
      // immediately on press, not only after release.
      screenBrightness(lv_slider_get_value(ui_brightnessSlider));
      unsigned long releaseWaitStart = millis();
      while (digitalRead(0) == LOW && millis() - releaseWaitStart < 15000)
      {
        delay(10);
      }
      break;
    }
  }
  Timber.i("deep_idle_loop: exited after %d iterations", iterations);
  Serial.flush();
}
#endif

bool check_alert_state(AlertType type)
{
  return (alert_states & type) == type;
}

#ifdef ELECROW_C3
// ELECROW C3 I2C IO extender
#define PI4IO_I2C_ADDR 0x43

// Extended IO function
void init_IO_extender()
{
  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x01); // test register
  Wire.endTransmission();
  Wire.requestFrom(PI4IO_I2C_ADDR, 1);
  uint8_t rxdata = Wire.read();
  Serial.print("Device ID: ");
  Serial.println(rxdata, HEX);

  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x03);                                                 // IO direction register
  Wire.write((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4)); // set pins 0, 1, 2 as outputs
  Wire.endTransmission();

  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x07);                                                    // Output Hi-Z register
  Wire.write(~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4))); // set pins 0, 1, 2 low
  Wire.endTransmission();
}

void set_pin_io(uint8_t pin_number, bool value)
{

  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x05); // test register
  Wire.endTransmission();
  Wire.requestFrom(PI4IO_I2C_ADDR, 1);
  uint8_t rxdata = Wire.read();
  Serial.print("Before the change: ");
  Serial.println(rxdata, HEX);

  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x05); // Output register

  if (!value)
    Wire.write((~(1 << pin_number)) & rxdata); // set pin low
  else
    Wire.write((1 << pin_number) | rxdata); // set pin high
  Wire.endTransmission();

  // Wire.beginTransmission(PI4IO_I2C_ADDR);
  // Wire.write(0x05); // test register
  // Wire.endTransmission();
  // Wire.requestFrom(PI4IO_I2C_ADDR, 1);
  // rxdata = Wire.read();
  // Serial.print("after the change: ");
  // Serial.println(rxdata, HEX);
}
#endif

#ifdef ENABLE_RTC
bool wasError(const char *errorTopic = "")
{
  uint8_t error = Rtc.LastError();
  if (error != 0)
  {
    // we have a communications error
    // see https://www.arduino.cc/reference/en/language/functions/communication/wire/endtransmission/
    // for what the number means
    Serial.print("[");
    Serial.print(errorTopic);
    Serial.print("] WIRE communications error (");
    Serial.print(error);
    Serial.print(") : ");

    switch (error)
    {
    case Rtc_Wire_Error_None:
      Serial.println("(none?!)");
      break;
    case Rtc_Wire_Error_TxBufferOverflow:
      Serial.println("transmit buffer overflow");
      break;
    case Rtc_Wire_Error_NoAddressableDevice:
      Serial.println("no device responded");
      break;
    case Rtc_Wire_Error_UnsupportedRequest:
      Serial.println("device doesn't support request");
      break;
    case Rtc_Wire_Error_Unspecific:
      Serial.println("unspecified error");
      break;
    case Rtc_Wire_Error_CommunicationTimeout:
      Serial.println("communications timed out");
      break;
    }
    return true;
  }
  return false;
}
#endif

String heapUsage()
{
  String usage;
  uint32_t total = ESP.getHeapSize();
  uint32_t free = ESP.getFreeHeap();
  usage += "Total: " + String(total);
  usage += "\tFree: " + String(free);
  usage += "\t" + String(((total - free) * 1.0) / total * 100, 2) + "%";
  return usage;
}

void *sd_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
  char buf[256];
  sprintf(buf, "/%s", path);
  // Serial.print("path : ");
  // Serial.println(buf);

  File f;

  if (mode == LV_FS_MODE_WR)
  {
    f = FLASH.open(buf, FILE_WRITE);
  }
  else if (mode == LV_FS_MODE_RD)
  {
    f = FLASH.open(buf);
  }
  else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD))
  {
    f = FLASH.open(buf, FILE_WRITE);
  }

  if (!f)
  {
    return NULL; // Return NULL if the file failed to open
  }

  File *fp = new File(f); // Allocate the File object on the heap
  return (void *)fp;      // Return the pointer to the allocated File object
}

lv_fs_res_t sd_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
  lv_fs_res_t res = LV_FS_RES_NOT_IMP;
  File *fp = (File *)file_p;
  uint8_t *buffer = (uint8_t *)buf;

  // Serial.print("name sd_read_cb : ");
  // Serial.println(fp->name());
  *br = fp->read(buffer, btr);

  res = LV_FS_RES_OK;
  return res;
}

lv_fs_res_t sd_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
  lv_fs_res_t res = LV_FS_RES_OK;
  File *fp = (File *)file_p;

  uint32_t actual_pos;

  switch (whence)
  {
  case LV_FS_SEEK_SET:
    actual_pos = pos;
    break;
  case LV_FS_SEEK_CUR:
    actual_pos = fp->position() + pos;
    break;
  case LV_FS_SEEK_END:
    actual_pos = fp->size() + pos;
    break;
  default:
    return LV_FS_RES_INV_PARAM; // Invalid parameter
  }

  if (!fp->seek(actual_pos))
  {
    return LV_FS_RES_UNKNOWN; // Seek failed
  }

  // Serial.print("name sd_seek_cb : ");
  // Serial.println(fp->name());

  return res;
}

lv_fs_res_t sd_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
  lv_fs_res_t res = LV_FS_RES_NOT_IMP;
  File *fp = (File *)file_p;

  *pos_p = fp->position();
  // Serial.print("name in sd_tell_cb : ");
  // Serial.println(fp->name());
  res = LV_FS_RES_OK;
  return res;
}

lv_fs_res_t sd_close_cb(lv_fs_drv_t *drv, void *file_p)
{
  File *fp = (File *)file_p;

  fp->close();
  // delete fp;  // Free the allocated memory

  return LV_FS_RES_OK;
}

void checkLocal(bool faces)
{

  File root = FLASH.open("/");
  if (!root)
  {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
    }
    else
    {
#ifdef ENABLE_CUSTOM_FACE
      String nm = String(file.name());
      // addListFile(file.name(), file.size());
      if (faces)
      {
        if (nm.endsWith(".js"))
        {
          Serial.print("Found raw face file: ");
          Serial.println(nm);
          String js = "/" + nm + "on";
          nm = "/" + nm;
          if (!FLASH.exists(js.c_str()))
          {
            Serial.println("Parsing");
            parseDial(nm.c_str(), false);
          }
          else
          {
            Serial.println("Skipping, already parsed");
          }
        }
      }
      else
      {

        if (nm.endsWith(".json"))
        {
          // load watchface elements
          nm = "/" + nm;
          registerCustomFace(nm.c_str(), &ui_img_custom_preview_png, &face_custom_root, nm);
        }
      }
#endif
    }
    file = root.openNextFile();
  }
}

void screenBrightness(uint8_t value)
{

#ifdef ELECROW_C3
  set_pin_io(2, value > 0); // ELECROW C3, no brightness control
#else
  tft.setBrightness(value);
#endif
}

void vibratePin(bool state)
{
#ifdef ELECROW_C3
  set_pin_io(0, state); // ELECROW C3, vibration pin
#elif VIBRATION_PIN
  digitalWrite(VIBRATION_PIN, state);
#endif
}

String readFile(const char *path)
{
  String result;
  File file = FLASH.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    return result;
  }

  Serial.println("- read from file:");
  while (file.available())
  {
    result += (char)file.read();
  }
  file.close();
  return result;
}

void deleteFile(const char *path)
{
  Serial.printf("Deleting file: %s\r\n", path);
  if (FLASH.remove(path))
  {
    Serial.println("- file deleted");
  }
  else
  {
    Serial.println("- delete failed");
  }
}

bool setupFS()
{

#ifndef ENABLE_CUSTOM_FACE
  return false;
#endif

  if (!FLASH.begin(true, "/ffat", MAX_FILE_OPEN))
  {
    FLASH.format();

    return false;
  }

  static lv_fs_drv_t sd_drv;
  lv_fs_drv_init(&sd_drv);
  sd_drv.cache_size = 512;

  sd_drv.letter = 'S';
  sd_drv.open_cb = sd_open_cb;
  sd_drv.close_cb = sd_close_cb;
  sd_drv.read_cb = sd_read_cb;
  sd_drv.seek_cb = sd_seek_cb;
  sd_drv.tell_cb = sd_tell_cb;
  lv_fs_drv_register(&sd_drv);

  checkLocal(true); // parse new faces

  checkLocal(); // register the local faces

  return true;
}

void listDir(const char *dirname, uint8_t levels)
{

  lv_obj_clean(ui_fileManagerPanel);

  addListBack(driveList_cb);

  File root = FLASH.open(dirname);
  if (!root)
  {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      addListDir(file.name());
      // if (levels)
      // {
      //   listDir(file.path(), levels - 1);
      // }
    }
    else
    {
      addListFile(file.name(), file.size());
    }
    file = root.openNextFile();
  }
}

void flashDrive_cb(lv_event_t *e)
{
  lv_disp_t *display = lv_display_get_default();
  lv_obj_t *actScr = lv_display_get_screen_active(display);
  if (actScr != ui_filesScreen)
  {
    return;
  }

  listDir("/", 0);
}

void sdDrive_cb(lv_event_t *e)
{
  lv_disp_t *display = lv_display_get_default();
  lv_obj_t *actScr = lv_display_get_screen_active(display);
  if (actScr != ui_filesScreen)
  {
    return;
  }

  showError("Error", "SD card is currently unavaliable");
}

void driveList_cb(lv_event_t *e)
{
  lv_obj_clean(ui_fileManagerPanel);

  addListDrive(F_NAME, FLASH.totalBytes(), FLASH.usedBytes(), flashDrive_cb);
  addListDrive("SD card", 0, 0, sdDrive_cb); // dummy SD card drive
}

bool loadCustomFace(String file)
{
  String path = file;
  if (!path.startsWith("/"))
  {
    path = "/" + path;
  }
  String read = readFile(path.c_str());
  JsonDocument face;
  DeserializationError err = deserializeJson(face, read);
  if (!err)
  {
    if (!face["elements"].is<JsonArray>())
    {
      return false;
    }
    String name = face["name"].as<String>();
    JsonArray elements = face["elements"].as<JsonArray>();
    int sz = elements.size();

    Serial.print(sz);
    Serial.println(" elements");

    invalidate_all();
    lv_obj_clean(face_custom_root);

    for (int i = 0; i < sz; i++)
    {
      JsonObject element = elements[i];
      int id = element["id"].as<int>();
      int x = element["x"].as<int>();
      int y = element["y"].as<int>();
      int pvX = element["pvX"].as<int>();
      int pvY = element["pvY"].as<int>();
      String image = element["image"].as<String>();
      JsonArray group = element["group"].as<JsonArray>();

      const char *group_arr[20];
      int group_size = group.size();
      for (int j = 0; j < group_size && j < 20; j++)
      {
        group_arr[j] = group[j].as<const char *>();
      }

      add_item(face_custom_root, id, x, y, pvX, pvY, image.c_str(), group_arr, group_size);
    }

    return true;
  }
  else
  {
    Serial.println("Deserialize failed");
  }

  return false;
}

bool deleteCustomFace(String file)
{
  String path = file;
  if (!path.startsWith("/"))
  {
    path = "/" + path;
  }
  String read = readFile(path.c_str());
  JsonDocument face;
  DeserializationError err = deserializeJson(face, read);
  if (!err)
  {
    if (!face["assets"].is<JsonArray>())
    {
      return false;
    }

    JsonArray assets = face["assets"].as<JsonArray>();
    int sz = assets.size();

    for (int j = 0; j < sz; j++)
    {
      deleteFile(assets[j].as<const char *>());
    }

    deleteFile(path.c_str());

    return true;
  }
  else
  {
    Serial.println("Deserialize failed");
  }

  return false;
}

void registerCustomFace(const char *name, const lv_image_dsc_t *preview, lv_obj_t **watchface, String path)
{
  if (numFaces >= MAX_FACES)
  {
    return;
  }
  faces[numFaces].name = name;
  faces[numFaces].preview = preview;
  faces[numFaces].watchface = watchface;

  faces[numFaces].customIndex = customFaceIndex;
  faces[numFaces].custom = true;

  addWatchface(faces[numFaces].name, faces[numFaces].preview, numFaces);

  customFacePaths[customFaceIndex] = path;
  customFaceIndex++;

  Timber.i("Custom Watchface: %s registered at %d", name, numFaces);
  numFaces++;
}

void onCustomDelete(lv_event_t *e)
{
  int index = (int)lv_event_get_user_data(e);

  Serial.println("Delete custom watchface");
  Serial.println(customFacePaths[index]);
  showError("Delete", "The watchface will be deleted from storage, ESP32 will restart after deletion");
  if (deleteCustomFace(customFacePaths[index]))
  {
    lv_screen_load_anim(ui_appListScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, false);
    ESP.restart();
  }
  else
  {
    showError("Error", "Failed to delete watchface");
  }
}

void addFaceList(lv_obj_t *parent, Face face)
{

  lv_obj_t *ui_faceItemPanel = lv_obj_create(parent);
  lv_obj_set_width(ui_faceItemPanel, 240);
  lv_obj_set_height(ui_faceItemPanel, 50);
  lv_obj_set_align(ui_faceItemPanel, LV_ALIGN_CENTER);
  lv_obj_remove_flag(ui_faceItemPanel, LV_OBJ_FLAG_SCROLLABLE); /// Flags
  lv_obj_set_style_radius(ui_faceItemPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(ui_faceItemPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_faceItemPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(ui_faceItemPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_opa(ui_faceItemPanel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ui_faceItemPanel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_side(ui_faceItemPanel, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
  // lv_obj_set_style_bg_color(ui_faceItemPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
  // lv_obj_set_style_bg_opa(ui_faceItemPanel, 100, LV_PART_MAIN | LV_STATE_PRESSED);

  lv_obj_t *ui_faceItemIcon = lv_image_create(ui_faceItemPanel);
  lv_image_set_src(ui_faceItemIcon, &ui_img_clock_png);
  lv_obj_set_width(ui_faceItemIcon, LV_SIZE_CONTENT);  /// 1
  lv_obj_set_height(ui_faceItemIcon, LV_SIZE_CONTENT); /// 1
  lv_obj_set_x(ui_faceItemIcon, 10);
  lv_obj_set_y(ui_faceItemIcon, 0);
  lv_obj_set_align(ui_faceItemIcon, LV_ALIGN_LEFT_MID);
  lv_obj_add_flag(ui_faceItemIcon, LV_OBJ_FLAG_ADV_HITTEST);   /// Flags
  lv_obj_remove_flag(ui_faceItemIcon, LV_OBJ_FLAG_SCROLLABLE); /// Flags

  lv_obj_t *ui_faceItemName = lv_label_create(ui_faceItemPanel);
  lv_obj_set_width(ui_faceItemName, 117);
  lv_obj_set_height(ui_faceItemName, LV_SIZE_CONTENT); /// 1
  lv_obj_set_x(ui_faceItemName, 50);
  lv_obj_set_y(ui_faceItemName, 0);
  lv_obj_set_align(ui_faceItemName, LV_ALIGN_LEFT_MID);
  lv_label_set_long_mode(ui_faceItemName, LV_LABEL_LONG_CLIP);
  if (face.custom)
  {
    lv_label_set_text(ui_faceItemName, customFacePaths[face.customIndex].c_str());
  }
  else
  {
    lv_label_set_text(ui_faceItemName, face.name);
  }

  lv_obj_set_style_text_font(ui_faceItemName, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_t *ui_faceItemDelete = lv_image_create(ui_faceItemPanel);
  lv_image_set_src(ui_faceItemDelete, &ui_img_bin_png);
  lv_obj_set_width(ui_faceItemDelete, LV_SIZE_CONTENT);  /// 1
  lv_obj_set_height(ui_faceItemDelete, LV_SIZE_CONTENT); /// 1
  lv_obj_set_x(ui_faceItemDelete, -10);
  lv_obj_set_y(ui_faceItemDelete, 0);
  lv_obj_set_align(ui_faceItemDelete, LV_ALIGN_RIGHT_MID);
  lv_obj_add_flag(ui_faceItemDelete, LV_OBJ_FLAG_CLICKABLE);     /// Flags
  lv_obj_add_flag(ui_faceItemDelete, LV_OBJ_FLAG_ADV_HITTEST);   /// Flags
  lv_obj_remove_flag(ui_faceItemDelete, LV_OBJ_FLAG_SCROLLABLE); /// Flags
  lv_obj_set_style_radius(ui_faceItemDelete, 20, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(ui_faceItemDelete, lv_color_hex(0xF34235), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(ui_faceItemDelete, 255, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(ui_faceItemDelete, lv_color_hex(0xF34235), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(ui_faceItemDelete, 255, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(ui_faceItemDelete, 2, LV_PART_MAIN | LV_STATE_PRESSED);

  if (!face.custom)
  {
    lv_obj_add_flag(ui_faceItemDelete, LV_OBJ_FLAG_HIDDEN);
  }
  else
  {
    lv_obj_add_event_cb(ui_faceItemDelete, onCustomDelete, LV_EVENT_CLICKED, (void *)face.customIndex);
  }
}

void timerEnded(int x)
{
  feedbackRun(T_ALARM);

  screenTimer.time = millis() + 50;
  screenTimer.active = true;
  screen_on();
}

void simonTone(int type, int pitch)
{
  switch (type)
  {
  case 0:
    feedbackTone(tone_simonsays_intro, 4, T_USER);
    break;
  case 1:
    feedbackTone(tone_simonsays_gameover, 4, T_USER);
    feedbackVibrate(v_notif, 2, true);
    break;
  case 2:
  {
    Note note[] = {pitch, 200};
    feedbackTone(note, 1, T_USER);
  }
  break;
  }
}

void connectionCallback(bool state)
{
  Timber.d(state ? "Connected" : "Disconnected");
  pendingConnectionState = state;
  connectionStatePending = true;
}

// Runs on the loop task (owner of LVGL), driven by contactsRebuildPending.
void rebuildContactList()
{
  Serial.print("Contacts received: ");
  Serial.println(pendingContactCount);

  clearContactList();

  for (int i = 0; i < pendingContactCount; i++)
  {
    Contact cn = watch.getContact(i);
    addContact(cn.name.c_str(), cn.number.c_str(), pendingSosIndex == i);
  }
}

void applyConnectionState(bool state)
{
  if (state)
  {
    lv_obj_remove_state(ui_btStateButton, LV_STATE_CHECKED);
  }
  else
  {
    lv_obj_add_state(ui_btStateButton, LV_STATE_CHECKED);
  }
  lv_label_set_text_fmt(ui_appConnectionText, "Estado\n%s", state ? "Conectado" : "Desconectado");
}

void ringerCallback(String caller, bool state)
{
  pendingRingerCaller = caller;
  pendingRingerState = state;
  ringerPending = true;
}

void applyRingerState(String caller, bool state)
{
  lv_disp_t *display = lv_display_get_default();
  lv_obj_t *actScr = lv_display_get_screen_active(display);

  if (state)
  {
    feedbackRun(T_CALLS);
    screenTimer.time = millis() + 50;

    lastActScr = actScr;
    Serial.print("Ringer: Incoming call from ");
    Serial.println(caller);
    /* Restore the two-button "incoming" layout - dial_contact() may have left the screen
       configured for an outgoing call (answer button hidden, hang-up centred). */
    lv_label_set_text(ui_callLabel, "Llamada entrante");
    if (ui_callAcceptButton)
    {
      lv_obj_remove_flag(ui_callAcceptButton, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_callRejectButton)
    {
      lv_obj_set_x(ui_callRejectButton, UI_SCALE_PX(-45));
    }
    lv_label_set_text(ui_callName, caller.c_str());
    lv_screen_load_anim(ui_callScreen, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
  }
  else
  {
    feedbackTone(tone_off, 1, T_USER, true);
    Serial.println("Ringer dismissed");
    // load last active screen
    if (actScr == ui_callScreen && lastActScr != nullptr)
    {
      lv_screen_load_anim(lastActScr, LV_SCR_LOAD_ANIM_FADE_OUT, 500, 0, false);
    }
  }
  screenTimer.active = true;
}

void notificationCallback(Notification notification)
{
  notificationsUpdate = true;
  // onNotificationsOpen(click);
  // feedbackRun()/showAlert() touch LVGL and must run on the loop() task - see
  // alertPending's declaration. hal_loop() picks this up and does the actual work.
  alertPending = true;
}

void configCallback(Config config, uint32_t a, uint32_t b)
{
  switch (config)
  {
  case CF_TIME:
    // time has been synced from BLE
#ifdef ENABLE_RTC
    // set the RTC time
    Rtc.SetDateTime(RtcDateTime(watch.getYear(), watch.getMonth() + 1, watch.getDay(), watch.getHour(true), watch.getMinute(), watch.getSecond()));

#endif

#if ESPS3_2_06 || ESPS3_1_75
    rtc.setDateTime(watch.getYear(), watch.getMonth() + 1, watch.getDay(), watch.getHour(true), watch.getMinute(), watch.getSecond());
#endif
    // ui_update_seconds(watch.getSecond());
    if (!hasUpdatedSec)
    {
      hasUpdatedSec = true;
      updateSeconds = true;
    }

    break;
  case CF_FIND:
    feedbackRun(T_TIMER);

    break;
  case CF_RST:

    Serial.println("Reset request, formating storage");
    FLASH.format();
    delay(2000);
    ESP.restart();

    break;
  case CF_WEATHER:

    if (a)
    {
      weatherUpdateFace = true;
    }
    if (a == 2)
    {
      weatherUpdate = true;
    }

    break;
  case CF_FONT:
    screenTimer.time = millis();
    screenTimer.active = true;
    if (((b >> 16) & 0xFFFF) == 0x01)
    { // Style 1
      if ((b & 0xFFFF) == 0x01)
      { // TOP
        lv_obj_set_style_text_color(ui_hourLabel, lv_color_hex(a), LV_PART_MAIN | LV_STATE_DEFAULT);
      }
      if ((b & 0xFFFF) == 0x02)
      { // CENTER
        lv_obj_set_style_text_color(ui_minuteLabel, lv_color_hex(a), LV_PART_MAIN | LV_STATE_DEFAULT);
      }
      if ((b & 0xFFFF) == 0x03)
      { // BOTTOM
        lv_obj_set_style_text_color(ui_dayLabel, lv_color_hex(a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_dateLabel, lv_color_hex(a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_weatherTemp, lv_color_hex(a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_amPmLabel, lv_color_hex(a), LV_PART_MAIN | LV_STATE_DEFAULT);
      }
    }

    break;
  case CF_CAMERA:
  {
    lv_disp_t *display = lv_display_get_default();
    lv_obj_t *actScr = lv_display_get_screen_active(display);

    if (b)
    {
      screenTimer.time = millis() + 50;
      lastActScr = actScr;
      lv_screen_load_anim(ui_cameraScreen, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
      screenTimer.active = true;
    }
    else
    {
      if (actScr == ui_cameraScreen && lastActScr != nullptr)
      {
        lv_screen_load_anim(lastActScr, LV_SCR_LOAD_ANIM_FADE_OUT, 500, 0, false);
      }
      screenTimer.active = true;
    }
  }
  break;

  case CF_APP:
    // state is saved internally
    Serial.print("Chronos App; Code: ");
    Serial.print(a); // int code = watch.getAppCode();
    Serial.print(" Version: ");
    Serial.println(watch.getAppVersion());
    lv_label_set_text_fmt(ui_appDetailsText, "App conectada\nv%s (%d)", watch.getAppVersion().c_str(), a);
    break;
  case CF_QR:
    if (a == 1)
    {
      updateQrLinks();
    }
    break;
  case CF_NAV_DATA:
    navChanged = true;
    break;
  case CF_NAV_ICON:
    if (a == 2)
    {
      navIcChanged = true;
      Timber.w("Navigation icon received. CRC 0x%04X", b);
    }
    break;
  case CF_CONTACT:
    // Defer both the "clear" and the "rebuild" to hal_loop() - see the flags' declaration
    // for why. The contact data itself is already fully stored in the library's own
    // buffer by the time a==1 fires, so reading it a few milliseconds later is fine.
    if (a == 0)
    {
      contactsClearPending = true;
    }
    if (a == 1)
    {
      pendingContactCount = uint8_t(b);      // watch.getContactCount()
      pendingSosIndex = uint8_t(b >> 8);     // watch.getSOSContactIndex()
      contactsRebuildPending = true;
    }
    break;
  }
}

void onMessageClick(lv_event_t *e)
{

  lv_disp_t *display = lv_display_get_default();
  lv_obj_t *actScr = lv_display_get_screen_active(display);
  if (actScr != ui_notificationScreen)
  {
    Timber.i("Message screen inactive");
    return;
  }
  // Your code here
  int index = (int)lv_event_get_user_data(e);

  index %= NOTIF_SIZE;
  Timber.i("Message clicked at index %d", index);

  lv_label_set_text(ui_messageTime, watch.getNotificationAt(index).time.c_str());
  lv_label_set_text(ui_messageContent, watch.getNotificationAt(index).message.c_str());
  setNotificationIcon(ui_messageIcon, watch.getNotificationAt(index).icon);

  lv_obj_scroll_to_y(ui_messagePanel, 0, LV_ANIM_ON);
  lv_obj_add_flag(ui_messageList, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(ui_messagePanel, LV_OBJ_FLAG_HIDDEN);
}

void onCaptureClick(lv_event_t *e)
{
  watch.capturePhoto();
}

void onForecastOpen(lv_event_t *e)
{
  // lv_obj_scroll_to_y(ui_forecastList, 0, LV_ANIM_ON);
}

void onScrollMode(lv_event_t *e)
{
  prefs.putBool("circular", circular);
}

void onAlertState(lv_event_t *e)
{
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
  alertSwitch = lv_obj_has_state(obj, LV_STATE_CHECKED);

  // prefs.putBool("alert_states", alert_states);
}

void on_alert_state_change(int32_t states)
{
  alert_states = states;
  prefs.putInt("alert_states", alert_states);

  feedbackTone(tone_button, 1, T_SYSTEM);
  feedbackVibrate(pattern, 2, true);
}

void onNavState(lv_event_t *e)
{
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
  navSwitch = lv_obj_has_state(obj, LV_STATE_CHECKED);

  prefs.putBool("autonav", navSwitch);
}

/* Extreme Power Save: settings toggle only so far, no behavior wired up yet
   (step 2 of the power-management plan - see DEVELOPER_NOTES.txt). */
void onExtremePowerSave(lv_event_t *e)
{
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
  extremePowerSave = lv_obj_has_state(obj, LV_STATE_CHECKED);

  prefs.putBool("extremepwr", extremePowerSave);
}

void savePrefInt(const char *key, int value)
{
  prefs.putInt(key, value);
}

int getPrefInt(const char *key, int def_value)
{
  return prefs.getInt(key, def_value);
}

void clear_touch_calibration(void)
{
  prefs.remove("touch_calib");
  ESP.restart();
}

void onNotificationsOpen(lv_event_t *e)
{
  if (!notificationsUpdate)
  {
    return;
  }
  notificationsUpdate = false;

  lv_obj_clean(ui_messageList);
  int c = watch.getNotificationCount();
  for (int i = 0; i < c; i++)
  {
    addNotificationList(watch.getNotificationAt(i).icon, watch.getNotificationAt(i).message.c_str(), i);
  }
  // addNotificationList(watch.getNotificationAt(0).icon, watch.getNotificationAt(0).message.c_str(), i);

  lv_obj_scroll_to_y(ui_messageList, 1, LV_ANIM_ON);
  lv_obj_remove_flag(ui_messageList, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_messagePanel, LV_OBJ_FLAG_HIDDEN);
}

void onClearNotifications(lv_event_t *e)
{
  watch.clearNotifications();
  notificationsUpdate = false;
  lv_obj_clean(ui_messageList);
  lv_obj_add_flag(ui_messageList, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(ui_messagePanel, LV_OBJ_FLAG_HIDDEN);
}

// Custom watch->phone command for the call accept/reject buttons - reuses
// ChronosESP32::sendCommand() (the same raw-notify mechanism musicControl() and
// setVolume() already use, see their implementations in ChronosESP32.cpp) with a hi/lo
// pair (0xC1) that doesn't collide with the library's own reserved music/volume Control
// enum (0x9D**/0x99**). No changes to the vendored library needed - this is entirely a
// phone-side (ChronosProtocol.kt/ChronosBleService.kt) + firmware-side (here) agreement,
// mirroring the exact 7-byte format decodeWatchCommand() already expects.
void sendCallAction(bool accept)
{
  uint8_t cmd[] = {0xAB, 0x00, 0x04, 0xFF, 0xC1, 0x80, (uint8_t)(accept ? 0x01 : 0x02)};
  watch.sendCommand(cmd, 7);
}

// Same raw-notify mechanism as sendCallAction(), with its own hi byte (0xC2) so the
// phone can tell "dial contact N" apart from accept/reject. lo carries the contact
// index, which is why CONTACTS_SIZE (255) conveniently fits in one byte.
void dial_contact(int index)
{
  if (index < 0 || index > 0xFF)
  {
    return;
  }
  uint8_t cmd[] = {0xAB, 0x00, 0x04, 0xFF, 0xC2, 0x80, (uint8_t)index};
  watch.sendCommand(cmd, 7);

  /* Show the call screen in "outgoing" form so the call can be hung up from here: same
     screen as an incoming call, but the green answer button is hidden - there is nothing
     to answer. The red button already maps to endCall() on the phone, which Android uses
     both to reject a ringing call and to end an active one, so it needs no new command. */
  Contact cn = watch.getContact(index);
  lv_label_set_text(ui_callLabel, "Llamando");
  lv_label_set_text(ui_callName, cn.name.c_str());
  if (ui_callAcceptButton)
  {
    lv_obj_add_flag(ui_callAcceptButton, LV_OBJ_FLAG_HIDDEN);
  }
  if (ui_callRejectButton)
  {
    /* Centre the hang-up button while it is the only one on screen. */
    lv_obj_set_x(ui_callRejectButton, 0);
  }

  lv_disp_t *display = lv_display_get_default();
  lastActScr = lv_display_get_screen_active(display);
  screenTimer.time = millis() + 50;
  screenTimer.active = true;
  lv_screen_load_anim(ui_callScreen, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

void onCallAccept(lv_event_t *e)
{
  sendCallAction(true);
  lv_screen_load_anim(ui_home, LV_SCR_LOAD_ANIM_FADE_OUT, 500, 0, false);
}

void onCallReject(lv_event_t *e)
{
  sendCallAction(false);
  lv_screen_load_anim(ui_home, LV_SCR_LOAD_ANIM_FADE_OUT, 500, 0, false);
}

void onWeatherLoad(lv_event_t *e)
{
  lv_obj_remove_flag(ui_weatherPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_forecastList, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_hourlyList, LV_OBJ_FLAG_HIDDEN);

  if (!weatherUpdate)
  {
    return;
  }
  // Deferred to hal_loop() - see rebuildWeatherLists()'s comment for why. Rebuilding
  // these lists directly from here crashed reliably (Guru Meditation LoadProhibited,
  // deep in lv_obj_clean -> lv_event_mark_deleted) the first time real weather data
  // ever populated the list.
  weatherListRebuildPending = true;
}

// See onWeatherLoad(): this used to run inline from there, but that's called from
// inside an active lv_event_send() dispatch (LV_EVENT_SCREEN_LOAD_START) - deleting
// objects (lv_obj_clean) while already nested inside LVGL's own event dispatch crashed
// reliably. Calling it from the top of hal_loop() instead - not nested inside any LVGL
// event - fixed it, matching the same fix already applied to the WhatsApp-notification
// crash (see alertPending).
void rebuildWeatherLists()
{
  weatherUpdate = false;

  if (watch.getWeatherCount() > 0)
  {
    String updateTime = "Updated at\n" + watch.getWeatherTime();
    lv_label_set_text(ui_weatherCity, watch.getWeatherCity().c_str());
    lv_label_set_text(ui_weatherUpdateTime, updateTime.c_str());
    lv_label_set_text_fmt(ui_weatherCurrentTemp, "%d°C", watch.getWeatherAt(0).temp);
    setWeatherIcon(ui_weatherIcon, watch.getWeatherAt(0).icon, isDay());
    setWeatherIcon(ui_weatherCurrentIcon, watch.getWeatherAt(0).icon, isDay());

    lv_obj_clean(ui_forecastList);
    int c = watch.getWeatherCount();
    for (int i = 0; i < c; i++)
    {
      addForecast(watch.getWeatherAt(i).day, watch.getWeatherAt(i).temp, watch.getWeatherAt(i).icon);
    }

    // lv_obj_scroll_by(ui_forecastList, 0, -1, LV_ANIM_OFF);

    lv_obj_clean(ui_hourlyList);
    addHourlyWeather(0, watch.getWeatherAt(0).icon, 0, 0, 0, 0, true);
    for (int h = watch.getHour(true); h < 24; h++)
    {
      HourlyForecast hf = watch.getForecastHour(h);
      addHourlyWeather(hf.hour, hf.icon, hf.temp, hf.humidity, hf.wind, hf.uv, false);
    }
  }
}

void onLoadHome(lv_event_t *e)
{
  // if (isDay())
  // {
  //   lv_obj_set_style_bg_image_src( ui_clockScreen, &ui_img_857483832, LV_PART_MAIN | LV_STATE_DEFAULT);
  // }
  // else
  // {
  //   lv_obj_set_style_bg_image_src( ui_clockScreen, &ui_img_753022056, LV_PART_MAIN | LV_STATE_DEFAULT);
  // }
}

void onBrightnessChange(lv_event_t *e)
{
  // Your code here
  lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
  int v = lv_slider_get_value(slider);
  screenBrightness(v);

  prefs.putInt("brightness", v);
}

void onFaceSelected(lv_event_t *e)
{
  feedbackVibrate(v_notif, 2, true);
  int index = (int)lv_event_get_user_data(e);
  prefs.putInt("watchface", index);
}

void on_watchface_list_open()
{
  feedbackVibrate(v_notif, 2, true);
}

void onCustomFaceSelected(int pathIndex)
{
#ifdef ENABLE_CUSTOM_FACE
  feedbackVibrate(v_notif, 2, true);

  if (pathIndex < 0)
  {
    prefs.putString("custom", "");
    return;
  }
  if (lv_obj_get_child_count(face_custom_root) > 0 && lastCustom == pathIndex)
  {
    ui_home = face_custom_root;
  }
  else if (loadCustomFace(customFacePaths[pathIndex]))
  {
    lastCustom = pathIndex;
    ui_home = face_custom_root;
  }

  lv_screen_load_anim(ui_home, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);

  prefs.putString("custom", customFacePaths[pathIndex]);
#endif
}

void onBatteryChange(lv_event_t *e)
{
  uint8_t lvl = lv_slider_get_value(ui_batterySlider);
  watch.setBattery(lvl);
}

void onStartSearch(lv_event_t *e)
{
  watch.findPhone(true);
}

void onEndSearch(lv_event_t *e)
{
  watch.findPhone(false);
}

void onClickAlert(lv_event_t *e)
{

  // cancel alert timer
  alertTimer.active = false;
  // change screen to notifications
  lv_screen_load(ui_notificationScreen);

  // enable screen for timeout + 5 seconds
  screenTimer.time = millis() + 5000;
  screenTimer.active = true;

  // load the last received message
  lv_label_set_text(ui_messageTime, watch.getNotificationAt(0).time.c_str());
  lv_label_set_text(ui_messageContent, watch.getNotificationAt(0).message.c_str());
  setNotificationIcon(ui_messageIcon, watch.getNotificationAt(0).icon);

  lv_obj_scroll_to_y(ui_messagePanel, 0, LV_ANIM_ON);
  lv_obj_add_flag(ui_messageList, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(ui_messagePanel, LV_OBJ_FLAG_HIDDEN);
}

void onTimeoutChange(lv_event_t *e)
{
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
  uint16_t sel = lv_dropdown_get_selected(obj);
  Timber.i("Selected index: %d", sel);

  setTimeout(sel);
  prefs.putInt("timeout", sel);
}

void onRotateChange(lv_event_t *e)
{
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
  uint16_t sel = lv_dropdown_get_selected(obj);
  Timber.i("Selected index: %d", sel);

if (SCREEN_WIDTH != SCREEN_HEIGHT && sel % 2 != 0) {
  showError("Not supported", "90 & 270 rotation is not supported when screen width & height are not equal");
  return;
}

  prefs.putInt("rotate", sel);
#ifdef SW_ROTATION
  lv_display_set_rotation(lv_display_get_default(), getRotation(sel));
#else
  tft.setRotation(sel);
  // screen rotation has changed, invalidate to redraw
  lv_obj_invalidate(lv_screen_active());
#endif
}

void onLanguageChange(lv_event_t *e)
{
}

void setTimeout(int i)
{
  if (i == 4)
  {
    screenTimer.duration = -1; // always on
  }
  else if (i == 0)
  {
    screenTimer.duration = 5000; // 5 seconds
    screenTimer.active = true;
  }
  else if (i < 4)
  {
    screenTimer.duration = 10000 * i; // 10, 20, 30 seconds
    screenTimer.active = true;
  }
}

void onMusicPlay(lv_event_t *e)
{
  watch.musicControl(MUSIC_TOGGLE);
}

void onMusicPrevious(lv_event_t *e)
{
  watch.musicControl(MUSIC_PREVIOUS);
}

void onMusicNext(lv_event_t *e)
{
  watch.musicControl(MUSIC_NEXT);
}

void onVolumeUp(lv_event_t *e)
{
  watch.musicControl(VOLUME_UP);
}

void onVolumeDown(lv_event_t *e)
{
  watch.musicControl(VOLUME_DOWN);
}

void updateQrLinks()
{
#if LV_USE_QRCODE == 1
  lv_obj_clean(ui_qrPanel);
  for (int i = 0; i < 9; i++)
  {
    addQrList(i, watch.getQrAt(i).c_str());
  }
#endif
}

void onRTWState(bool state)
{
}

void gameLoop(void *pvParameters)
{
  for (;;)
  {
    ui_games_update();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void onGameOpened()
{
  gameActive = true;

#ifdef ENABLE_GAME_TASK
  if (gameHandle == NULL)
  {
    // create task to run the game loop
    xTaskCreate(gameLoop, "Game Task", 8192, NULL, 1, &gameHandle);
  }
#endif
}

void onGameClosed()
{
  gameActive = false;
#ifdef ENABLE_GAME_TASK
  if (gameHandle != NULL)
  {
    vTaskDelete(gameHandle);
    gameHandle = NULL;
  }
#endif
  screenTimer.active = true;
}

void showAlert()
{
  lv_disp_t *display = lv_display_get_default();
  lv_obj_t *actScr = lv_display_get_screen_active(display);
  if (actScr == ui_notificationScreen)
  {
    // at notifications screen, switch to message
    // enable screen for timeout + 5 seconds
    screenTimer.time = millis() + 5000;
    screenTimer.active = true;

    // load the last received message
    lv_label_set_text(ui_messageTime, watch.getNotificationAt(0).time.c_str());
    lv_label_set_text(ui_messageContent, watch.getNotificationAt(0).message.c_str());
    setNotificationIcon(ui_messageIcon, watch.getNotificationAt(0).icon);

    lv_obj_scroll_to_y(ui_messagePanel, 0, LV_ANIM_ON);
    lv_obj_add_flag(ui_messageList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui_messagePanel, LV_OBJ_FLAG_HIDDEN);
  }
  else
  {
    if (!check_alert_state(ALERT_POPUP))
    {
      return;
    }
    // attach the alert panel to current screen
    lv_obj_set_parent(ui_alertPanel, actScr);

    // load the last received message
    lv_label_set_text(ui_alertText, watch.getNotificationAt(0).message.c_str());
    setNotificationIcon(ui_alertIcon, watch.getNotificationAt(0).icon);

    // turn screen for timeout + 5 seconds
    screenTimer.time = millis() + 5000;
    screenTimer.active = true;

    alertTimer.time = millis();
    alertTimer.active = true;

    // show the alert
    lv_obj_remove_flag(ui_alertPanel, LV_OBJ_FLAG_HIDDEN);
  }
}

void rawDataCallback(uint8_t *data, int len)
{

#ifdef ENABLE_CUSTOM_FACE
  if (data[0] == 0xB0)
  {
    // this is a chunk header data command
    cSize = data[1] * 256 + data[2];                                                           // data chunk size
    pos = data[3] * 256 + data[4];                                                             // position of the chunk, ideally sequential 0..
    last = data[7] == 1;                                                                       // whether this is the last chunk (1) or not (0)
    total = (data[8] * 256 * 256 * 256) + (data[9] * 256 * 256) + (data[10] * 256) + data[11]; // total size of the whole file
    recv = 0;                                                                                  // counter for the chunk data

    start = pos == 0;
    if (pos == 0)
    {
      // this is the first chunk
      transfer = true;
      currentRecv = 0;

      fName = "/" + String(total, HEX) + "-" + String(total) + ".cbn";
    }
  }
  if (data[0] == 0xAF)
  {
    // this is the chunk data, line by line. The complete chunk will have several of these
    // actual data starts from index 5
    int ln = ((data[1] * 256 + data[2]) - 5); // byte 1 and 2 make up the (total size of data - 5)

    if (wSwitch)
    {
      memcpy(buf1 + recv, data + 5, ln);
    }
    else
    {
      memcpy(buf2 + recv, data + 5, ln);
    }

    recv += ln; // increment the received chunk data size by current received size

    currentRecv += ln; // track the progress

    if (recv == cSize)
    { // received expected? if data chunk size equals chunk receive size then chunk is complete
      if (wSwitch)
      {
        wLen1 = cSize;
      }
      else
      {
        wLen2 = cSize;
      }

      wSwitch = !wSwitch;
      writeFile = true;

      // write to file (file name -> fName)
      // buf1, wLen1 if wSwitch false
      // buf2, wLen2 if wSwitch true

      pos++;

      // notify if save successful, otherwise ignore
      uint8_t lst = last ? 0x01 : 0x00;
      uint8_t cmd[5] = {0xB0, 0x02, highByte(pos), lowByte(pos), lst};
      watch.sendCommand(cmd, 5); // notify the app that we received the chunk, this will trigger transfer of next chunk
    }

    if (last)
    {
    }
  }
#endif
}

void dataCallback(uint8_t *data, int length)
{
  // Serial.println("Received Data");
  // for (int i = 0; i < length; i++)
  // {
  //   Serial.printf("%02X ", data[i]);
  // }
  // Serial.println();
}

void imu_init()
{
#ifdef ENABLE_APP_QMI8658C
  int err = qmi8658c.init(calib, QMI_ADDRESS);
  if (err != 0)
  {
    showError("IMU State", "Failed to init");
  }
#endif
}

imu_data_t get_imu_data()
{
  imu_data_t qmi;
#ifdef ENABLE_APP_QMI8658C

  qmi8658c.update();
  qmi8658c.getAccel(&acc);
  qmi8658c.getGyro(&gyro);

  qmi.ax = acc.accelX;
  qmi.ay = acc.accelY;
  qmi.az = acc.accelZ;
  qmi.gx = gyro.gyroX;
  qmi.gy = gyro.gyroY;
  qmi.gz = gyro.gyroZ;
  qmi.temp = qmi8658c.getTemp();
  qmi.success = true;
#else
  qmi.success = false;
#endif
  return qmi;
}

void imu_close()
{
#ifdef ENABLE_APP_QMI8658C

#endif
}

void contacts_app_launched()
{
  clearContactList();
  int n = watch.getContactCount();
  int s = watch.getSOSContactIndex();
  int i;
  for (i = 0; i < n; i++)
  {
    Contact cn = watch.getContact(i);
    addContact(cn.name.c_str(), cn.number.c_str(), s == i);
  }
  if (i == 0)
  {
    setNoContacts();
  }
}

void calendar_app_launched(void)
{
  calendar_set_today(watch.getYear(), watch.getMonth() + 1, watch.getDay());
}

int32_t read_encoder_position()
{
#ifdef M5_STACK_DIAL
  M5Dial.update();
  return M5Dial.Encoder.read();
#elif defined(VIEWE_KNOB_15) || defined(ELECROW_S3)
  return myEnc.read();
#endif
  return 0;
}

#if defined(BUTTON_HOME) && (BUTTON_HOME != -1)
void btn_home_handler(Button2 &btn)
{
  lv_obj_t *actScr = lv_screen_active();

  switch (btn.getType())
  {
  case single_click:
    if (actScr != ui_home)
    {
      lv_screen_load_anim(ui_home, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
    }
    if (screenTimer.active && screenTimer.duration > 1 && actScr == ui_home)
    {
      if (staying_on_for_charging())
      {
        // The normal expiry check is suppressed while staying_on_for_charging()
        // is true, so the rewind-then-expire trick below would never actually
        // fire - force it off directly instead.
        screenTimer.active = false;
        screenBrightness(0);
      }
      else
      {
        screenTimer.time = millis() - screenTimer.duration - 1; // trigger timeout immediately
      }
    }
    else if (!screenTimer.active)
    {
      screen_on();
    }

    break;
  case double_click:
    Serial.print("double ");
    if (actScr != ui_appListScreen)
    {
      lv_screen_load_anim(ui_appListScreen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
    }
    if (!screenTimer.active)
    {
      screen_on();
    }
    break;
  case triple_click:
    Serial.print("triple ");
    break;
  case long_click:
    Serial.print("long");
    if (actScr == ui_home)
    {
      lv_screen_load_anim(ui_faceSelect, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
      on_watchface_list_open();
      if (!screenTimer.active)
      {
        screen_on();
      }
    }
    break;
  case empty:
    return;
  }
}
#endif

void logCallback(timber_level_t level, uint32_t ts, const char *message)
{
  Serial.print(message);
  Serial1.print(message);
}

// void lv_log_register_print_cb(lv_log_print_g_cb_t print_cb) {
//   // Do nothing, not needed here!
// }

// void my_log_cb(lv_log_level_t level, const char *buf)
// {
//   Serial.write(buf, strlen(buf));
//   Serial1.write(buf, strlen(buf));
// }

int putchar(int ch)
{
  Serial.write(ch); // Send character to Serial
  return ch;
}

void loadSplash()
{
  int w = 122;
  int h = 130;
  int x = (SCREEN_WIDTH - w) / 2;
  int y = (SCREEN_HEIGHT - h) / 2;
  tft.fillScreen(TFT_BLACK);
  screenBrightness(200);
  tft.pushImage(x, y, w, h, (uint16_t *)splash);
  delay(2000);
}

static uint32_t my_tick(void)
{
  return millis();
}

void hal_setup()
{

  Serial.begin(115200); /* prepare for possible serial debug */
  Serial1.begin(115200);

  Timber.setOutput(logCallback);

  Timber.i("Starting up device");

  prefs.begin("my-app");

  int rt = prefs.getInt("rotate", 0);

#ifdef ELECROW_C3
  Wire.begin(4, 5);
  init_IO_extender();
  delay(100);
  set_pin_io(0, false);
  set_pin_io(2, true);
  set_pin_io(3, true);
  set_pin_io(4, true);
#endif

#ifdef M5_STACK_DIAL
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
#endif
  alert_states = 0x0F; // set default
#if !defined(BUZZER_PIN) || (BUZZER_PIN == -1)
  alert_states &= ~0x04;
#endif
#if !defined(VIBRATION_PIN) || (VIBRATION_PIN == -1)
  alert_states &= ~0x08;
#endif

  tft.init();
  tft.initDMA();
  tft.startWrite();
  tft.fillScreen(TFT_BLACK);
  tft.setRotation(rt);

#ifdef ESP32_CYD
  uint16_t calib[8];
  bool calibLoaded = prefs.getBytes("touch_calib", calib, sizeof(calib));
  if (calibLoaded)
  {
    for (int i = 0; i < 8; i++)
    {
      Timber.i("Stored calib %d: %d", i, calib[i]);
    }
    tft.setTouchCalibrate(calib);
  }
  else
  {
    tft.setTextColor(0xFFFFFFU, 0);
    tft.setTextWrap(true, true);
    tft.println("Touch calibration required!\nPress center to start then \ntouch the corners as instructed.\nUse a pen or stylus.");
    uint16_t touchX, touchY;
    while(!tft.getTouch(&touchX, &touchY)) {
      delay(100);
    }

    tft.fillScreen(TFT_BLACK);

    Timber.w("No touch calibration found, starting calibration");
    tft.calibrateTouch(calib, 0xFFFF, 0x0000);
    for (int i = 0; i < 8; i++)
    {
      Timber.d("Configured calib %d: %d", i, calib[i]);
    }
    prefs.putBytes("touch_calib", calib, sizeof(calib));
  }
#endif

  loadSplash();


  alert_states = prefs.getInt("alert_states", alert_states);

  startToneSystem();
  startVibrationSystem();

  feedbackTone(tone_startup, 3, T_SYSTEM);
  feedbackVibrate(pattern, 4);

  lv_init();

  lv_tick_set_cb(my_tick);

  static auto *lvDisplay = lv_display_create(screenWidth, screenHeight);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvDisplay, my_disp_flush);
  lv_display_set_buffers(lvDisplay, lvBuffer[0], lvBuffer[1], lvBufferSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(lvDisplay, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

#ifdef SW_ROTATION
  lv_display_set_rotation(lvDisplay, getRotation(rt));
#endif

  static auto *lvInput = lv_indev_create();
  lv_indev_set_type(lvInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvInput, my_touchpad_read);
  // lvInput->gesture_limit = LV_CLAMP(50, ((SCREEN_WIDTH * 2) / 3), 255);

  // lv_log_register_print_cb(my_log_cb);

  // _lv_fs_init();

  ui_init();

#if ESPS3_2_06 || ESPS3_1_75
  lv_obj_remove_flag(ui_batterySlider, LV_OBJ_FLAG_CLICKABLE); // real PMU reading, not a fake-level debug control
#endif

  bool fsState = setupFS();
  if (fsState)
  {
    // driveList_cb(NULL);
    Serial.println("Setup FS success");

    Timber.i("Flash: Total %d => Used %d", FLASH.totalBytes(), FLASH.usedBytes());
  }
  else
  {
    Serial.println("Setup FS failed");
    // showError(F_NAME, "Failed to mount the partition");
  }

  int wf = prefs.getInt("watchface", 0);
#ifdef ENABLE_CUSTOM_FACE
  String custom = prefs.getString("custom", "");
  if (wf >= numFaces)
  {
    wf = 0; // default
  }
  currentIndex = wf;
  if (custom != "" && fsState && loadCustomFace(custom))
  {
    ui_home = face_custom_root;
  }
  else
  {
    ui_home = *faces[wf].watchface; // load saved watchface power on
  }
#else
  if (wf >= numFaces)
  {
    wf = 0; // default
  }
  currentIndex = wf;
  ui_home = *faces[wf].watchface; // load saved watchface power on
#endif
  lv_screen_load(ui_home);

  int ch = lv_obj_get_child_count(ui_faceSelect);
  if (wf < ch)
  {
    lv_obj_scroll_to_view(lv_obj_get_child(ui_faceSelect, wf), LV_ANIM_OFF);
  }

#if defined(ESPS3_1_69) || defined(ELECROW_S3) || defined(ESP32_CYD)
  watch.setScreen(CS_240x296_191_RTF);
#elif defined(ESPS3_2_06)
  watch.setScreen(CS_410x494_200_RTF);
#elif defined(VIEWE_SMARTRING) || defined(VIEWE_KNOB_15) ||  defined(ESPS3_1_75)
  watch.setScreen(CS_466x466_143_CTF);
#endif
  String chip = String(ESP.getChipModel());
  watch.setName(chip);
#ifdef BOARD_NAME
  watch.setName(BOARD_NAME);
#endif
  watch.setConnectionCallback(connectionCallback);
  watch.setNotificationCallback(notificationCallback);
  watch.setConfigurationCallback(configCallback);
  watch.setRingerCallback(ringerCallback);
  watch.setDataCallback(dataCallback);
  watch.setRawDataCallback(rawDataCallback);
  watch.begin();
  watch.set24Hour(true);
  watch.setBattery(85);

  String about = String(ui_info_text) + "\n" + chip + "\n" + watch.getAddress();
  lv_label_set_text(ui_aboutText, about.c_str());

#if LV_USE_QRCODE == 1
  String address = watch.getAddress();
  address.toUpperCase();
  String qrCode = "{\"Name\":\"" + chip + "\", \"Mac\":\"" + address + "\"}";
  lv_qrcode_update(ui_connectImage, qrCode.c_str(), qrCode.length());
  lv_label_set_text(ui_connectText, "Escanea para conectar");
#endif
  // bool intro = prefs.getBool("intro", true);

  // if (intro)
  // {
  //   showAlert();
  //   prefs.putBool("intro", false);
  // }
  // else
  // {
  //   lv_obj_add_flag(ui_alertPanel, LV_OBJ_FLAG_HIDDEN);
  // }

  // load saved preferences
  int tm = prefs.getInt("timeout", 0);

  int br = prefs.getInt("brightness", 100);
  circular = prefs.getBool("circular", false);
  alertSwitch = prefs.getBool("alerts", false);
  navSwitch = prefs.getBool("autonav", false);
  extremePowerSave = prefs.getBool("extremepwr", false);

  lv_obj_scroll_to_y(ui_settingsList, 1, LV_ANIM_ON);
  lv_obj_scroll_to_y(ui_appList, 1, LV_ANIM_ON);
  lv_obj_scroll_to_y(ui_appInfoPanel, 1, LV_ANIM_ON);
  lv_obj_scroll_to_y(ui_gameList, 1, LV_ANIM_ON);

  if (tm > 4)
  {
    tm = 4;
  }
  else if (tm < 0)
  {
    tm = 0;
  }

  screenBrightness(br);

  lv_dropdown_set_selected(ui_timeoutSelect, tm);
  lv_dropdown_set_selected(ui_rotateSelect, rt);
  lv_slider_set_value(ui_brightnessSlider, br, LV_ANIM_OFF);

  set_alert_states(alert_states);

  if (circular)
  {
    lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
  }
  else
  {
    lv_obj_remove_state(ui_Switch2, LV_STATE_CHECKED);
  }

#ifdef ENABLE_APP_NAVIGATION
  if (navSwitch)
  {
    lv_obj_add_state(ui_navStateSwitch, LV_STATE_CHECKED);
  }
  else
  {
    lv_obj_remove_state(ui_navStateSwitch, LV_STATE_CHECKED);
  }
#endif

// The switch itself and its click handler (onExtremePowerSave()) already run on every
// board - only the actual power-save behavior (steps 3-6) stays gated to specific
// boards below. This boot-time restore just needs the same board list so the switch
// correctly reflects its saved on/off state after a restart, instead of always
// starting unchecked.
#if ESPS3_2_06 || ESPS3_1_75
  if (extremePowerSave)
  {
    lv_obj_add_state(ui_extremePowerSaveSwitch, LV_STATE_CHECKED);
  }
  else
  {
    lv_obj_remove_state(ui_extremePowerSaveSwitch, LV_STATE_CHECKED);
  }
#endif

  screenTimer.active = true;
  screenTimer.time = millis();
  lastActivityMs = millis(); // boot counts as activity - otherwise this starts at 0 and
                             // the 1-hour BLE-sleep check would fire on the very first
                             // hal_loop() iteration (millis() - 0 already exceeds it).

  setTimeout(tm);

#ifdef ENABLE_CUSTOM_FACE
  if (!fsState)
  {
    showError(F_NAME, "Failed to mount the partition");
  }
#endif
  if (lv_fs_is_ready('S'))
  {
    Serial.println("Drive S is ready");
  }
  else
  {
    Serial.println("Drive S is not ready");
  }

  imu_init();

#ifdef ENABLE_RTC
  Rtc.Begin();

  if (!Rtc.GetIsRunning())
  {
    uint8_t error = Rtc.LastError();
    if (error != 0)
    {
      showError("RTC", "Error on RTC");
    }
    Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();

  watch.setTime(now.Second(), now.Minute(), now.Hour(), now.Day(), now.Month(), now.Year());

  Rtc.StopAlarm();
  Rtc.StopTimer();
  Rtc.SetSquareWavePin(PCF8563SquareWavePinMode_None);
#endif

#if ESPS3_2_06 || ESPS3_1_75
  /* Runs after tft.init(), which is what actually brings up the shared I2C bus with the
     touch pins - the RTC, the touch controller and the AXP2101 all live on it. */
  if (!rtc.begin(Wire))
	{
		Timber.e("Failed to find PCF85063 - check your wiring!");
	}
  else
  {
    RTC_DateTime dt = rtc.getDateTime();
    /* A chip that has never been set reads back as year 2000 or similar. Showing that
       would be worse than showing nothing, so only trust a plausible date and otherwise
       just wait for the phone's first time sync (which then writes the RTC). */
    if (dt.getYear() >= 2024)
    {
      watch.setTime(dt.getSecond(), dt.getMinute(), dt.getHour(), dt.getDay(), dt.getMonth(), dt.getYear());
      Timber.i("Time restored from RTC: %04d-%02d-%02d %02d:%02d:%02d",
               dt.getYear(), dt.getMonth(), dt.getDay(), dt.getHour(), dt.getMinute(), dt.getSecond());
    }
    else
    {
      Timber.w("RTC has no valid time yet (year %d) - waiting for phone sync", dt.getYear());
    }
    rtc.resetAlarm();
    rtc.disableAlarm();
  }
#endif

#if ESPS3_2_06 || ESPS3_1_75
  if (!PMU.init(Wire, TOUCH_SDA, TOUCH_SCL, AXP2101_SLAVE_ADDRESS))
  {
    Timber.e("Failed to find AXP2101 - check your wiring!");
  }
  else
  {
    int pct = PMU.getBatteryPercent();
    Timber.i("AXP2101 battery: %d%%, charging: %d, vbus: %d", pct, PMU.isCharging(), PMU.isVbusIn());
    if (pct >= 0)
    {
      watchBatteryPercent = pct;
      watch.setBattery(pct, PMU.isCharging());
      lv_slider_set_value(ui_batterySlider, watchBatteryPercent, LV_ANIM_OFF);
      lv_label_set_text_fmt(ui_batteryLabel, "Batería %d%%", watchBatteryPercent);
    }
  }
#endif

  ui_update_seconds(watch.getSecond());

  lv_rand_set_seed(millis());

  navigateInfo("Navigation", "Chronos", "Start navigation on Google maps");

  watch.clearNotifications();
  notificationsUpdate = false;
  lv_obj_clean(ui_messageList);
  lv_obj_t *info = lv_label_create(ui_messageList);
  lv_obj_set_width(info, 180);
  lv_obj_set_y(info, 20);
  lv_obj_set_height(info, LV_SIZE_CONTENT); /// 1
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(info, "No hay notificaciones disponibles. Conecta el reloj con la app Chronos Companion en tu celular");

  // Positioned top-mid (not top-right) with a modest offset: this is a round display,
  // so a literal screen corner (e.g. TOP_RIGHT with a small inset) falls outside the
  // visible circular area and is clipped by the bezel - unlike a rectangular screen,
  // "near the top and off to one side" only stays visible if it hugs the vertical
  // center line. Also kept above y=50 (the notification list's own top padding, see
  // ui_messageList's pad_top in ui.c) so it never sits under the first list item.
  lv_obj_t *ui_clearNotifications = lv_image_create(ui_notificationScreen);
  lv_image_set_src(ui_clearNotifications, &ui_img_bin_png);
  lv_obj_set_width(ui_clearNotifications, LV_SIZE_CONTENT);
  lv_obj_set_height(ui_clearNotifications, LV_SIZE_CONTENT);
  lv_obj_set_align(ui_clearNotifications, LV_ALIGN_TOP_MID);
  lv_obj_set_x(ui_clearNotifications, UI_SCALE_PX(44));
  lv_obj_set_y(ui_clearNotifications, UI_SCALE_PX(10));
  lv_obj_add_flag(ui_clearNotifications, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ui_clearNotifications, LV_OBJ_FLAG_ADV_HITTEST);
  lv_obj_remove_flag(ui_clearNotifications, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ui_clearNotifications, onClearNotifications, LV_EVENT_CLICKED, NULL);

  // Incoming-call accept/reject buttons. ui_callScreen already existed (icon + caller
  // name label, from the original template) but had no way to actually control the call -
  // swiping away just dismissed the screen locally, it never told the phone anything.
  // Positioned low, symmetric left/right of center - round-safe the same way as
  // ui_clearNotifications above: at this height (~150px below screen center on this
  // 466px round display) the visible circle is wide enough for two buttons this size
  // with margin to spare.
  ui_callAcceptButton = lv_obj_create(ui_callScreen);
  lv_obj_set_width(ui_callAcceptButton, UI_SCALE_PX(64));
  lv_obj_set_height(ui_callAcceptButton, UI_SCALE_PX(64));
  lv_obj_set_align(ui_callAcceptButton, LV_ALIGN_BOTTOM_MID);
  lv_obj_set_x(ui_callAcceptButton, UI_SCALE_PX(45));
  lv_obj_set_y(ui_callAcceptButton, UI_SCALE_PX(-40));
  lv_obj_set_style_radius(ui_callAcceptButton, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(ui_callAcceptButton, lv_color_hex(0x2ECC71), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_callAcceptButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ui_callAcceptButton, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_remove_flag(ui_callAcceptButton, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ui_callAcceptButton, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ui_callAcceptButton, onCallAccept, LV_EVENT_CLICKED, NULL);

  lv_obj_t *ui_callAcceptIcon = lv_image_create(ui_callAcceptButton);
  lv_image_set_src(ui_callAcceptIcon, &ui_img_answer_png);
  lv_obj_set_align(ui_callAcceptIcon, LV_ALIGN_CENTER);
  lv_obj_remove_flag(ui_callAcceptIcon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ui_callAcceptIcon, LV_OBJ_FLAG_ADV_HITTEST);

  ui_callRejectButton = lv_obj_create(ui_callScreen);
  lv_obj_set_width(ui_callRejectButton, UI_SCALE_PX(64));
  lv_obj_set_height(ui_callRejectButton, UI_SCALE_PX(64));
  lv_obj_set_align(ui_callRejectButton, LV_ALIGN_BOTTOM_MID);
  lv_obj_set_x(ui_callRejectButton, UI_SCALE_PX(-45));
  lv_obj_set_y(ui_callRejectButton, UI_SCALE_PX(-40));
  lv_obj_set_style_radius(ui_callRejectButton, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(ui_callRejectButton, lv_color_hex(0xE74C3C), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui_callRejectButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ui_callRejectButton, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_remove_flag(ui_callRejectButton, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ui_callRejectButton, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ui_callRejectButton, onCallReject, LV_EVENT_CLICKED, NULL);

  lv_obj_t *ui_callRejectLabel = lv_label_create(ui_callRejectButton);
  lv_label_set_text(ui_callRejectLabel, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(ui_callRejectLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(ui_callRejectLabel);

  // Re-space the two labels the template placed above (ui.c's ui_callScreen_screen_init):
  // the caller name sat at y=169 (=328px here), which lands right inside the 264..388px
  // band the buttons above now occupy, so the name rendered behind them. Pull it up, and
  // lift the phone icon by the same idea to keep the four elements evenly spread:
  //   "Llamada entrante" 68..88, icon 101..165, name 179..223, buttons 264..388.
  // Done here rather than in ui.c so the template stays untouched and this stays one
  // coherent unit with the buttons that caused the conflict. Values are tuned against
  // this board's 466x466 panel; the icon is a fixed 64px asset that doesn't scale with
  // UI_SCALE_PX, so smaller panels would need their own tuning.
  lv_obj_set_y(ui_callIcon, UI_SCALE_PX(52));
  lv_obj_set_y(ui_callName, UI_SCALE_PX(92));

#if !defined(BUZZER_PIN) || (BUZZER_PIN == -1)
  lv_obj_add_state(ui_soundsAlert, LV_STATE_DISABLED);
#endif
#if !defined(VIBRATION_PIN) || (VIBRATION_PIN == -1)
  lv_obj_add_state(ui_vibrateAlert, LV_STATE_DISABLED);
#endif

  ui_setup();

#if defined(BUTTON_HOME) && (BUTTON_HOME != -1)
  btn_home.begin(BUTTON_HOME);

  btn_home.setClickHandler(btn_home_handler);
  //btn_home.setLongClickHandler(btn_home_handler);       // this will only be called upon release
  btn_home.setLongClickDetectedHandler(btn_home_handler);  // this will only be called upon detection
  btn_home.setDoubleClickHandler(btn_home_handler);
  btn_home.setTripleClickHandler(btn_home_handler);
  btn_home.setLongClickTime(1000); // set long click time to 1000ms
#endif

  Serial.println(heapUsage());
  Serial.printf("[PSRAM] size=%u free=%u\n",
                (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

  Timber.i("Setup done");
  Timber.i(about.c_str());
}

void hal_loop()
{
#if ESPS3_2_06
  if (extremePowerSave && touchAsleep && bleAsleep && on_battery())
  {
    deep_idle_loop();
  }
#endif

  /* Heap watch. LVGL is configured with LV_USE_STDLIB_MALLOC = LV_STDLIB_CLIB, so every
     UI object (including the watch face labels rewritten every second) allocates from the
     same system heap the BLE stack needs - and only ~31 KB of it is free at boot. After
     ~7.7 hours of uptime that heap could no longer satisfy BLE ("BLE_INIT: Malloc failed"
     on serial), so the watch accepted connections but could not complete GATT service
     discovery, and the phone dropped it after 15s (status=22) over and over. A reboot
     cleared it completely.
     Both a slow leak and fragmentation fit those symptoms, and they need different fixes,
     so log the numbers instead of guessing: `free` falling steadily means a leak, while
     `free` holding roughly flat while `maxAlloc` (largest contiguous block) shrinks means
     fragmentation. */
  static unsigned long lastHeapLog = 0;
  if (millis() - lastHeapLog >= 300000UL) // every 5 minutes
  {
    lastHeapLog = millis();
    // psram* is here to size up the real fix: the board definition (lolin_s3_pro.json)
    // already sets -DBOARD_HAS_PSRAM with OPI PSRAM, but LVGL allocates through plain
    // malloc(), which on Arduino-ESP32 serves internal RAM only. If PSRAM really is
    // present, pointing LVGL's allocator at it moves every screen/label/style off the
    // 192 KB internal heap that BLE also depends on.
    Serial.printf("[HEAP] up=%lus free=%u maxAlloc=%u minEver=%u | psramSize=%u psramFree=%u\n",
                  millis() / 1000UL,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getPsramSize(),
                  (unsigned)ESP.getFreePsram());
  }

  if (!transfer)
  {
    if (alertPending)
    {
      alertPending = false;
      feedbackRun(T_NOTIFICATION);
      showAlert();
    }

    // New weather can land while the user is already sitting on the weather screen.
    // onWeatherLoad() only runs on LV_EVENT_SCREEN_LOAD_START, so without this the screen
    // keeps showing the old values until they navigate away and back - confirmed on
    // hardware, and the reason the weather looked "stuck" even though the phone's log
    // showed every packet delivered. rebuildWeatherLists() clears weatherUpdate itself,
    // so this can't retrigger in a loop.
    if (weatherUpdate && lv_screen_active() == ui_weatherScreen)
    {
      weatherListRebuildPending = true;
    }

    if (weatherListRebuildPending)
    {
      weatherListRebuildPending = false;
      rebuildWeatherLists();
    }

    if (connectionStatePending)
    {
      connectionStatePending = false;
      applyConnectionState(pendingConnectionState);
    }

    if (ringerPending)
    {
      ringerPending = false;
      applyRingerState(pendingRingerCaller, pendingRingerState);
    }

    if (contactsClearPending)
    {
      contactsClearPending = false;
      setNoContacts();
    }

    if (contactsRebuildPending)
    {
      contactsRebuildPending = false;
      rebuildContactList();
    }

    lv_timer_handler(); // Update the UI-
    delay(5);

    watch.loop();

#if defined(BUTTON_HOME) && (BUTTON_HOME != -1)
  btn_home.loop();
#endif

#if defined(M5_STACK_DIAL) || defined(VIEWE_KNOB_15) || defined(ELECROW_S3)
    long newPosition = read_encoder_position();
    if (newPosition != oldPosition)
    {
      input_bus_emit_encoder_event(newPosition, newPosition - oldPosition);
      oldPosition = newPosition;
    }
#endif
#ifdef M5_STACK_DIAL
    M5Dial.update();
    if (M5Dial.BtnA.wasPressed())
    {
      input_bus_emit_button_event(true);
      M5Dial.Encoder.readAndReset();
    }
#endif

    if (updateSeconds)
    {
      updateSeconds = false;
      ui_update_seconds(watch.getSecond());
    }

    if (ui_home == ui_clockScreen)
    {
      lv_label_set_text(ui_hourLabel, watch.getHourZ().c_str());
      lv_label_set_text(ui_dayLabel, ES_WEEKDAYS[watch.getDayofWeek()]);
      lv_label_set_text(ui_minuteLabel, watch.getTime("%M").c_str());
      String dateStr = watch.getTime("%d\n") + String(ES_MONTHS_ABBR[watch.getMonth()]);
      lv_label_set_text(ui_dateLabel, dateStr.c_str());
      lv_label_set_text(ui_amPmLabel, watch.getAmPmC(false).c_str());
    }
    else
    {
      update_faces();
    }

    lv_disp_t *display = lv_display_get_default();
    lv_obj_t *actScr = lv_display_get_screen_active(display);
    if (actScr != ui_home)
    {
    }

    if (weatherUpdateFace)
    {
      lv_label_set_text_fmt(ui_weatherTemp, "%d°C", watch.getWeatherAt(0).temp);
      // set icon ui_weatherIcon
      setWeatherIcon(ui_weatherIcon, watch.getWeatherAt(0).icon, isDay());
      weatherUpdateFace = false;
    }

    if (navChanged)
    {
      navChanged = false;
      nav = watch.getNavigation();
      if (!nav.active)
      {
        nav.directions = "Start navigation on Google maps";
        nav.title = "Chronos";
        nav.duration = watch.isConnected() ? "Inactive" : "Disconnected";
        nav.eta = "Navigation";
        nav.distance = "";
        navIcCRC = 0xFFFFFFFF;
      }

      if (!nav.isNavigation)
      {
        nav.directions = nav.title;
        nav.title = "";
      }

      String navText = nav.eta + "\n" + nav.duration + " " + nav.distance;

#ifdef ENABLE_APP_NAVIGATION
      if (actScr != get_nav_screen() && nav.active && navSwitch)
      {
        lastActScr = actScr;
        if (!get_nav_screen())
        {
          ui_navScreen_screen_init();
        }
        lv_screen_load_anim(get_nav_screen(), LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
        gameActive = true;
        screenTimer.active = true;
      }
      if (actScr == get_nav_screen() && !nav.active && navSwitch && lastActScr != nullptr)
      {
        screenTimer.active = true;
        lv_screen_load_anim(lastActScr, LV_SCR_LOAD_ANIM_FADE_OUT, 500, 0, false);
      }
#endif
      navIconState(nav.active && nav.hasIcon);
      navigateInfo(navText.c_str(), nav.title.c_str(), nav.directions.c_str());
    }
    if (navIcChanged)
    {
      navIcChanged = false;
      nav = watch.getNavigation();

      if (nav.iconCRC != navIcCRC)
      {
        navIcCRC = nav.iconCRC;
        navIconState(nav.active && nav.hasIcon);
        for (int y = 0; y < 48; y++)
        {
          for (int x = 0; x < 48; x++)
          {
            int byte_index = (y * 48 + x) / 8;
            int bit_pos = 7 - (x % 8);
            bool px_on = (nav.icon[byte_index] >> bit_pos) & 0x01;
            setNavIconPx(x, y, px_on);
          }
        }
        navIconRefresh();
      }
    }

    if (actScr == ui_appInfoScreen)
    {
      lv_label_set_text_fmt(ui_appBatteryText, "Batería - %d%%", watch.getPhoneBattery());
      lv_bar_set_value(ui_appBatteryLevel, watch.getPhoneBattery(), LV_ANIM_OFF);
      if (watch.isPhoneCharging())
      {
        lv_image_set_src(ui_appBatteryIcon, &ui_img_battery_plugged_png);
      }
      else
      {
        lv_image_set_src(ui_appBatteryIcon, &ui_img_battery_state_png);
      }
    }

    if (alertTimer.active)
    {
      if (alertTimer.time + alertTimer.duration < millis())
      {
        alertTimer.active = false;
        lv_obj_add_flag(ui_alertPanel, LV_OBJ_FLAG_HIDDEN);
      }
    }

#if ESPS3_2_06 || ESPS3_1_75
    {
      static unsigned long lastBatteryPoll = 0;
      if (millis() - lastBatteryPoll >= 30000)
      {
        lastBatteryPoll = millis();
        int pct = PMU.getBatteryPercent();
        if (pct >= 0)
        {
          watchBatteryPercent = pct;
          watch.setBattery(pct, PMU.isCharging());
          lv_slider_set_value(ui_batterySlider, watchBatteryPercent, LV_ANIM_OFF);
          lv_label_set_text_fmt(ui_batteryLabel, "Batería %d%%", watchBatteryPercent);
        }
      }
    }
#endif

    if (screenTimer.active)
    {
      uint8_t lvl = lv_slider_get_value(ui_brightnessSlider);
      screenBrightness(lvl);

      if (screenTimer.duration < 0)
      {
        Timber.w("Always On active");
        screenTimer.active = false;
      }
      else if (watch.isCameraReady() || gameActive)
      {
        screenTimer.active = false;
      }
      else if (!staying_on_for_charging() && screenTimer.time + screenTimer.duration < millis())
      {
        Timber.w("Screen timeout");
        screenTimer.active = false;

        screenBrightness(0);
        lv_screen_load(ui_home);

#if ESPS3_2_06
        // Step 4: sleep the touch chip too, gated the same way as the rest of Extreme
        // Power Save - only while the switch is on and genuinely on battery.
        // ESPS3_2_06-only - see my_touchpad_read() for why this isn't extended to
        // ESPS3_1_75 (no-op wakeup()/sleep() on that board's touch driver).
        if (extremePowerSave && on_battery())
        {
          tft.touch.sleep();
          touchAsleep = true;
        }
#endif

#if ESPS3_2_06
        // Step 5: stop BLE entirely too - clearAll=true (the default) is
        // correct here since begin() below will recreate the server/
        // advertising objects from scratch; bonding/pairing info lives in
        // the NimBLE host's own store, untouched by deinit either way, so
        // the phone's existing pairing should survive this cycle.
        // Deliberately NOT extended to ESPS3_1_75: the user relies on this watch
        // for real-time motorcycle navigation, and BLE needing to reconnect after
        // being stopped added noticeable lag right when navigation data started
        // arriving - had to babysit the screen to keep it awake so BLE wouldn't
        // drop. Not worth the tradeoff for this board/use case, even though the
        // connect/disconnect crash itself (connectionCallback, see git history)
        // is now fixed regardless of whether this step is active.
        if (extremePowerSave && on_battery())
        {
          watch.stop(true);
          bleAsleep = true;
        }
#endif
      }
    }

#if ESPS3_1_75
    // ESPS3_1_75's own, much longer BLE-sleep trigger - independent of screenTimer.active
    // (which goes false once the screen has already timed out, so it can't live inside
    // that block above). Only actually stops BLE after a full hour of true inactivity;
    // any touch/button press resets lastActivityMs via screen_on(), and navigation being
    // active is already exempt via gameActive. See lastActivityMs's declaration for why
    // this exists instead of just extending ESPS3_2_06's step 5 to this board.
    if (extremePowerSave && on_battery() && !bleAsleep && !gameActive &&
        millis() - lastActivityMs >= BLE_SLEEP_INACTIVITY_MS)
    {
      Timber.w("Long inactivity - stopping BLE");
      watch.stop(true);
      bleAsleep = true;
    }
#endif
  }

#ifdef ENABLE_CUSTOM_FACE
  if (writeFile && transfer)
  {
    if (start)
    {
      screenBrightness(200);
      tft.fillScreen(TFT_BLUE);

      tft.drawRoundRect(70, 120, 100, 20, 5, TFT_WHITE);
    }

    writeFile = false;

    File file = FLASH.open(fName, start ? FILE_WRITE : FILE_APPEND);
    if (file)
    {

      if (!wSwitch)
      {
        file.write(buf1, wLen1);
      }
      else
      {
        file.write(buf2, wLen2);
      }

      file.close();

      // Serial.print(last ? "Complete: " :  "");
      // Serial.print("Receieved ");
      // Serial.print(currentRecv);
      // Serial.print("/");
      // Serial.print(total);
      // Serial.print("   ");
      // Serial.println(hexString(cmd, 5, true, "-"));

      if (total > 0)
      {
        int progress = (100 * currentRecv) / total;

        // Serial.println(String(progress, 2) + "%");
        tft.setTextColor(TFT_WHITE, TFT_BLUE);
        tft.setTextSize(2);
        tft.setCursor(80, 80);
        tft.print(progress);
        tft.print("%");

        tft.fillRoundRect(70, 120, progress, 20, 5, TFT_WHITE);
      }

      if (last)
      {
        // the file transfer has ended
        transfer = false;

        tft.setTextColor(TFT_WHITE, TFT_BLUE);
        tft.setTextSize(2);
        tft.setCursor(60, 80);
        tft.print("Processing");

        parseDial(fName.c_str(), true); // process the file
      }
    }
    else
    {
      Serial.println("- failed to open file for writing");

      transfer = false;

      ESP.restart();
    }
  }

#endif
}

bool isDay()
{
  return watch.getHour(true) > 7 && watch.getHour(true) < 21;
}

void update_faces()
{
  int second = watch.getSecond();
  int minute = watch.getMinute();
  int hour = watch.getHourC();
  bool mode = watch.is24Hour();
  bool am = watch.getHour(true) < 12;
  int day = watch.getDay();
  int month = watch.getMonth() + 1;
  int year = watch.getYear();
  int weekday = watch.getDayofWeek();

  int temp = watch.getWeatherAt(0).temp;
  int icon = watch.getWeatherAt(0).icon;

#if ESPS3_2_06 || ESPS3_1_75
  int battery = watchBatteryPercent;
#else
  int battery = watch.getPhoneBattery();
#endif
  bool connection = watch.isConnected();

  int steps = 2735;
  int distance = 17;
  int kcal = 348;
  int bpm = 76;
  int oxygen = 97;

  if (ui_home == face_custom_root)
  {
    update_time_custom(second, minute, hour, mode, am, day, month, year, weekday);
  }
  else
  {

    ui_update_watchfaces(second, minute, hour, mode, am, day, month, year, weekday,
                         temp, icon, battery, connection, steps, distance, kcal, bpm, oxygen);
  }
}

bool readDialBytes(const char *path, uint8_t *data, size_t offset, size_t size)
{
  File file = FLASH.open(path, "r");
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return false;
  }

  if (!file.seek(offset))
  {
    Serial.println("Failed to seek file");
    file.close();
    return false;
  }

  int bytesRead = file.readBytes((char *)data, size);

  if (bytesRead <= 0)
  {
    Serial.println("Error reading file");
    file.close();
    return false;
  }

  file.close();
  return true;
}

bool isKnown(uint8_t id)
{
  if (id < 0x1E)
  {
    if (id != 0x04 || id != 0x05 || id != 0x12 || id != 0x18 || id != 0x20)
    {
      return true;
    }
  }
  else
  {
    if (id == 0xFA || id == 0xFD)
    {
      return true;
    }
  }
  return false;
}

String hexString(uint8_t *arr, size_t len, bool caps, String separator)
{
  String hexString = "";
  for (size_t i = 0; i < len; i++)
  {
    char hex[3];
    sprintf(hex, caps ? "%02X" : "%02x", arr[i]);
    hexString += separator;
    hexString += hex;
  }
  return hexString;
}

String longHexString(unsigned long l)
{
  char buffer[9];             // Assuming a 32-bit long, which requires 8 characters for hex representation and 1 for null terminator
  sprintf(buffer, "%08x", l); // Format as 8-digit hex with leading zeros
  return String(buffer);
}

void parseDial(const char *path, bool restart)
{

#ifdef ENABLE_CUSTOM_FACE
  String name = longHexString(watch.getEpoch());

  Serial.print("Parsing dial:");
  Serial.println(path);

  JsonDocument json;
  JsonDocument elements;
  JsonDocument assetFiles;
  JsonArray elArray = elements.to<JsonArray>();
  JsonArray assetArray = assetFiles.to<JsonArray>();

  json["name"] = name;
  json["file"] = String(path);

  JsonDocument rsc;
  int errors = 0;

  uint8_t az[1];
  if (!readDialBytes(path, az, 0, 1))
  {
    Serial.println("Failed to read watchface header");
    errors++;
  }
  uint8_t j = az[0];

  static uint8_t item[20];
  static uint8_t table[512];

  uint8_t lid = 0;
  int a = 0;
  int lan = 0;
  int tp = 0;
  int wt = 0;

  for (int i = 0; i < j; i++)
  {
    if (i >= 60)
    {
      Serial.println("Too many watchface elements >= 60");
      break;
    }

    JsonDocument element;

    if (!readDialBytes(path, item, (i * 20) + 4, 20))
    {
      Serial.println("Failed to read element properties");
      errors++;
    }

    uint8_t id = item[0];

    element["id"] = id;

    uint16_t xOff = item[5] * 256 + item[4];
    uint16_t yOff = item[7] * 256 + item[6];

    element["x"] = xOff;
    element["y"] = yOff;

    uint16_t xSz = item[9] * 256 + item[8];
    uint16_t ySz = item[11] * 256 + item[10];

    uint32_t clt = item[15] * 256 * 256 * 258 + item[14] * 256 * 256 + item[13] * 256 + item[12];
    uint32_t dat = item[19] * 256 * 256 * 256 + item[18] * 256 * 256 + item[17] * 256 + item[16];

    uint8_t id2 = item[1];

    bool isG = (item[1] & 0x80) == 0x80;

    if (id == 0x08)
    {
      isG = true;
    }

    uint8_t cmp = isG ? (item[1] & 0x7F) : 1;

    int aOff = item[2];

    bool isM = (item[3] & 0x80) == 0x80;
    uint8_t cG = isM ? (item[3] & 0x7F) : 1;

    if (!isKnown(id))
    {
      continue;
    }

    if (id == 0x16 && (item[1] == 0x06 || item[1] == 0x00))
    {
      // weather (-) label
      continue;
    }
    if (isM)
    {
      lan++;
    }

    if (tp == 0x09 && id == 0x09)
    {
      a++;
    }
    else if (tp != id)
    {
      tp = id;
      a++;
    }
    else if (lan == 1)
    {
      a++;
    }

    if (xSz == 0 || ySz == 0)
    {
      continue;
    }

    int z = i;
    int rs = -1;

    bool createFile = false;

    if (rsc.containsKey(String(clt)))
    {
      z = rsc[String(clt)].as<int>();
      rs = z;
    }

    bool drawable = (id == 0x0d) ? (lan == 1 || lan == 17 || lan == 33) : true;

    JsonDocument grp;
    JsonArray grpArr = grp.to<JsonArray>();

    if (rs == -1 && drawable)
    {
      rsc[String(clt)] = i;

      for (int aa = 0; aa < cmp; aa++)
      {
        unsigned long nm = (i * 10000) + (clt * 10) + aa;
        grpArr.add("S:" + name + "_" + longHexString(nm) + ".bin");
      }

      if (id == 0x17)
      {
      }
      else if (id == 0x0A)
      {
      }
      else if (cmp == 1)
      {
      }
      else
      {
      }

      // save asset
      createFile = true;
    }
    else if (id == 0x16 && id2 == 0x00)
    {
      // save asset
      createFile = true;

      for (int aa = 0; aa < cmp; aa++)
      {
        unsigned long nm = (z * 10000) + (clt * 10) + aa;
        grpArr.add("S:" + name + "_" + longHexString(nm) + ".bin");
      }
    }
    else
    {

      for (int aa = 0; aa < cmp; aa++)
      {
        unsigned long nm = (z * 10000) + (clt * 10) + aa;
        grpArr.add("S:" + name + "_" + longHexString(nm) + ".bin");
      }
    }

    if (cmp <= 1)
    {
      // grp is null
      grpArr.clear();
    }

    if (id == 0x0A)
    {
      // if (connIC.count { it == '\n' } < 3) {
      //     continue
      // }
    }

    if (isM)
    {
      if (lan == cG)
      {
        lan = 0;
      }
      else if (id == 0x0d && (lan == 1 || lan == 32 || lan == 40 || lan == 17 || lan == 33))
      {
        yOff -= (ySz - aOff);
        xOff -= aOff;
      }
      else
      {
        continue;
      }
    }
    if (id == 0x17)
    {
      wt++;
      if (wt != 1)
      {
        continue;
      }
    }

    if (id == 0x16 && id2 == 0x06)
    {
      continue;
    }

    if (drawable)
    {
      element["pvX"] = aOff;
      element["pvY"] = ySz - aOff;

      unsigned long nm = (z * 10000) + (clt * 10) + 0;

      element["image"] = "S:" + name + "_" + longHexString(nm) + ".bin";
      element["group"] = grpArr;

      elArray.add(element);
    }

    Serial.printf("i:%d, id:%d, xOff:%d, yOff:%d, xSz:%d, ySz:%d, clt:%d, dat:%d, cmp:%d\n", i, id, xOff, yOff, xSz, ySz, clt, dat, cmp);

    if (!createFile)
    {
      continue;
    }
    uint8_t cf = (id == 0x09 && i == 0) || (id == 0x19) ? 0x12 : 0x13;
    bool tr = cf == 0x13;
    uint16_t st = (cf == 0x12) ? 2 : 3;

    for (int b = 0; b < cmp; b++)
    {
      unsigned long nm = (z * 10000) + (clt * 10) + b;

      String asset = "/" + name + "_" + longHexString(nm) + ".bin";
      Serial.print("Create asset-> ");
      Serial.print(asset);

      assetArray.add(asset);

      uint8_t header[12];

      lvImgHeader(header, cf, xSz, ySz / cmp, xSz * st);

      Serial.print("\t");
      Serial.println(hexString(header, 12));

      File ast = FLASH.open(asset.c_str(), FILE_WRITE);
      if (ast)
      {
        ast.write(header, 12);

        if (!readDialBytes(path, table, clt, 512))
        {
          Serial.println("Could not read color table bytes from file");
          errors++;
          break;
        }

        uint16_t yZ = uint16_t(ySz / cmp); // height of individual element

        File file = FLASH.open(path, "r");
        if (!file)
        {
          Serial.println("Failed to open file for reading");
          errors++;
          break;
        }
        int offset = (xSz * yZ) * b;

        if (!file.seek(dat + offset))
        {
          Serial.println("Failed to seek file");
          file.close();
          errors++;
          break;
        }

        int x = 0;
        if (id == 0x19)
        {
          for (int z = 0; z < (xSz * yZ); z++)
          {
            uint8_t pixel[2];
            pixel[0] = item[13];
            pixel[1] = item[12];
            ast.write(pixel, 2);
          }
        }
        else
        {
          while (file.available())
          {
            uint16_t index = file.read();

            uint8_t pixel[3];
            if (tr)
            {
              pixel[1] = table[index * 2];
              pixel[2] = table[(index * 2) + 1];
              pixel[0] = (uint16_t(pixel[1] * 256 + pixel[2]) == 0) ? 0x00 : 0xFF; // alpha byte (black pixel [0] is transparent)
              ast.write(pixel, 3);
            }
            else
            {
              pixel[0] = table[index * 2];
              pixel[1] = table[(index * 2) + 1];

              ast.write(pixel, 2);
            }
            x++;
            if (x >= (xSz * yZ))
            {
              break;
            }
          }
        }
        file.close();

        ast.close();
      }
      else
      {
        errors++;
      }
    }
  }

  json["elements"] = elements;
  json["assets"] = assetFiles;

  serializeJsonPretty(json, Serial);

  String jsnFile = "/" + name + ".json";
  assetArray.add(jsnFile);
  File jsn = FLASH.open(jsnFile, FILE_WRITE);

  if (jsn)
  {
    serializeJsonPretty(json, jsn);
    jsn.flush();
    jsn.close();
  }
  else
  {
    errors++;
  }

  if (errors > 0)
  {
    // failed to parse watchface files
    // probably delete assetfiles
    Serial.print(errors);
    Serial.println(" errors encountered when parsing watchface");
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(80, 80);
    tft.print("Failed");
  }
  else
  {
    // success
    // probably delete source file

    tft.fillScreen(TFT_GREEN);
    tft.setTextColor(TFT_WHITE, TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(80, 80);
    tft.print("Success");

    deleteFile(path);
    Serial.println("Watchface parsed successfully");

    prefs.putString("custom", jsnFile);
  }

  if (restart)
  {
    delay(500);
    ESP.restart();
  }

#endif
}

bool lvImgHeader(uint8_t *byteArray, uint8_t cf, uint16_t w, uint16_t h, uint16_t stride)
{

  byteArray[0] = LV_IMAGE_HEADER_MAGIC;
  byteArray[1] = cf;
  byteArray[2] = 0;
  byteArray[3] = 0;

  byteArray[4] = (w & 0xFF);
  byteArray[5] = (w >> 8) & 0xFF;

  byteArray[6] = (h & 0xFF);
  byteArray[7] = (h >> 8) & 0xFF;

  byteArray[8] = (stride & 0xFF);
  byteArray[9] = (stride >> 8) & 0xFF;

  byteArray[10] = 0;
  byteArray[11] = 0;

  return true;
}
