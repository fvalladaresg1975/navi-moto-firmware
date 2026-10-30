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

void nf_set_bg_color(lv_obj_t *obj, lv_color_t c)
{
    if (obj && !lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), c))
        lv_obj_set_style_bg_color(obj, c, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void nf_set_text_color(lv_obj_t *obj, lv_color_t c)
{
    if (obj && !lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), c))
        lv_obj_set_style_text_color(obj, c, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void nf_set_border_color(lv_obj_t *obj, lv_color_t c)
{
    if (obj && !lv_color_eq(lv_obj_get_style_border_color(obj, LV_PART_MAIN), c))
        lv_obj_set_style_border_color(obj, c, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void setWeatherIcon(lv_obj_t *obj, int id, bool day); // ui.c

void nf_set_weather_icon(lv_obj_t *img, int id, bool day)
{
    static lv_obj_t *lastImg[8];
    static int lastKey[8];
    if (!img)
        return;
    /* lv_image_set_src() invalidates even for the same source, so remember what each
       icon shows (a handful of icons across the faces). */
    int key = id * 2 + (day ? 1 : 0);
    int slot = -1;
    for (int i = 0; i < 8; i++)
    {
        if (lastImg[i] == img)
        {
            if (lastKey[i] == key)
                return;
            slot = i;
            break;
        }
        if (slot < 0 && lastImg[i] == NULL)
            slot = i;
    }
    if (slot >= 0)
    {
        lastImg[slot] = img;
        lastKey[slot] = key;
    }
    setWeatherIcon(img, id, day);
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
    nf_arc_cache(arc);
    return arc;
}

#define NF_ARC_STRIP_ROWS 16

/* A cached arc: the pixels it covers, as ARGB8888 strips relative to the arc's corner. */
typedef struct
{
    lv_area_t area;
    lv_draw_buf_t *buf;
} nf_arc_strip_t;

typedef struct
{
    nf_arc_strip_t *strips;
    uint32_t count;
    uint32_t sig;   /* nf_arc_signature() of what the strips show; 0 = nothing cached */
    bool pending;   /* a render is queued */
    bool rendering; /* inside the render: let the arc class draw */
} nf_arc_cache_t;

static lv_obj_t *nfArcCanvas;
static lv_draw_buf_t *nfArcDummy;

/* Everything that changes how the arc looks. */
static uint32_t nf_arc_signature(lv_obj_t *arc)
{
    lv_area_t c;
    lv_obj_get_coords(arc, &c);
    int32_t v[] = {
        lv_area_get_width(&c), lv_area_get_height(&c),
        lv_arc_get_value(arc), lv_arc_get_min_value(arc), lv_arc_get_max_value(arc),
        (int32_t)lv_arc_get_bg_angle_start(arc), (int32_t)lv_arc_get_bg_angle_end(arc),
        (int32_t)lv_arc_get_angle_start(arc), (int32_t)lv_arc_get_angle_end(arc),
        lv_arc_get_rotation(arc),
        (int32_t)lv_color_to_u32(lv_obj_get_style_arc_color(arc, LV_PART_MAIN)),
        (int32_t)lv_color_to_u32(lv_obj_get_style_arc_color(arc, LV_PART_INDICATOR)),
        lv_obj_get_style_arc_width(arc, LV_PART_MAIN), lv_obj_get_style_arc_width(arc, LV_PART_INDICATOR),
        lv_obj_get_style_arc_opa(arc, LV_PART_MAIN), lv_obj_get_style_arc_opa(arc, LV_PART_INDICATOR),
        lv_obj_get_style_arc_rounded(arc, LV_PART_MAIN), lv_obj_get_style_arc_rounded(arc, LV_PART_INDICATOR),
        lv_obj_get_style_pad_left(arc, LV_PART_MAIN), lv_obj_get_style_pad_top(arc, LV_PART_MAIN),
        lv_obj_get_style_pad_left(arc, LV_PART_INDICATOR),
    };
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof(v) / sizeof(v[0]); i++)
        h = (h ^ (uint32_t)v[i]) * 16777619u;
    return h ? h : 1;
}

static void nf_arc_free_strips(nf_arc_cache_t *cache)
{
    for (uint32_t i = 0; i < cache->count; i++)
        lv_draw_buf_destroy(cache->strips[i].buf);
    lv_free(cache->strips);
    cache->strips = NULL;
    cache->count = 0;
    cache->sig = 0;
}

static void nf_arc_draw(lv_event_t *e);

static nf_arc_cache_t *nf_arc_find(lv_obj_t *arc)
{
    uint32_t n = lv_obj_get_event_count(arc);
    for (uint32_t i = 0; i < n; i++)
    {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(arc, i);
        if (lv_event_dsc_get_cb(d) == nf_arc_draw)
            return lv_event_dsc_get_user_data(d);
    }
    return NULL;
}

/* Runs from lv_async_call, outside any screen refresh: draws the arc once, the normal way,
   into a full-size buffer, and keeps only the strips that have pixels. */
static void nf_arc_render(void *p)
{
    lv_obj_t *arc = p;
    nf_arc_cache_t *cache = nf_arc_find(arc);
    if (!cache)
        return;
    cache->pending = false;
    nf_arc_free_strips(cache);

    lv_area_t c;
    lv_obj_get_coords(arc, &c);
    int32_t w = lv_area_get_width(&c);
    int32_t h = lv_area_get_height(&c);
    lv_draw_buf_t *big = w > 0 && h > 0 ? lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO) : NULL;
    if (!big)
        return; /* no memory: the arc keeps drawing itself */
    lv_draw_buf_clear(big, NULL);

    if (!nfArcCanvas)
    {
        nfArcCanvas = lv_canvas_create(lv_layer_sys());
        lv_obj_add_flag(nfArcCanvas, LV_OBJ_FLAG_HIDDEN);
        nfArcDummy = lv_draw_buf_create(1, 1, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    }
    lv_canvas_set_draw_buf(nfArcCanvas, big);
    lv_layer_t layer;
    lv_canvas_init_layer(nfArcCanvas, &layer);
    layer.buf_area = c; /* the buffer stands for the arc's own area */
    layer._clip_area = c;
    layer.phy_clip_area = c;
    cache->rendering = true;
    lv_obj_redraw(&layer, arc);
    cache->rendering = false;
    lv_canvas_finish_layer(nfArcCanvas, &layer);
    lv_canvas_set_draw_buf(nfArcCanvas, nfArcDummy);

    /* Strips of NF_ARC_STRIP_ROWS rows, trimmed to the columns that have pixels. */
    uint32_t maxStrips = (h + NF_ARC_STRIP_ROWS - 1) / NF_ARC_STRIP_ROWS;
    cache->strips = lv_malloc(maxStrips * sizeof(nf_arc_strip_t));
    bool ok = cache->strips != NULL;
    for (int32_t sy = 0; ok && sy < h; sy += NF_ARC_STRIP_ROWS)
    {
        int32_t ey = LV_MIN(sy + NF_ARC_STRIP_ROWS, h) - 1;
        int32_t minX = w, maxX = -1;
        for (int32_t y = sy; y <= ey; y++)
        {
            const uint32_t *row = lv_draw_buf_goto_xy(big, 0, y);
            for (int32_t x = 0; x < w; x++)
                if (row[x] >> 24)
                {
                    minX = LV_MIN(minX, x);
                    maxX = LV_MAX(maxX, x);
                }
        }
        if (maxX < minX)
            continue;
        int32_t sw = maxX - minX + 1;
        lv_draw_buf_t *buf = lv_draw_buf_create(sw, ey - sy + 1, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
        if (!buf)
        {
            ok = false;
            break;
        }
        for (int32_t y = sy; y <= ey; y++)
            lv_memcpy(lv_draw_buf_goto_xy(buf, 0, y - sy), lv_draw_buf_goto_xy(big, minX, y), sw * 4);
        nf_arc_strip_t *s = &cache->strips[cache->count++];
        lv_area_set(&s->area, minX, sy, maxX, ey);
        s->buf = buf;
    }
    lv_draw_buf_destroy(big);
    if (!ok)
    {
        nf_arc_free_strips(cache);
        return;
    }
    cache->sig = nf_arc_signature(arc);
    lv_obj_invalidate(arc);
}

/* Runs before the arc class's DRAW_MAIN: draws the cached strips instead, as plain images. */
static void nf_arc_draw(lv_event_t *e)
{
    lv_obj_t *arc = lv_event_get_current_target(e);
    nf_arc_cache_t *cache = lv_event_get_user_data(e);
    if (cache->rendering)
        return;
    if (cache->sig == 0 || cache->sig != nf_arc_signature(arc))
    {
        /* Changed: this frame the arc draws itself; the strips are redone right after. */
        if (!cache->pending)
        {
            cache->pending = true;
            lv_async_call(nf_arc_render, arc);
        }
        return;
    }

    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(arc, &c);
    for (uint32_t i = 0; i < cache->count; i++)
    {
        lv_area_t a = cache->strips[i].area;
        lv_area_move(&a, c.x1, c.y1);
        lv_area_t visible;
        if (!lv_area_intersect(&visible, &a, &layer->_clip_area))
            continue;
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = cache->strips[i].buf;
        d.image_area = a;
        lv_draw_image(layer, &d, &a);
    }
    lv_event_stop_processing(e);
}

static void nf_arc_delete(lv_event_t *e)
{
    nf_arc_cache_t *cache = lv_event_get_user_data(e);
    lv_async_call_cancel(nf_arc_render, lv_event_get_current_target(e));
    nf_arc_free_strips(cache);
    lv_free(cache);
}

void nf_arc_cache(lv_obj_t *arc)
{
    if (!arc || nf_arc_find(arc))
        return;
    nf_arc_cache_t *cache = lv_malloc(sizeof(nf_arc_cache_t));
    if (!cache)
        return;
    lv_memzero(cache, sizeof(*cache));
    lv_obj_add_event_cb(arc, nf_arc_draw, LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS, cache);
    lv_obj_add_event_cb(arc, nf_arc_delete, LV_EVENT_DELETE, cache);
}

void nf_arc_battery(lv_obj_t *arc, int pct, bool charging)
{
    if (!arc)
        return;
    int p = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (lv_arc_get_value(arc) != p)
        lv_arc_set_value(arc, p);
    lv_color_t c = nf_battery_color(p, charging);
    if (!lv_color_eq(lv_obj_get_style_arc_color(arc, LV_PART_INDICATOR), c))
        lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR | LV_STATE_DEFAULT);
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
