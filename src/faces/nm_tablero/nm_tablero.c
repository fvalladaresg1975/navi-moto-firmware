/*
    Navi Moto face "Tablero" (466x466). See nm_tablero.h and src/common/navi_face.h.
*/

#include "nm_tablero.h"

#ifdef ENABLE_FACE_NM_TABLERO

#include <math.h>
#include <stdio.h>
#include "../../common/navi_face.h"
#include "../../ui/ui.h"

lv_obj_t *face_nm_tablero;

#define CX 233
#define CY 233
#define R 205
/* Dial: 0 at the bottom left (150 deg, LVGL/screen angles: 0 = 3 o'clock, clockwise),
   12 at the bottom right, over 240 degrees. */
#define A_START 150
#define A_SPAN 240

static lv_obj_t *sweep, *needle, *timeL, *dateL, *tempL, *wxIcon, *btBox, *btL, *tripL;
static lv_obj_t *fuel[5];
static lv_point_precise_t tickPts[13][2];
static lv_point_precise_t needlePts[2];
static int lastNeedleMin = -1;

static void polar(float deg, float r, lv_point_precise_t *p)
{
    float a = deg * 3.14159265f / 180.0f;
    p->x = CX + r * cosf(a);
    p->y = CY + r * sinf(a);
}

/* A full-screen line object, so its points are plain screen coordinates. */
static lv_obj_t *screen_line(lv_point_precise_t *pts, int width, uint32_t color)
{
    lv_obj_t *l = lv_line_create(face_nm_tablero);
    lv_obj_set_size(l, 466, 466);
    lv_obj_set_pos(l, 0, 0);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(l, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

#endif

void init_face_nm_tablero(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **))
{
#ifdef ENABLE_FACE_NM_TABLERO
    face_nm_tablero = nf_screen();

    // amber sweep from 0 to the current hour, under the ticks
    sweep = lv_arc_create(face_nm_tablero);
    lv_obj_set_size(sweep, 2 * (R - 6), 2 * (R - 6));
    lv_obj_center(sweep);
    lv_arc_set_rotation(sweep, A_START);
    lv_arc_set_bg_angles(sweep, 0, A_SPAN);
    lv_arc_set_range(sweep, 0, 720); // minutes over 12 h
    lv_arc_set_value(sweep, 0);
    lv_obj_remove_style(sweep, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(sweep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(sweep, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(sweep, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(sweep, 10, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sweep, lv_color_hex(NF_AMBER), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(sweep, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    for (int h = 0; h <= 12; h++)
    {
        float a = A_START + A_SPAN * h / 12.0f;
        bool major = h % 3 == 0;
        polar(a, R - (major ? 26 : 14), &tickPts[h][0]);
        polar(a, R, &tickPts[h][1]);
        screen_line(tickPts[h], major ? 5 : 3, h >= 10 ? NF_RED : NF_WHITE);
        if (major)
        {
            lv_point_precise_t p;
            polar(a, R - 50, &p);
            char s[4];
            snprintf(s, sizeof(s), "%d", h);
            lv_obj_t *l = nf_label(face_nm_tablero, &lv_font_montserrat_24, NF_GREY, 0, 0, s);
            lv_obj_align(l, LV_ALIGN_CENTER, (int)p.x - CX, (int)p.y - CY);
        }
    }

    btBox = lv_obj_create(face_nm_tablero);
    lv_obj_remove_style_all(btBox);
    lv_obj_set_size(btBox, 60, 46);
    lv_obj_align(btBox, LV_ALIGN_CENTER, 0, -98);
    lv_obj_set_style_radius(btBox, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btBox, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btBox, lv_color_hex(NF_DARK), LV_PART_MAIN | LV_STATE_DEFAULT);
    btL = nf_label(face_nm_tablero, &lv_font_montserrat_24, NF_DARK, 0, -98, "BT");

    needlePts[0].x = CX;
    needlePts[0].y = CY;
    needlePts[1] = needlePts[0];
    needle = screen_line(needlePts, 8, NF_AMBER);
    lv_obj_t *hub = lv_obj_create(face_nm_tablero);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 32, 32);
    lv_obj_center(hub);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(hub, lv_color_hex(NF_AMBER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(hub, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    timeL = nf_label(face_nm_tablero, &nm_time_86, NF_WHITE, 0, 64, "00:00");
    dateL = nf_label(face_nm_tablero, &lv_font_montserrat_20, NF_GREY, 0, 114, "");

    // fuel gauge = board battery
    nf_label(face_nm_tablero, &lv_font_montserrat_20, NF_GREY, -112, 150, "E");
    nf_label(face_nm_tablero, &lv_font_montserrat_20, NF_GREY, -34, 150, "F");
    for (int k = 0; k < 5; k++)
    {
        fuel[k] = lv_obj_create(face_nm_tablero);
        lv_obj_remove_style_all(fuel[k]);
        lv_obj_set_size(fuel[k], 9, 20);
        lv_obj_align(fuel[k], LV_ALIGN_CENTER, -96 + k * 12, 150);
        lv_obj_set_style_bg_opa(fuel[k], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(fuel[k], lv_color_hex(NF_DARK), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    tempL = nf_label(face_nm_tablero, &lv_font_montserrat_20, NF_BLUE, 52, 150, "--");
    wxIcon = lv_image_create(face_nm_tablero);
    lv_image_set_scale(wxIcon, 110);
    lv_obj_align(wxIcon, LV_ALIGN_CENTER, 108, 150);
    setWeatherIcon(wxIcon, 0, true);

    tripL = nf_label(face_nm_tablero, &lv_font_montserrat_20, NF_AMBER, 0, 190, "");

    nf_finish(face_nm_tablero);
    callback("Tablero", &face_nm_tablero_preview, &face_nm_tablero, NULL);
#endif
}

void update_check_nm_tablero(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                             int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen)
{
#ifdef ENABLE_FACE_NM_TABLERO
    if (root != face_nm_tablero || !face_nm_tablero)
        return;
    navi_face_data_t d;
    navi_face_get(&d);
    char buf[40];

    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    nf_set_text(timeL, buf);
    snprintf(buf, sizeof(buf), "%s %d %s", NF_WEEKDAYS_ABBR[d.weekday % 7], d.day, NF_MONTHS_ABBR[(d.month + 11) % 12]);
    nf_set_text(dateL, buf);

    // needle + sweep: once a minute
    int m12 = (d.hour % 12) * 60 + d.minute;
    if (m12 != lastNeedleMin)
    {
        lastNeedleMin = m12;
        polar(A_START + A_SPAN * m12 / 720.0f, R - 30, &needlePts[1]);
        lv_line_set_points(needle, needlePts, 2);
        lv_arc_set_value(sweep, m12);
    }

    uint32_t bt = d.connected ? NF_GREEN : NF_DARK;
    nf_set_border_color(btBox, lv_color_hex(bt));
    nf_set_text_color(btL, lv_color_hex(bt));

    int seg = d.boardPct < 0 ? 0 : (d.boardPct + 19) / 20; // 1-20 % -> 1 segment ... 81-100 % -> 5
    lv_color_t fc = nf_battery_color(d.boardPct, d.boardCharging);
    for (int k = 0; k < 5; k++)
        nf_set_bg_color(fuel[k], k < seg ? fc : lv_color_hex(NF_DARK));

    if (d.weatherKnown)
    {
        snprintf(buf, sizeof(buf), "%d°C", d.temp);
        nf_set_weather_icon(wxIcon, d.icon, d.hour > 7 && d.hour < 21);
    }
    else
        snprintf(buf, sizeof(buf), "--");
    nf_set_text(tempL, buf);

    if (d.tripMeters > 0)
    {
        char km[16];
        nf_format_km(km, sizeof(km), d.tripMeters);
        snprintf(buf, sizeof(buf), "TRIP %s", km);
    }
    else
        snprintf(buf, sizeof(buf), "TRIP --");
    nf_set_text(tripL, buf);
#endif
}
