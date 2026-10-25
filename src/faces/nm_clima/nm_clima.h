/*
    Navi Moto face "Clima de ruta": is it a good time to ride? Current weather, the next
    five hours with temperature and chance of rain, when the rain starts, wind and sunset,
    plus the board / phone battery arcs on the edge.
*/

#ifndef _FACE_NM_CLIMA_H
#define _FACE_NM_CLIMA_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include "app_hal.h"

#ifdef ENABLE_FACE_NM_CLIMA
    extern lv_obj_t *face_nm_clima;
    LV_IMG_DECLARE(face_nm_clima_preview);
#endif

    void init_face_nm_clima(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **));
    void update_check_nm_clima(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                               int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen);

#ifdef __cplusplus
}
#endif

#endif
