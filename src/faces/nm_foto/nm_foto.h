/*
    Navi Moto face "Foto": a photo from the phone as the background (the app's
    "Esfera con foto" screen crops it to the circle and sends it, command 0xCA), with
    the time, date and a small status line over it, at the top, centre or bottom as
    the user chose there.
*/

#ifndef _FACE_NM_FOTO_H
#define _FACE_NM_FOTO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include "app_hal.h"

#ifdef ENABLE_FACE_NM_FOTO
    extern lv_obj_t *face_nm_foto;
    LV_IMG_DECLARE(face_nm_foto_preview);
#endif

    void init_face_nm_foto(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **));
    void update_check_nm_foto(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                              int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen);

#ifdef __cplusplus
}
#endif

#endif
