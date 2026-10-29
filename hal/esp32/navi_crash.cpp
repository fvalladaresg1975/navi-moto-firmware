/*
   Navi Moto: crash detection (0xCC).

   The QMI8658 IMU (I2C, on the bus the touch panel, RTC and PMU share) is read from
   hal_loop() every CRASH_SAMPLE_MS. A crash is called only when two things happen in order:
     1. an impact: acceleration above CRASH_IMPACT_G, or the board turning faster than
        CRASH_SPIN_DPS. Potholes and kerbs give spikes too, so this alone means nothing;
     2. within CRASH_WINDOW_MS of it, the bike down: the board tilted more than
        CRASH_TILT_DEG from how it sat while riding, and nearly still, for CRASH_DOWN_MS.
   A slow drop while parked has no impact, and rough road never tilts the handlebar that
   far for seconds, so neither starts the countdown. "How it sat while riding" is a slow
   average of the direction of gravity (CRASH_REF_TAU_MS), frozen at the impact: it
   follows the mount angle and the small movements of the bars.

   Then a CRASH_COUNTDOWN_S countdown on a full-screen panel with a big "Estoy bien"
   button; the phone is told at the start (it sounds an alarm and can cancel too). At zero
   the board asks the phone to send the SOS, or as soon as the link is back if it is down
   (for up to CRASH_SOS_KEEP_MS).

   Armed only once the phone has switched it on in this boot (the switch and the contacts
   live in the app): without the phone nobody could be told.

   Phone -> watch:
     sub 0x00 [enabled u8]   configuration, sent on every connection
     sub 0x01                test: a short countdown, marked as a test
     sub 0x02                cancel (the "Estoy bien" on the phone's alarm)
     sub 0x03 [sent u8]      SOS result: how many contacts got the SMS (0 = none)
   Watch -> phone {AB 00 04 FF CC 80 lo}:
     0 cancelled, 1 send the SOS, 2 countdown started, 3 send the test message,
     4 test countdown started
*/

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <ChronosESP32.h>
#include <lvgl.h>
#include <SensorQMI8658.hpp>
#include "navi_crash.h"
#include "ui/ui.h"
#include "common/navi_face.h"
#include "displays/pins.h"

extern ChronosESP32 watch;
void screen_on(long extra);

#define CRASH_SAMPLE_MS 10
#define CRASH_IMPACT_G 3.5f        // |a|, in g (1 g at rest)
#define CRASH_SPIN_DPS 300.0f      // |rotation rate|, degrees per second
#define CRASH_WINDOW_MS 20000UL    // after the last impact, the bike must be down within this
#define CRASH_TILT_DEG 55.0f       // lying on its side is 70-90 deg; side stand + full lock < 35
#define CRASH_STILL_DPS 60.0f      // averaged rotation rate below this counts as "still"
#define CRASH_DOWN_MS 4000UL       // down and still this long
#define CRASH_REF_TAU_MS 10000.0f  // time constant of the riding orientation
#define CRASH_COUNTDOWN_S 30
#define CRASH_TEST_COUNTDOWN_S 10
#define CRASH_SOS_KEEP_MS 600000UL // a pending SOS is still worth sending this long
#define CRASH_ANSWER_MS 60000UL    // the phone answers the SOS within this (location + SMS)

enum
{
  CRASH_TELL_CANCELLED = 0,
  CRASH_TELL_SOS = 1,
  CRASH_TELL_COUNTDOWN = 2,
  CRASH_TELL_TEST_SOS = 3,
  CRASH_TELL_TEST_COUNTDOWN = 4
};

enum
{
  CR_IDLE,      // riding (or parked): following the riding orientation
  CR_IMPACT,    // impact seen, watching whether the bike stays down
  CR_COUNTDOWN, // panel up, counting down
  CR_SOS        // countdown over: asking the phone / showing its answer
};

static SensorQMI8658 qmi;
static bool imuOk = false;

static volatile bool crashEnabled = false;
static volatile bool crashTestPending = false;
static volatile bool crashCancelPending = false;
static volatile int crashResult = -1; // SOS answer from the phone, -1 = none yet

