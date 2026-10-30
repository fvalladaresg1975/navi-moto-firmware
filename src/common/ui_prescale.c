/*
    Pre-scaled images - see ui_prescale.h.
*/

#include "ui_prescale.h"
#include <stdbool.h>

/* Every (source, scale) pair rendered so far. Entries are never freed: the sources are
   const images in flash and the screens using them live as long as the watch is on. */
#define PRESCALE_MAX_ENTRIES 128
#define PRESCALE_BUDGET_BYTES (3 * 1024 * 1024)

typedef struct
{
    const void *src;
    int32_t scale;
    lv_draw_buf_t *buf;
} prescale_entry_t;

/* Per image object: what it was asked to show. Attached as the event user data. */
typedef struct
{
    const void *src;
    int32_t scale;
    bool fixSize; /* it was LV_SIZE_CONTENT: keep it at the unscaled size */
} prescale_obj_t;

static prescale_entry_t entries[PRESCALE_MAX_ENTRIES];
static uint32_t entryCount;
static uint32_t usedBytes;
static lv_obj_t *renderCanvas;

static lv_draw_buf_t *render(const void *src, int32_t scale)
{
    if (lv_image_src_get_type(src) != LV_IMAGE_SRC_VARIABLE)
        return NULL;
    const lv_image_dsc_t *dsc = (const lv_image_dsc_t *)src;
    int32_t w = dsc->header.w;
    int32_t h = dsc->header.h;
    if (w <= 0 || h <= 0)
        return NULL;

    /* Same pivot and transformed area as lv_image uses for a runtime scale. */
    lv_point_t pivot = {w / 2, h / 2};
    lv_area_t a;
    lv_image_buf_get_transformed_area(&a, w, h, 0, scale, scale, &pivot);
    int32_t sw = lv_area_get_width(&a);
    int32_t sh = lv_area_get_height(&a);
    if (sw <= 0 || sh <= 0)
        return NULL;

    uint32_t bytes = (uint32_t)sw * sh * 4;
    if (usedBytes + bytes > PRESCALE_BUDGET_BYTES)
        return NULL;
    lv_draw_buf_t *buf = lv_draw_buf_create(sw, sh, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if (!buf)
        return NULL;
    lv_draw_buf_clear(buf, NULL);

    if (!renderCanvas)
    {
        renderCanvas = lv_canvas_create(lv_layer_sys());
        lv_obj_add_flag(renderCanvas, LV_OBJ_FLAG_HIDDEN);
    }
    lv_canvas_set_draw_buf(renderCanvas, buf);

    lv_layer_t layer;
    lv_canvas_init_layer(renderCanvas, &layer);
    lv_draw_image_dsc_t draw;
    lv_draw_image_dsc_init(&draw);
    draw.src = src;
    draw.scale_x = scale;
    draw.scale_y = scale;
    draw.pivot = pivot;
    draw.antialias = 1;
    lv_area_set(&draw.image_area, -a.x1, -a.y1, -a.x1 + w - 1, -a.y1 + h - 1);
    lv_draw_image(&layer, &draw, &draw.image_area);
    lv_canvas_finish_layer(renderCanvas, &layer);

    usedBytes += bytes;
    return buf;
}

static lv_draw_buf_t *lookup(const void *src, int32_t scale)
{
    for (uint32_t i = 0; i < entryCount; i++)
        if (entries[i].src == src && entries[i].scale == scale)
            return entries[i].buf;
    if (entryCount >= PRESCALE_MAX_ENTRIES)
        return NULL;
    lv_draw_buf_t *buf = render(src, scale);
    if (buf)
        entries[entryCount++] = (prescale_entry_t){src, scale, buf};
    return buf;
}

/* The bitmap is bigger than the object when scaled up; give LVGL room to redraw the
   overhang, as it does for a runtime scale. */
static void extDrawCb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    const lv_image_dsc_t *shown = (const lv_image_dsc_t *)lv_image_get_src(obj);
    if (!shown || lv_image_src_get_type(shown) != LV_IMAGE_SRC_VARIABLE)
        return;
    int32_t *s = lv_event_get_param(e);
    int32_t padX = ((int32_t)shown->header.w - lv_obj_get_width(obj) + 1) / 2;
    int32_t padY = ((int32_t)shown->header.h - lv_obj_get_height(obj) + 1) / 2;
    *s = LV_MAX(*s, LV_MAX(padX, padY));
}

