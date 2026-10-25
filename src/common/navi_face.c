/*
    Navi Moto watch faces: shared helpers. See navi_face.h.
*/

#include "navi_face.h"
#include <stdio.h>
#include <string.h>

const char *NF_WEEKDAYS[7] = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};
const char *NF_WEEKDAYS_ABBR[7] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
const char *NF_MONTHS[12] = {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio",
                             "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"};
const char *NF_MONTHS_ABBR[12] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                                  "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
const char *NF_WEEKDAYS_SHORT[7] = {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};
const char *NF_MONTHS_SHORT[12] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun",
                                   "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};

lv_obj_t *nf_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    return s;
}

void onFaceEvent(lv_event_t *e); // ui.c

void nf_finish(lv_obj_t *face)
{
    uint32_t n = lv_obj_get_child_count(face);
    for (uint32_t i = 0; i < n; i++)
    {
        lv_obj_t *c = lv_obj_get_child(face, i);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    lv_obj_add_event_cb(face, onFaceEvent, LV_EVENT_ALL, NULL);
}

lv_obj_t *nf_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(l, LV_ALIGN_CENTER, x, y);
    lv_label_set_text(l, text);
    return l;
}

void nf_set_text(lv_obj_t *label, const char *text)
{
    if (!label)
        return;
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0)
        return;
    lv_label_set_text(label, text);
}

lv_color_t nf_battery_color(int pct, bool charging)
{
    if (charging)
        return lv_color_hex(NF_BLUE);
    if (pct < 20)
        return lv_color_hex(NF_RED);
    if (pct < 50)
        return lv_color_hex(0xEF9F27);
    return lv_color_hex(NF_GREEN);
}

lv_obj_t *nf_edge_arc(lv_obj_t *parent, int side, int width, int inset)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, lv_pct(100), lv_pct(100));
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    if (side == 0)
    {
        lv_arc_set_bg_angles(arc, 100, 260);
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    }
    else
    {
        lv_arc_set_bg_angles(arc, 280, 80);
        lv_arc_set_mode(arc, LV_ARC_MODE_REVERSE);
    }
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(arc, inset, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(arc, lv_color_hex(NF_DARK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    return arc;
}

void nf_arc_battery(lv_obj_t *arc, int pct, bool charging)
{
    if (!arc)
        return;
    int p = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (lv_arc_get_value(arc) != p)
        lv_arc_set_value(arc, p);
    lv_obj_set_style_arc_color(arc, nf_battery_color(p, charging), LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

void nf_format_km(char *buf, int len, long meters)
{
    if (meters < 1000)
    {
        snprintf(buf, len, "%ld m", meters);
        return;
    }
    long dm = (meters + 50) / 100; // tenths of a km
    snprintf(buf, len, "%ld,%ld km", dm / 10, dm % 10);
}
