/*
    Navi Moto face "Clima de ruta" (466x466). See nm_clima.h and src/common/navi_face.h.
*/

#include "nm_clima.h"

#ifdef ENABLE_FACE_NM_CLIMA

#include <stdio.h>
#include "../../common/navi_face.h"
#include "../../ui/ui.h"

lv_obj_t *face_nm_clima;
static lv_obj_t *arcBoard, *arcPhone, *timeL, *dateL, *wxIcon, *tempL, *degL, *rainL, *windL, *sunL;
static lv_obj_t *hHour[NAVI_FACE_HOURS], *hTemp[NAVI_FACE_HOURS], *hDot[NAVI_FACE_HOURS], *hRain[NAVI_FACE_HOURS];

static uint32_t rain_color(int r)
{
    if (r >= 50)
        return NF_BLUE;
    if (r >= 30)
        return NF_CYAN;
    return NF_GREY;
}

#endif

void init_face_nm_clima(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **))
{
#ifdef ENABLE_FACE_NM_CLIMA
    face_nm_clima = nf_screen();
    arcBoard = nf_edge_arc(face_nm_clima, 0, 8, 4);
    arcPhone = nf_edge_arc(face_nm_clima, 1, 8, 4);

    timeL = nf_label(face_nm_clima, &nm_time_86, NF_WHITE, 0, -146, "00:00");
    dateL = nf_label(face_nm_clima, &lv_font_montserrat_20, NF_GREY, 0, -88, "");
    // a long city name ends in "..." (LVGL cuts on whole UTF-8 characters) instead of
    // running into the round edge
    lv_obj_set_width(dateL, 320);
    lv_label_set_long_mode(dateL, LV_LABEL_LONG_DOT);

    wxIcon = lv_image_create(face_nm_clima);
    lv_image_set_scale(wxIcon, 330); // ~82 px
    lv_obj_align(wxIcon, LV_ALIGN_CENTER, -58, -28);
    setWeatherIcon(wxIcon, 0, true);
    tempL = nf_label(face_nm_clima, &lv_font_montserrat_48, NF_WHITE, 34, -28, "--");
    degL = nf_label(face_nm_clima, &nm_text_28, NF_WHITE, 0, 0, "°");
    lv_obj_align_to(degL, tempL, LV_ALIGN_OUT_RIGHT_TOP, 2, 0);

    for (int i = 0; i < NAVI_FACE_HOURS; i++)
    {
        int x = (i - 2) * 66;
        hHour[i] = nf_label(face_nm_clima, &lv_font_montserrat_18, NF_GREY, x, 30, "--");
        hTemp[i] = nf_label(face_nm_clima, &lv_font_montserrat_20, NF_WHITE, x, 58, "--");
        hDot[i] = lv_obj_create(face_nm_clima);
        lv_obj_remove_style_all(hDot[i]);
        lv_obj_set_size(hDot[i], 8, 8);
        lv_obj_align(hDot[i], LV_ALIGN_CENTER, x, 84);
        lv_obj_set_style_radius(hDot[i], LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(hDot[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(hDot[i], lv_color_hex(NF_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);
        hRain[i] = nf_label(face_nm_clima, &lv_font_montserrat_16, NF_GREY, x, 104, "--");
    }

    rainL = nf_label(face_nm_clima, &lv_font_montserrat_20, NF_CYAN, 0, 140, "");
    windL = nf_label(face_nm_clima, &lv_font_montserrat_18, NF_WHITE, 0, 168, "");
    sunL = nf_label(face_nm_clima, &lv_font_montserrat_18, NF_AMBER, 0, 194, "");

    nf_finish(face_nm_clima);
    callback("Clima de ruta", &face_nm_clima_preview, &face_nm_clima, NULL);
#endif
}

void update_check_nm_clima(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                           int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen)
{
#ifdef ENABLE_FACE_NM_CLIMA
    if (root != face_nm_clima || !face_nm_clima)
        return;
    navi_face_data_t d;
    navi_face_get(&d);
    char buf[48];

    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    nf_set_text(timeL, buf);
    if (d.city && d.city[0])
        snprintf(buf, sizeof(buf), "%s %d %s · %s", NF_WEEKDAYS_SHORT[d.weekday % 7], d.day,
                 NF_MONTHS_SHORT[(d.month + 11) % 12], d.city);
    else
        snprintf(buf, sizeof(buf), "%s %d %s", NF_WEEKDAYS[d.weekday % 7], d.day, NF_MONTHS[(d.month + 11) % 12]);
    nf_set_text(dateL, buf);

    nf_arc_battery(arcBoard, d.boardPct, d.boardCharging);
    nf_arc_battery(arcPhone, d.phonePct, d.phoneCharging);

    if (d.weatherKnown)
    {
        snprintf(buf, sizeof(buf), "%d", d.temp);
        nf_set_weather_icon(wxIcon, d.icon, d.hour > 7 && d.hour < 21);
    }
    else
        snprintf(buf, sizeof(buf), "--");
    nf_set_text(tempL, buf);
    lv_obj_align_to(degL, tempL, LV_ALIGN_OUT_RIGHT_TOP, 2, 0);

    int rainFrom = -1; // first hour of the five with >= 50 % chance of rain
    for (int i = 0; i < NAVI_FACE_HOURS; i++)
    {
        snprintf(buf, sizeof(buf), "%02d", d.hourHour[i]);
        nf_set_text(hHour[i], buf);
        if (d.weatherKnown)
            snprintf(buf, sizeof(buf), "%d°", d.hourTemp[i]);
        else
            snprintf(buf, sizeof(buf), "--");
        nf_set_text(hTemp[i], buf);
        int r = d.hourRain[i];
        if (r >= 0)
            snprintf(buf, sizeof(buf), "%d%%", r);
        else
            snprintf(buf, sizeof(buf), "--");
        nf_set_text(hRain[i], buf);
        uint32_t c = r >= 0 ? rain_color(r) : NF_DARK;
        nf_set_text_color(hRain[i], lv_color_hex(c));
        nf_set_bg_color(hDot[i], lv_color_hex(c));
        if (rainFrom < 0 && r >= 50)
            rainFrom = i;
    }

    if (d.hourRain[0] < 0)
        snprintf(buf, sizeof(buf), "Sin datos de lluvia");
    else if (rainFrom < 0)
        snprintf(buf, sizeof(buf), "Sin lluvia en 5 horas");
    else
        snprintf(buf, sizeof(buf), "Lluvia desde las %d h", d.hourHour[rainFrom]);
    nf_set_text(rainL, buf);
    nf_set_text_color(rainL, lv_color_hex(rainFrom >= 0 ? NF_CYAN : NF_GREY));

    if (d.windKmh >= 0)
        snprintf(buf, sizeof(buf), "Viento %d km/h", d.windKmh);
    else
        snprintf(buf, sizeof(buf), "Viento --");
    nf_set_text(windL, buf);

    if (d.sunsetMin >= 0)
        snprintf(buf, sizeof(buf), "Atardece %02d:%02d", d.sunsetMin / 60, d.sunsetMin % 60);
    else
        snprintf(buf, sizeof(buf), " ");
    nf_set_text(sunL, buf);
#endif
}
