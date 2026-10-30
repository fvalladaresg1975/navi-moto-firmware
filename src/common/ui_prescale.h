/*
    Pre-scaled images.

    The UI was drawn for a 240 px screen and grows to the 466 px boards with
    lv_image_set_scale(). LVGL then transforms the bitmap again on every frame the image
    is part of - with bilinear filtering, per pixel - and that was most of the render time
    measured on the 1.75C (the control panel sliding over the face: ~150 ms per frame).

    ui_image_scale() renders each (source, scale) pair once into a PSRAM buffer and shows
    that buffer unscaled, so a frame only copies pixels. The object keeps the size and
    position it had with the runtime scale, so layouts and touch areas do not move.
    ui_image_src() changes the source of such an image and keeps the scale; use it instead
    of lv_image_set_src() for any image that went through ui_image_scale().

    Falls back to the runtime scale when the source is not an in-memory image or the
    buffer budget (PRESCALE_BUDGET_BYTES) is used up.
*/

#ifndef _UI_PRESCALE_H
#define _UI_PRESCALE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"

    void ui_image_scale(lv_obj_t *img, int32_t scale);
    void ui_image_src(lv_obj_t *img, const void *src);
    uint32_t ui_image_prescaled_bytes(void); /* PSRAM held by the pre-scaled bitmaps */

#ifdef __cplusplus
}
#endif

#endif