static int state = CR_IDLE;
static bool isTest = false;
static unsigned long lastSampleMs = 0;
static float lpX = 0, lpY = 0, lpZ = 1, gyroLp = 0; // gravity direction (~0.1 s), rotation (~0.2 s)
static float refX = 0, refY = 0, refZ = 1;
static bool refOk = false;
static unsigned long refSince = 0;
static unsigned long impactAt = 0, downSince = 0;
static unsigned long countdownAt = 0;
static int countdownS = CRASH_COUNTDOWN_S;
static int shownLeft = -1;
static unsigned long sosAt = 0, sosSentAt = 0;
static bool sosSent = false, sosAnswered = false, wasConnected = false, offlineShown = false;
static unsigned long lastSpikeLog = 0;

static void tell(uint8_t lo)
{
  uint8_t cmd[] = {0xAB, 0x00, 0x04, 0xFF, 0xCC, 0x80, lo};
  watch.sendCommand(cmd, 7);
}

void crashOnPacket(const uint8_t *data, int length)
{
  if (length < 7 || data[0] != 0xAB || data[4] != 0xCC)
    return;
  int n = length - 7; // payload, data[6] .. data[length - 2]
  switch (data[5])
  {
  case 0x00:
    if (n >= 1)
      crashEnabled = data[6] != 0;
    break;
  case 0x01:
    crashTestPending = true;
    break;
  case 0x02:
    crashCancelPending = true;
    break;
  case 0x03:
    if (n >= 1)
      crashResult = data[6];
    break;
  }
}

void crashSetup(void)
{
  imuOk = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, TOUCH_SDA, TOUCH_SCL) ||
          qmi.begin(Wire, QMI8658_H_SLAVE_ADDRESS, TOUCH_SDA, TOUCH_SCL);
  if (!imuOk)
  {
    Serial.println("[CRASH] QMI8658 not found - crash detection off");
    return;
  }
  // 16 g / 1024 dps: a crash saturates anything smaller. LPF_MODE_3 (13 % of the ODR)
  // keeps short impact peaks that the default 2.66 % would flatten.
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_16G, SensorQMI8658::ACC_ODR_250Hz, SensorQMI8658::LPF_MODE_3);
  qmi.configGyroscope(SensorQMI8658::GYR_RANGE_1024DPS, SensorQMI8658::GYR_ODR_224_2Hz, SensorQMI8658::LPF_MODE_3);
  qmi.enableAccelerometer();
  qmi.enableGyroscope();
  Serial.printf("[CRASH] QMI8658 ready (id 0x%02X)\n", qmi.getChipID());
}

/* ---- Screen ---- */

static lv_obj_t *panel = NULL;
static lv_obj_t *titleLabel, *textLabel, *countLabel, *button, *buttonLabel;

static void onButton(lv_event_t *e);

