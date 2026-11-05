/*
   Navi Moto: low battery warning and power off.

   The AXP2101 is read every BATT_POLL_MS, screen on or off. Nothing happens on the charger
   or with no battery. A level only counts after BATT_CONFIRM readings in a row, so a
   short dip under load (or a jump of the gauge) never acts on its own.

     BATT_WARN_PCT   a warning, once. A small banner while navigating, so the next turn
                     stays readable, a panel with a button otherwise.
     BATT_OFF_PCT    a BATT_COUNTDOWN_S countdown to power off, with a button to stay on.
                     While navigating only a banner: losing the directions all of a sudden
                     is worse than spending what is left. The countdown starts when the
                     navigation ends, unless the rider already chose to stay on.
     BATT_CUT_MV     the battery is really empty: power off after BATT_OFF_SCREEN_MS,
                     navigating or not, and no button. The percentage is only an estimate
                     near the end, so this one goes by the voltage.

   A crash in progress comes first: nothing here shows or powers off until it is over, so
   the SOS is never cut. The board comes back with the PWR button or the charger.
*/

#include <Arduino.h>
#include <lvgl.h>
#include "navi_battery.h"
#include "navi_crash.h"
#include "ui/ui.h"
#include "common/navi_face.h"

void screen_on(long extra);
bool on_battery();
bool boardBatteryRead(int *percent, int *millivolts);
bool boardNavigating(void);
void boardPowerOff(void);

#define BATT_POLL_MS 10000UL
#define BATT_CONFIRM 3
#define BATT_WARN_PCT 15
#define BATT_OFF_PCT 5
#define BATT_REARM_PCT 20 // the gauge climbed back here without a charger: warn again next time
#define BATT_CUT_MV 3300
#define BATT_MIN_MV 2500 // below this it is not a reading (the ADC off, a failed read)
#define BATT_COUNTDOWN_S 30
#define BATT_OFF_SCREEN_MS 5000UL
#define BATT_PANEL_MS 20000UL  // an unanswered warning closes by itself
#define BATT_BANNER_MS 6000UL
#define BATT_LOG_MS 60000UL

#define BATT_COLOR 0xC25E00 // orange: red is the crash panel
#define BATT_COLOR_PULSE 0xE07000

enum
{
  BT_IDLE,      // nothing on screen
  BT_WARNING,   // the panel with the warning
  BT_COUNTDOWN, // the panel counting down
  BT_OFF        // the last panel, then power off
};

static int state = BT_IDLE;
static bool warned = false;   // BATT_WARN_PCT already shown
static bool lowNoted = false; // BATT_OFF_PCT banner already shown
static bool stayOn = false;   // the rider chose to stay on: only BATT_CUT_MV is left
static uint8_t warnReads = 0, lowReads = 0, cutReads = 0;
static volatile bool buttonPending = false;
static unsigned long lastPoll = 0, lastLog = 0;
static unsigned long stateAt = 0, bannerAt = 0;
static int shownLeft = -1;

/* ---- Screen ---- */

static lv_obj_t *panel = NULL, *banner = NULL;
static lv_obj_t *textLabel, *countLabel, *button, *buttonLabel;

static void onButton(lv_event_t *e);

static void panelClose(void)
{
  if (panel)
  {
    lv_obj_delete(panel);
    panel = NULL;
  }
  state = BT_IDLE;
}

static void panelOpen(const char *title, const char *text, const char *buttonText)
{
  panelClose();
  panel = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(BATT_COLOR), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE); // swallow touches meant for the screen below
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *titleLabel = lv_label_create(panel);
  lv_obj_set_style_text_font(titleLabel, &nm_text_28, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 50);
  lv_label_set_text(titleLabel, title);

  textLabel = lv_label_create(panel);
  lv_obj_set_width(textLabel, 340);
  lv_label_set_long_mode(textLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(textLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(textLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(textLabel, lv_color_white(), 0);
  lv_obj_align(textLabel, LV_ALIGN_TOP_MID, 0, 95);
  lv_label_set_text(textLabel, text);

  countLabel = lv_label_create(panel);
  lv_obj_set_style_text_font(countLabel, &nm_digits_170, 0);
  lv_obj_set_style_text_color(countLabel, lv_color_white(), 0);
  lv_obj_align(countLabel, LV_ALIGN_CENTER, 0, 10);
  lv_label_set_text(countLabel, "");

  if (buttonText)
  {
    button = lv_obj_create(panel);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 300, 96);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_radius(button, 48, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x444444), 0);
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
    lv_label_set_text(buttonLabel, buttonText);
  }
  stateAt = millis();
  screen_on(0);
}

static void bannerClose(void)
{
  if (banner)
  {
    lv_obj_delete(banner);
    banner = NULL;
  }
}

