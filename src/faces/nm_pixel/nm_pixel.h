/*
    Navi Moto face "Pixel Moto": the Pixel layout (big time, date, four tiles) with
    riding data - weather, wind, board and phone battery - and the last trip.
*/

#ifndef _FACE_NM_PIXEL_H
#define _FACE_NM_PIXEL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include "app_hal.h"

#ifdef ENABLE_FACE_NM_PIXEL
    extern lv_obj_t *face_nm_pixel;
    LV_IMG_DECLARE(face_nm_pixel_preview);
#endif

    void init_face_nm_pixel(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **));
    void update_check_nm_pixel(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                               int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen);

#ifdef __cplusplus
}
#endif

#endif
