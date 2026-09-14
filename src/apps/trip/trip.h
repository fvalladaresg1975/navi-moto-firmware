/*
    Last-trip statistics, derived from the navigation data the phone already sends.
    See get_last_trip() in the HAL for how the numbers are produced (and their limits).
*/

#ifndef _TRIP_APP_H
#define _TRIP_APP_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include "app_hal.h"
#include "../../common/app_manager.h"
#include "../../common/api.h"

#ifdef ENABLE_APP_TRIP

    /* Reused from the navigation app rather than adding another asset - it is the same
       idea (a route) and flash is the scarcer resource here. */
    LV_IMG_DECLARE(ui_img_arrow_png);

    void ui_tripScreen_screen_init();
    void ui_app_load(lv_obj_t **screen, void (*screen_init)(void));
    void ui_app_exit(void);

#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
