/*
    Navi Moto face "Copiloto": minimal and high contrast, readable at a glance in the sun -
    huge hours and minutes, the day, the board / phone battery arcs and one status line.
*/

#ifndef _FACE_NM_COPILOTO_H
#define _FACE_NM_COPILOTO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include "app_hal.h"

#ifdef ENABLE_FACE_NM_COPILOTO
    extern lv_obj_t *face_nm_copiloto;
    LV_IMG_DECLARE(face_nm_copiloto_preview);
#endif

    void init_face_nm_copiloto(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **));
    void update_check_nm_copiloto(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                                  int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen);

#ifdef __cplusplus
}
#endif

#endif
