/*
    Navi Moto face "Tablero": a motorcycle instrument cluster - the hour as a tachometer
    needle (0-12 like rpm x1000, red zone at the end), digital time, the board battery as
    a fuel gauge, the temperature where the engine temperature would be, a "BT" tell-tale
    that lights when connected, and the last trip as the trip meter.
*/

#ifndef _FACE_NM_TABLERO_H
#define _FACE_NM_TABLERO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include "app_hal.h"

#ifdef ENABLE_FACE_NM_TABLERO
    extern lv_obj_t *face_nm_tablero;
    LV_IMG_DECLARE(face_nm_tablero_preview);
#endif

    void init_face_nm_tablero(void (*callback)(const char *, const lv_image_dsc_t *, lv_obj_t **, lv_obj_t **));
    void update_check_nm_tablero(lv_obj_t *root, int second, int minute, int hour, bool mode, bool am, int day, int month, int year, int weekday,
                                 int temp, int icon, int battery, bool connection, int steps, int distance, int kcal, int bpm, int oxygen);

#ifdef __cplusplus
}
#endif

#endif