// Low on the screen and gone in seconds: the turn box and the speed stay in view.
static void bannerOpen(const char *text)
{
  bannerClose();
  banner = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(banner);
  lv_obj_set_size(banner, 260, 48);
  lv_obj_align(banner, LV_ALIGN_BOTTOM_MID, 0, -32);
  lv_obj_set_style_radius(banner, 24, 0);
  lv_obj_set_style_bg_color(banner, lv_color_hex(BATT_COLOR), 0);
  lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
  lv_obj_remove_flag(banner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(banner, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = lv_label_create(banner);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_center(label);
  lv_label_set_text(label, text);
  bannerAt = millis();
  screen_on(0);
}

/* ---- Flow ---- */

static void startWarning(int percent, bool navigating)
{
  char buf[48];
  warned = true;
  Serial.printf("[BATT] warning at %d%%\n", percent);
  if (navigating)
  {
    snprintf(buf, sizeof(buf), "Batería baja: %d %%", percent);
    bannerOpen(buf);
    return;
  }
  snprintf(buf, sizeof(buf), "Queda %d %% de batería", percent);
  panelOpen("Batería baja", buf, "Entendido");
  state = BT_WARNING;
}

static void startCountdown(int percent)
{
  char buf[48];
  snprintf(buf, sizeof(buf), "Queda %d %%. La placa se apagará", percent);
  panelOpen("Batería muy baja", buf, "Seguir encendida");
  state = BT_COUNTDOWN;
  shownLeft = -1;
  Serial.printf("[BATT] countdown at %d%%\n", percent);
}

static void startOff(void)
{
  bannerClose();
  panelOpen("Batería agotada", "Apagando la placa...", NULL);
  state = BT_OFF;
  Serial.println("[BATT] powering off");
}

// The panel goes away in batteryLoop(), not from inside its own button's event.
static void onButton(lv_event_t *e)
{
  buttonPending = true;
}

// A charger put everything back to normal, whatever was going on.
static void rearm(void)
{
  bannerClose();
  panelClose();
  warned = lowNoted = stayOn = false;
  warnReads = lowReads = cutReads = 0;
}

static bool confirmed(uint8_t *reads, bool low)
{
  if (!low)
    *reads = 0;
  else if (*reads < BATT_CONFIRM)
    (*reads)++;
  return *reads >= BATT_CONFIRM;
}

static void poll(unsigned long now)
{
  int percent, millivolts;
  if (!boardBatteryRead(&percent, &millivolts) || !on_battery())
  {
    if (warned || lowNoted || stayOn || panel)
      Serial.println("[BATT] on the charger");
    rearm();
    return;
  }

  if (percent <= BATT_REARM_PCT && now - lastLog >= BATT_LOG_MS)
  {
    lastLog = now;
    Serial.printf("[BATT] %d%% %d mV\n", percent, millivolts);
  }
  if (percent >= BATT_REARM_PCT && !panel)
    warned = lowNoted = stayOn = false;

  bool warn = confirmed(&warnReads, percent <= BATT_WARN_PCT);
  bool low = confirmed(&lowReads, percent <= BATT_OFF_PCT);
  bool cut = confirmed(&cutReads, millivolts >= BATT_MIN_MV && millivolts < BATT_CUT_MV);
  bool navigating = boardNavigating();

  if (cut)
  {
    Serial.printf("[BATT] empty: %d mV\n", millivolts);
    startOff();
  }
  else if (low && navigating)
  {
    if (state == BT_COUNTDOWN)
      panelClose();
    if (!lowNoted)
    {
      char buf[48];
      lowNoted = true;
      snprintf(buf, sizeof(buf), "Batería muy baja: %d %%", percent);
      bannerOpen(buf);
    }
  }
  else if (low && !stayOn && state != BT_COUNTDOWN)
  {
    startCountdown(percent);
  }
  else if (warn && !low && !warned)
  {
    startWarning(percent, navigating);
  }
}

void batteryLoop(void)
{
  unsigned long now = millis();

  if (crashBusy())
  {
    if (state == BT_COUNTDOWN || state == BT_OFF || state == BT_WARNING)
      panelClose();
    bannerClose();
    warnReads = lowReads = cutReads = 0;
    return;
  }

  if (buttonPending)
  {
    buttonPending = false;
    if (state == BT_COUNTDOWN)
    {
      stayOn = true;
      Serial.println("[BATT] staying on");
    }
    panelClose();
  }

  if (banner && now - bannerAt >= BATT_BANNER_MS)
    bannerClose();

  if (state == BT_WARNING && now - stateAt >= BATT_PANEL_MS)
    panelClose();

  if (state == BT_COUNTDOWN)
  {
    int left = BATT_COUNTDOWN_S - (int)((now - stateAt) / 1000);
    if (left <= 0)
    {
      startOff();
    }
    else if (left != shownLeft)
    {
      shownLeft = left;
      lv_label_set_text_fmt(countLabel, "%d", left);
      lv_obj_set_style_bg_color(panel, lv_color_hex(left % 2 ? BATT_COLOR : BATT_COLOR_PULSE), 0);
      screen_on(0);
      if (!on_battery())
        rearm();
    }
  }

  if (state == BT_OFF)
  {
    if (!on_battery())
    {
      rearm();
    }
    else if (now - stateAt >= BATT_OFF_SCREEN_MS)
    {
      boardPowerOff();
      rearm(); // only reached if the PMU did not cut the power
    }
    return;
  }

  if (now - lastPoll >= BATT_POLL_MS)
  {
    lastPoll = now;
    poll(now);
  }
}
