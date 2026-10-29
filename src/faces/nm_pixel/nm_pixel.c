/*
    Navi Moto face "Pixel Moto" (466x466). See nm_pixel.h and src/common/navi_face.h.
*/

#include "nm_pixel.h"

#ifdef ENABLE_FACE_NM_PIXEL

#include <stdio.h>
#include "../../common/navi_face.h"
#include "../../ui/ui.h"

LV_IMG_DECLARE(ui_img_navimoto_logo); // the idle navigation logo (white motorcycle)

lv_obj_t *face_nm_pixel;
static lv_obj_t *dot, *timeL, *dateL, *tripL;
static lv_obj_t *wxIcon, *wxVal, *windVal, *boardIcon, *boardVal, *phoneIcon, *phoneVal;

/* Tile centres, left to right, and their colour bars. */
static const int TX[4] = {-138, -46, 46, 138};
static const uint32_t TCOL[4] = {NF_BLUE, NF_CYAN, NF_GREEN, NF_YELLOW};

static void bar(int x, int y, uint32_t color)
{
    lv_obj_t *b = lv_obj_create(face_nm_pixel);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 76, 6);
    lv_obj_align(b, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_radius(b, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(b, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* Three staggered strokes: a wind mark (no wind glyph in the fonts). */
static void wind_mark(int x, int y)
{
    static const int len[3] = {26, 34, 20};
    static const int off[3] = {-4, 2, -7};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *s = lv_obj_create(face_nm_pixel);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, len[i], 4);
        lv_obj_align(s, LV_ALIGN_CENTER, x + off[i], y - 9 + i * 9);
        lv_obj_set_style_radius(s, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(s, lv_color_hex(NF_WHITE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

#endif

void init_face_nm_pixel(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **))
{
#ifdef ENABLE_FACE_NM_PIXEL
    face_nm_pixel = nf_screen();

    lv_obj_t *logo = lv_image_create(face_nm_pixel);
    lv_image_set_src(logo, &ui_img_navimoto_logo);
    lv_image_set_scale(logo, 150); // ~56x30
    lv_obj_align(logo, LV_ALIGN_CENTER, -8, -172);

    dot = lv_obj_create(face_nm_pixel);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_align(dot, LV_ALIGN_CENTER, 36, -172);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot, lv_color_hex(NF_DARK), LV_PART_MAIN | LV_STATE_DEFAULT);

    timeL = nf_label(face_nm_pixel, &nm_time_110, NF_WHITE, 0, -86, "00:00");
    dateL = nf_label(face_nm_pixel, &nm_text_28, NF_WHITE, 0, -12, "");

    for (int i = 0; i < 4; i++)
    {
        bar(TX[i], 28, TCOL[i]);
        bar(TX[i], 120, TCOL[i]);
    }
    wxIcon = lv_image_create(face_nm_pixel);
    lv_image_set_scale(wxIcon, 150);
    lv_obj_align(wxIcon, LV_ALIGN_CENTER, TX[0], 57);
    setWeatherIcon(wxIcon, 0, true);
    wind_mark(TX[1], 57);
    boardIcon = nf_label(face_nm_pixel, &lv_font_montserrat_24, NF_WHITE, TX[2], 57, LV_SYMBOL_BATTERY_FULL);
    phoneIcon = nf_label(face_nm_pixel, &lv_font_montserrat_24, NF_WHITE, TX[3], 57, LV_SYMBOL_CALL);
    wxVal = nf_label(face_nm_pixel, &lv_font_montserrat_20, NF_WHITE, TX[0], 93, "--");
    windVal = nf_label(face_nm_pixel, &lv_font_montserrat_20, NF_WHITE, TX[1], 93, "--");
    boardVal = nf_label(face_nm_pixel, &lv_font_montserrat_20, NF_WHITE, TX[2], 93, "--");
    phoneVal = nf_label(face_nm_pixel, &lv_font_montserrat_20, NF_WHITE, TX[3], 93, "--");

    tripL = nf_label(face_nm_pixel, &lv_font_montserrat_20, NF_GREY, 0, 158, "");

    nf_finish(face_nm_pixel);
    callback("Pixel Moto", &face_nm_pixel_preview, &face_nm_pixel, NULL);
#endif
}

void update_check_nm_pixel(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                           int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen)
{
#ifdef ENABLE_FACE_NM_PIXEL
    if (root != face_nm_pixel || !face_nm_pixel)
        return;
    navi_face_data_t d;
    navi_face_get(&d);
    char buf[48];

    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    nf_set_text(timeL, buf);
    snprintf(buf, sizeof(buf), "%s %d %s", NF_WEEKDAYS[d.weekday % 7], d.day, NF_MONTHS[(d.month + 11) % 12]);
    nf_set_text(dateL, buf);

    nf_set_bg_color(dot, lv_color_hex(d.connected ? NF_GREEN : NF_DARK));

    if (d.weatherKnown)
    {
        snprintf(buf, sizeof(buf), "%d°", d.temp);
        nf_set_weather_icon(wxIcon, d.icon, d.hour > 7 && d.hour < 21);
    }
    else
        snprintf(buf, sizeof(buf), "--");
    nf_set_text(wxVal, buf);

    if (d.windKmh >= 0)
        snprintf(buf, sizeof(buf), "%d km/h", d.windKmh);
    else
        snprintf(buf, sizeof(buf), "--");
    nf_set_text(windVal, buf);

    if (d.boardPct >= 0)
        snprintf(buf, sizeof(buf), "%d%%", d.boardPct);
    else
        snprintf(buf, sizeof(buf), "--");
    nf_set_text(boardVal, buf);
    nf_set_text(boardIcon, d.boardCharging ? LV_SYMBOL_CHARGE
                         : d.boardPct >= 90 ? LV_SYMBOL_BATTERY_FULL
                         : d.boardPct >= 65 ? LV_SYMBOL_BATTERY_3
                         : d.boardPct >= 40 ? LV_SYMBOL_BATTERY_2
                         : d.boardPct >= 15 ? LV_SYMBOL_BATTERY_1
                                            : LV_SYMBOL_BATTERY_EMPTY);

    if (d.phonePct >= 0)
        snprintf(buf, sizeof(buf), d.phoneCharging ? LV_SYMBOL_CHARGE "%d%%" : "%d%%", d.phonePct);
    else
        snprintf(buf, sizeof(buf), "--");
    nf_set_text(phoneVal, buf);

    if (d.tripMeters > 0)
    {
        char km[16];
        nf_format_km(km, sizeof(km), d.tripMeters);
        snprintf(buf, sizeof(buf), "Último viaje  %s · %lu min", km, (d.tripSeconds + 30) / 60);
    }
    else
        snprintf(buf, sizeof(buf), "Sin viajes registrados");
    nf_set_text(tripL, buf);
#endif
}