static void panelOpen(void)
{
  if (panel)
    return;
  panel = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(0xB00020), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE); // swallow touches meant for the screen below
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  titleLabel = lv_label_create(panel);
  lv_obj_set_style_text_font(titleLabel, &nm_text_28, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 50);

  textLabel = lv_label_create(panel);
  lv_obj_set_width(textLabel, 340);
  lv_label_set_long_mode(textLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(textLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(textLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(textLabel, lv_color_white(), 0);
  lv_obj_align(textLabel, LV_ALIGN_TOP_MID, 0, 95);

  countLabel = lv_label_create(panel);
  lv_obj_set_style_text_font(countLabel, &nm_digits_170, 0);
  lv_obj_set_style_text_color(countLabel, lv_color_white(), 0);
  lv_obj_align(countLabel, LV_ALIGN_CENTER, 0, 10);

  button = lv_obj_create(panel);
  lv_obj_remove_style_all(button);
  lv_obj_set_size(button, 280, 96);
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_radius(button, 48, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(button, lv_color_white(), 0);
  lv_obj_set_style_border_width(button, 4, 0);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(button, onButton, LV_EVENT_CLICKED, NULL);

  buttonLabel = lv_label_create(button);
  lv_obj_set_style_text_font(buttonLabel, &nm_text_28, 0);
  lv_obj_set_style_text_color(buttonLabel, lv_color_white(), 0);
  lv_obj_center(buttonLabel);
}

static void panelClose(void)
{
  if (panel)
  {
    lv_obj_delete(panel);
    panel = NULL;
  }
}

static void panelButton(const char *text, uint32_t color)
{
  lv_label_set_text(buttonLabel, text);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
}

static void panelMessage(const char *text)
{
  lv_obj_add_flag(countLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(textLabel, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_text_font(textLabel, &nm_text_28, 0);
  lv_label_set_text(textLabel, text);
}

/* ---- Flow ---- */

static void backToIdle(void)
{
  panelClose();
  state = CR_IDLE;
  isTest = false;
  downSince = 0;
}

static void startCountdown(bool test)
{
  isTest = test;
  countdownS = test ? CRASH_TEST_COUNTDOWN_S : CRASH_COUNTDOWN_S;
  countdownAt = millis();
  shownLeft = -1;
  state = CR_COUNTDOWN;
  crashCancelPending = false;
  panelOpen();
  lv_label_set_text(titleLabel, test ? "Prueba de caída" : "¿Estás bien?");
  lv_label_set_text(textLabel, test ? "Así se ve si la placa detecta una caída"
                                    : "Posible caída detectada");
  panelButton("Estoy bien", 0x1E8E3E);
  screen_on(0);
  tell(test ? CRASH_TELL_TEST_COUNTDOWN : CRASH_TELL_COUNTDOWN);
  Serial.printf("[CRASH] countdown started%s\n", test ? " (test)" : "");
}

static void startSos(void)
{
  state = CR_SOS;
  sosAt = millis();
  sosSent = false;
  sosAnswered = false;
  offlineShown = false;
  crashResult = -1;
  lv_label_set_text(titleLabel, isTest ? "Prueba de caída" : "Pidiendo ayuda");
  panelMessage("Enviando tu ubicación a tus contactos de emergencia...");
  panelButton("Cerrar", 0x444444);
}

static void onButton(lv_event_t *e)
{
  if (state == CR_COUNTDOWN)
  {
    tell(CRASH_TELL_CANCELLED);
    Serial.println("[CRASH] cancelled on the board");
  }
  backToIdle();
}

static void sample(unsigned long now)
{
  float ax, ay, az, gx, gy, gz;
  if (!qmi.getAccelerometer(ax, ay, az))
    return;
  if (qmi.getGyroscope(gx, gy, gz) < 0)
    gx = gy = gz = 0;
  float a = sqrtf(ax * ax + ay * ay + az * az);
  float g = sqrtf(gx * gx + gy * gy + gz * gz);
  lpX += (ax - lpX) * 0.1f;
  lpY += (ay - lpY) * 0.1f;
  lpZ += (az - lpZ) * 0.1f;
  gyroLp += (g - gyroLp) * 0.05f;
  float lpN = sqrtf(lpX * lpX + lpY * lpY + lpZ * lpZ);
  if (lpN < 0.2f)
    return; // free fall or garbage: no direction to compare
  float ux = lpX / lpN, uy = lpY / lpN, uz = lpZ / lpN;

  if (!refOk)
  {
    if (refSince == 0)
      refSince = now;
    if (now - refSince > 1000) // let the filter settle first
    {
      refX = ux;
      refY = uy;
      refZ = uz;
      refOk = true;
    }
    return;
  }

  bool impact = a > CRASH_IMPACT_G || g > CRASH_SPIN_DPS;
  if (a > 2.5f && now - lastSpikeLog > 2000)
  {
    lastSpikeLog = now;
    Serial.printf("[CRASH] spike %.1f g, %.0f dps\n", a, g);
  }

  if (state == CR_IDLE)
  {
    if (a > 0.8f && a < 1.2f) // only while nothing but gravity is pulling
    {
      float k = CRASH_SAMPLE_MS / CRASH_REF_TAU_MS;
      refX += (ux - refX) * k;
      refY += (uy - refY) * k;
      refZ += (uz - refZ) * k;
      float n = sqrtf(refX * refX + refY * refY + refZ * refZ);
      refX /= n;
      refY /= n;
      refZ /= n;
    }
    if (impact && crashEnabled)
    {
      state = CR_IMPACT;
      impactAt = now;
      downSince = 0;
      Serial.printf("[CRASH] impact %.1f g, %.0f dps\n", a, g);
    }
    return;
  }

  if (state == CR_IMPACT)
  {
    if (!crashEnabled)
    {
      backToIdle();
      return;
    }
    if (impact)
      impactAt = now;
    float dot = ux * refX + uy * refY + uz * refZ;
    if (dot > 1)
      dot = 1;
    if (dot < -1)
      dot = -1;
    float tilt = acosf(dot) * 57.29578f;
    bool down = tilt > CRASH_TILT_DEG && gyroLp < CRASH_STILL_DPS;
    if (down)
    {
      if (downSince == 0)
      {
        downSince = now;
        Serial.printf("[CRASH] down, tilt %.0f deg\n", tilt);
      }
      else if (now - downSince >= CRASH_DOWN_MS)
      {
        startCountdown(false);
      }
    }
    else
    {
      downSince = 0;
      if (now - impactAt > CRASH_WINDOW_MS)
      {
        state = CR_IDLE;
        Serial.println("[CRASH] impact without a fall, back to normal");
      }
    }
  }
}

void crashLoop(void)
{
  unsigned long now = millis();

  if (crashTestPending)
  {
    crashTestPending = false;
    if (state == CR_IDLE || state == CR_IMPACT)
      startCountdown(true);
  }

  if (imuOk && now - lastSampleMs >= CRASH_SAMPLE_MS && state != CR_COUNTDOWN && state != CR_SOS)
  {
    lastSampleMs = now;
    sample(now);
  }

  if (state == CR_COUNTDOWN)
  {
    if (crashCancelPending)
    {
      crashCancelPending = false;
      Serial.println("[CRASH] cancelled from the phone");
      backToIdle();
      return;
    }
    int left = countdownS - (int)((now - countdownAt) / 1000);
    if (left <= 0)
    {
      startSos();
    }
    else if (left != shownLeft)
    {
      shownLeft = left;
      lv_label_set_text_fmt(countLabel, "%d", left);
      // Pulse: brighter red on even seconds, so it catches the eye from a distance.
      lv_obj_set_style_bg_color(panel, lv_color_hex(left % 2 ? 0xB00020 : 0xE0002A), 0);
      screen_on(0);
    }
  }

  if (state == CR_SOS)
  {
    bool connected = watch.isConnected();
    if (connected && !sosSent && !sosAnswered)
    {
      sosSent = true;
      sosSentAt = now;
      offlineShown = false;
      tell(isTest ? CRASH_TELL_TEST_SOS : CRASH_TELL_SOS);
      Serial.println("[CRASH] SOS asked to the phone");
      panelMessage("Enviando tu ubicación a tus contactos de emergencia...");
    }
    if (!connected && wasConnected && sosSent && !sosAnswered)
    {
      sosSent = false; // the answer can't come on this link any more: ask again on the next
    }
    if (!connected && !sosSent && !sosAnswered && !offlineShown && now - sosAt > 3000)
    {
      offlineShown = true;
      panelMessage("Sin conexión con el celular.\nSe enviará el aviso apenas vuelva.");
    }
    int r = crashResult;
    if (r >= 0 && !sosAnswered)
    {
      sosAnswered = true;
      crashResult = -1;
      char buf[96];
      if (r > 0)
        snprintf(buf, sizeof(buf), "Aviso enviado a %d contacto%s.", r, r == 1 ? "" : "s");
      else
        snprintf(buf, sizeof(buf), "El celular no pudo enviar el SMS.");
      panelMessage(buf);
      Serial.printf("[CRASH] phone answered: %d sent\n", r);
    }
    if (sosSent && !sosAnswered && now - sosSentAt > CRASH_ANSWER_MS)
    {
      sosAnswered = true;
      panelMessage("El celular no confirmó el envío.");
    }
    if (!sosAnswered && !sosSent && now - sosAt > CRASH_SOS_KEEP_MS)
    {
      sosAnswered = true;
      panelMessage("No se pudo avisar: el celular no volvió a conectarse.");
    }
    screen_on(0);
  }

  wasConnected = watch.isConnected();
}
