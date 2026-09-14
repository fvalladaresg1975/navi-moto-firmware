/*
    Last-trip statistics.

    Deliberately derives everything from data the phone ALREADY sends for navigation, so
    it costs no extra battery on either side - no GPS tracking is involved. The trade is
    that the distance is an estimate rather than a measurement; get_last_trip() in the HAL
    documents exactly how it is accumulated and where it can be off.
*/

#include "trip.h"
#include "../../hal/esp32/displays/pins.h"

#ifdef ENABLE_APP_TRIP

REGISTER_APP("Viaje", &ui_img_arrow_png, ui_tripScreen, ui_tripScreen_screen_init);

static lv_obj_t *ui_tripDistance;
static lv_obj_t *ui_tripDuration;
static lv_obj_t *ui_tripSpeed;
static lv_obj_t *ui_tripNote;

/* 20 is the largest size with accented characters (src/fonts_es); LVGL's built-in 24/30
   are ASCII-only. Fine here - these strings carry no accents, but keeping to the same
   family avoids a surprise if the wording ever changes. */
static void refresh_trip_values(void)
{
    long meters = 0;
    unsigned long seconds = 0;
    get_last_trip(&meters, &seconds);

    if (seconds == 0 && meters == 0)
    {
        lv_label_set_text(ui_tripDistance, "--");
        lv_label_set_text(ui_tripDuration, "Sin viajes aun");
        lv_label_set_text(ui_tripSpeed, "");
        return;
    }

    if (meters >= 1000)
    {
        lv_label_set_text_fmt(ui_tripDistance, "%ld,%ld km", meters / 1000, (meters % 1000) / 100);
    }
    else
    {
        lv_label_set_text_fmt(ui_tripDistance, "%ld m", meters);
    }

    unsigned long h = seconds / 3600UL;
    unsigned long m = (seconds % 3600UL) / 60UL;
    if (h > 0)
    {
        lv_label_set_text_fmt(ui_tripDuration, "%luh %02lum", h, m);
    }
    else
    {
        lv_label_set_text_fmt(ui_tripDuration, "%lu min", m);
    }

    /* Average only - a peak speed would need continuous GPS sampling, which this app
       exists precisely to avoid. */
    if (seconds > 0)
    {
        long kmh = (meters * 36L) / (long)(seconds * 10UL);
        lv_label_set_text_fmt(ui_tripSpeed, "Promedio %ld km/h", kmh);
    }
    else
    {
        lv_label_set_text(ui_tripSpeed, "");
    }
}

void ui_event_tripScreen(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_SCREEN_LOADED)
    {
        /* Read on open rather than keeping the screen live: a trip only changes when
           navigation ends, so polling it would be pure waste. */
        refresh_trip_values();
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT)
    {
        ui_app_exit();
    }
}

void ui_tripScreen_screen_init()
{
    ui_tripScreen = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_tripScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_tripScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_tripScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(ui_tripScreen);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, UI_SCALE_PX(38));
    lv_label_set_text(title, "Ultimo viaje");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Distance is the headline number, so it gets the big font. */
    ui_tripDistance = lv_label_create(ui_tripScreen);
    lv_obj_set_align(ui_tripDistance, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_tripDistance, UI_SCALE_PX(-24));
    lv_label_set_text(ui_tripDistance, "--");
    lv_obj_set_style_text_font(ui_tripDistance, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_tripDuration = lv_label_create(ui_tripScreen);
    lv_obj_set_align(ui_tripDuration, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_tripDuration, UI_SCALE_PX(6));
    lv_label_set_text(ui_tripDuration, "Sin viajes aun");
    lv_obj_set_style_text_font(ui_tripDuration, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_tripSpeed = lv_label_create(ui_tripScreen);
    lv_obj_set_align(ui_tripSpeed, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_tripSpeed, UI_SCALE_PX(30));
    lv_label_set_text(ui_tripSpeed, "");
    lv_obj_set_style_text_font(ui_tripSpeed, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* States plainly that this is an estimate. The numbers come from watching the
       remaining-distance figure drop, not from measuring the road travelled, and the user
       should not read them as exact. */
    ui_tripNote = lv_label_create(ui_tripScreen);
    lv_obj_set_width(ui_tripNote, UI_SCALE_PX(170));
    lv_obj_set_align(ui_tripNote, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(ui_tripNote, UI_SCALE_PX(-40));
    lv_label_set_long_mode(ui_tripNote, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(ui_tripNote, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_tripNote, "Estimado desde la navegacion");
    lv_obj_set_style_text_font(ui_tripNote, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_tripScreen, ui_event_tripScreen, LV_EVENT_ALL, NULL);

    refresh_trip_values();
}

#endif
