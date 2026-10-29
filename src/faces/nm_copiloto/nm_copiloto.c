/*
    Navi Moto face "Copiloto" (466x466). See nm_copiloto.h and src/common/navi_face.h.
*/

#include "nm_copiloto.h"

#ifdef ENABLE_FACE_NM_COPILOTO

#include <stdio.h>
#include "../../common/navi_face.h"
#include "../../ui/ui.h"

lv_obj_t *face_nm_copiloto;
static lv_obj_t *arcBoard, *arcPhone, *dayL, *hourL, *minL, *statusL, *wxIcon;

#endif

void init_face_nm_copiloto(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **))
{
#ifdef ENABLE_FACE_NM_COPILOTO
    face_nm_copiloto = nf_screen();
    arcBoard = nf_edge_arc(face_nm_copiloto, 0, 10, 5);
    arcPhone = nf_edge_arc(face_nm_copiloto, 1, 10, 5);

    dayL = nf_label(face_nm_copiloto, &nm_text_28, NF_GREY, 0, -146, "");
    hourL = nf_label(face_nm_copiloto, &nm_digits_170, NF_WHITE, 0, -38, "00");
    minL = nf_label(face_nm_copiloto, &nm_digits_120, NF_AMBER, 0, 86, "00");
    statusL = nf_label(face_nm_copiloto, &lv_font_montserrat_20, NF_GREEN, -18, 168, "");
    wxIcon = lv_image_create(face_nm_copiloto);
    lv_image_set_scale(wxIcon, 120);
    setWeatherIcon(wxIcon, 0, true);
    lv_obj_add_flag(wxIcon, LV_OBJ_FLAG_HIDDEN);

    nf_finish(face_nm_copiloto);
    callback("Copiloto", &face_nm_copiloto_preview, &face_nm_copiloto, NULL);
#endif
}

void update_check_nm_copiloto(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                              int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen)
{
#ifdef ENABLE_FACE_NM_COPILOTO
    if (root != face_nm_copiloto || !face_nm_copiloto)
        return;
    navi_face_data_t d;
    navi_face_get(&d);
    char buf[40];

    snprintf(buf, sizeof(buf), "%s %d", NF_WEEKDAYS_ABBR[d.weekday % 7], d.day);
    nf_set_text(dayL, buf);
    snprintf(buf, sizeof(buf), "%02d", hour);
    nf_set_text(hourL, buf);
    snprintf(buf, sizeof(buf), "%02d", minute);
    nf_set_text(minL, buf);

    nf_arc_battery(arcBoard, d.boardPct, d.boardCharging);
    nf_arc_battery(arcPhone, d.phonePct, d.phoneCharging);

    if (d.weatherKnown)
        snprintf(buf, sizeof(buf), d.connected ? LV_SYMBOL_BLUETOOTH " Conectado   %d°" : "Sin conexión   %d°", d.temp);
    else
        snprintf(buf, sizeof(buf), d.connected ? LV_SYMBOL_BLUETOOTH " Conectado" : "Sin conexión");
    nf_set_text(statusL, buf);
    nf_set_text_color(statusL, lv_color_hex(d.connected ? NF_GREEN : NF_GREY));
    if (d.weatherKnown)
    {
        nf_set_weather_icon(wxIcon, d.icon, d.hour > 7 && d.hour < 21);
        lv_obj_remove_flag(wxIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align_to(wxIcon, statusL, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    }
    else
        lv_obj_add_flag(wxIcon, LV_OBJ_FLAG_HIDDEN);
#endif
}
