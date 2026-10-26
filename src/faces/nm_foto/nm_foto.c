/*
    Navi Moto face "Foto" (466x466). See nm_foto.h and src/common/navi_face.h.

    The photo itself lives in the HAL (navi_face_photo): it arrives over BLE, is saved
    to the board's flash and decoded once into PSRAM. This face only shows it, and
    re-reads it whenever navi_face_photo_serial() changes. A photo can be anything, so
    every text gets a dark offset copy behind it (LVGL has no text shadow) and sits on
    a band that darkens the photo under it.
*/

#include "nm_foto.h"

#ifdef ENABLE_FACE_NM_FOTO

#include <stdio.h>
#include "../../common/navi_face.h"
#include "../../ui/ui.h"

lv_obj_t *face_nm_foto;
static lv_obj_t *bg, *hintIcon, *hintText, *band;
static lv_obj_t *timeL, *timeS, *dateL, *dateS, *statL, *statS;
static uint32_t shownSerial = 0xFFFFFFFF;
static int shownPos = -1;

/* A white label with its shadow copy (created first, so it is drawn underneath). */
static lv_obj_t *shadowed(const lv_font_t *font, lv_obj_t **shadow)
{
    *shadow = nf_label(face_nm_foto, font, 0x000000, 0, 0, "");
    lv_obj_set_style_text_opa(*shadow, 190, LV_PART_MAIN | LV_STATE_DEFAULT);
    return nf_label(face_nm_foto, font, NF_WHITE, 0, 0, "");
}

static void place(lv_obj_t *l, lv_obj_t *s, int y, int off)
{
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    lv_obj_align(s, LV_ALIGN_CENTER, off, y + off);
}

static void set_both(lv_obj_t *l, lv_obj_t *s, const char *text)
{
    nf_set_text(l, text);
    nf_set_text(s, text);
}

/* Time position chosen in the app: 0 top, 1 centre, 2 bottom. */
static void apply_layout(int pos)
{
    int yTime, yDate, yStat;
    lv_obj_set_style_bg_grad_dir(band, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (pos == 0)
    {
        yStat = -188;
        yTime = -110;
        yDate = -40;
        lv_obj_set_size(band, 466, 250);
        lv_obj_align(band, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_main_opa(band, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_opa(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else if (pos == 2)
    {
        yTime = 70;
        yDate = 140;
        yStat = 188;
        lv_obj_set_size(band, 466, 250);
        lv_obj_align(band, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_main_opa(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_opa(band, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else
    {
        yTime = -20;
        yDate = 50;
        yStat = 95;
        lv_obj_set_size(band, 400, 200);
        lv_obj_align(band, LV_ALIGN_CENTER, 0, 22);
        lv_obj_set_style_radius(band, 48, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(band, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_main_opa(band, 110, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_opa(band, 110, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    place(timeL, timeS, yTime, 4);
    place(dateL, dateS, yDate, 2);
    place(statL, statS, yStat, 2);
}

static void load_photo(void)
{
    const lv_image_dsc_t *photo = navi_face_photo();
    lv_image_set_src(bg, NULL); // the descriptor is reused for every photo: force a re-read
    if (photo)
    {
        lv_image_cache_drop(photo);
        lv_image_set_src(bg, photo);
        lv_obj_center(bg);
        lv_obj_remove_flag(bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(band, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hintIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hintText, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(hintIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(hintText, LV_OBJ_FLAG_HIDDEN);
    }
}

#endif

void init_face_nm_foto(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **))
{
#ifdef ENABLE_FACE_NM_FOTO
    face_nm_foto = nf_screen();

    bg = lv_image_create(face_nm_foto);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);

    /* No photo yet: say where to send one from (the time still shows, in the middle). */
    hintIcon = nf_label(face_nm_foto, &lv_font_montserrat_48, NF_GREY, 0, -150, LV_SYMBOL_IMAGE);
    hintText = nf_label(face_nm_foto, &lv_font_montserrat_20, NF_GREY, 0, 150,
                        "Envía una foto desde la app:\nEsfera con foto");

    band = lv_obj_create(face_nm_foto);
    lv_obj_remove_style_all(band);
    lv_obj_set_style_bg_color(band, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(band, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(band, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);

    timeL = shadowed(&nm_time_110, &timeS);
    dateL = shadowed(&nm_text_28, &dateS);
    statL = shadowed(&lv_font_montserrat_20, &statS);

    nf_finish(face_nm_foto);
    callback("Foto", &face_nm_foto_preview, &face_nm_foto, NULL);
#endif
}

void update_check_nm_foto(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                          int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen)
{
#ifdef ENABLE_FACE_NM_FOTO
    if (root != face_nm_foto || !face_nm_foto)
        return;
    navi_face_data_t d;
    navi_face_get(&d);
    char buf[48];

    uint32_t serial = navi_face_photo_serial();
    if (serial != shownSerial)
    {
        shownSerial = serial;
        load_photo();
    }
    int pos = navi_face_photo() ? navi_face_photo_pos() : 1;
    if (pos != shownPos)
    {
        shownPos = pos;
        apply_layout(pos);
    }

    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    set_both(timeL, timeS, buf);
    snprintf(buf, sizeof(buf), "%s %d %s", NF_WEEKDAYS[d.weekday % 7], d.day, NF_MONTHS[(d.month + 11) % 12]);
    set_both(dateL, dateS, buf);

    if (d.boardPct >= 0)
        snprintf(buf, sizeof(buf), "%s%s %d%%", d.connected ? LV_SYMBOL_BLUETOOTH "   " : "",
                 d.boardCharging ? LV_SYMBOL_CHARGE
                 : d.boardPct >= 90 ? LV_SYMBOL_BATTERY_FULL
                 : d.boardPct >= 65 ? LV_SYMBOL_BATTERY_3
                 : d.boardPct >= 40 ? LV_SYMBOL_BATTERY_2
                 : d.boardPct >= 15 ? LV_SYMBOL_BATTERY_1
                                    : LV_SYMBOL_BATTERY_EMPTY,
                 d.boardPct);
    else
        snprintf(buf, sizeof(buf), "%s", d.connected ? LV_SYMBOL_BLUETOOTH : "");
    set_both(statL, statS, buf);
#endif
}