static void deleteCb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static prescale_obj_t *find(lv_obj_t *img)
{
    uint32_t n = lv_obj_get_event_count(img);
    for (uint32_t i = 0; i < n; i++)
    {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(img, i);
        if (lv_event_dsc_get_cb(d) == deleteCb)
            return lv_event_dsc_get_user_data(d);
    }
    return NULL;
}

static void apply(lv_obj_t *img, prescale_obj_t *p)
{
    if (!p->src)
        return; /* no source yet: applied by the first ui_image_src() */

    const lv_image_dsc_t *dsc = (const lv_image_dsc_t *)p->src;
    lv_draw_buf_t *buf = p->scale == LV_SCALE_NONE ? NULL : lookup(p->src, p->scale);
    const void *shown = buf ? (const void *)buf : p->src;
    int32_t scale = buf ? LV_SCALE_NONE : p->scale; /* no buffer: runtime scale, as before */

    if (lv_image_get_src(img) == shown && lv_image_get_scale(img) == scale)
        return;
    if (lv_image_get_scale(img) != scale)
        lv_image_set_scale(img, scale);
    if (lv_image_get_src(img) != shown)
        lv_image_set_src(img, shown);

    /* lv_image sizes a LV_SIZE_CONTENT image to its bitmap; keep the unscaled size the
       runtime scale had, so nothing around it moves. */
    if (p->fixSize && (lv_obj_get_style_width(img, LV_PART_MAIN) != dsc->header.w ||
                       lv_obj_get_style_height(img, LV_PART_MAIN) != dsc->header.h))
        lv_obj_set_size(img, dsc->header.w, dsc->header.h);
    if (buf)
    {
        /* A runtime-scaled image is also touched on its scaled area. */
        int32_t pad = ((int32_t)buf->header.w - (int32_t)dsc->header.w + 1) / 2;
        if (pad > 0)
            lv_obj_set_ext_click_area(img, pad);
    }
    lv_obj_refresh_ext_draw_size(img);
}

void ui_image_scale(lv_obj_t *img, int32_t scale)
{
    prescale_obj_t *p = find(img);
    if (!p)
    {
        p = lv_malloc(sizeof(prescale_obj_t));
        if (!p)
        {
            lv_image_set_scale(img, scale);
            return;
        }
        const void *src = lv_image_get_src(img);
        p->src = src && lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE ? src : NULL;
        p->fixSize = lv_obj_get_style_width(img, LV_PART_MAIN) == LV_SIZE_CONTENT &&
                     lv_obj_get_style_height(img, LV_PART_MAIN) == LV_SIZE_CONTENT;
        lv_obj_add_event_cb(img, deleteCb, LV_EVENT_DELETE, p);
        lv_obj_add_event_cb(img, extDrawCb, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
        if (!p->src && src)
        {
            /* A file or symbol: nothing to pre-render. */
            p->scale = scale;
            lv_image_set_scale(img, scale);
            return;
        }
    }
    p->scale = scale;
    apply(img, p);
}

void ui_image_src(lv_obj_t *img, const void *src)
{
    prescale_obj_t *p = find(img);
    if (!p || !src || lv_image_src_get_type(src) != LV_IMAGE_SRC_VARIABLE)
    {
        lv_image_set_src(img, src);
        return;
    }
    p->src = src;
    apply(img, p);
}

uint32_t ui_image_prescaled_bytes(void)
{
    return usedBytes;
}
