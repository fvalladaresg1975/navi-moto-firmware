/*
    Navi Moto watch faces: shared data and drawing helpers.

    The upstream faces only get time, weather icon/temperature, battery and connection
    (ui_update_watchfaces). The Navi Moto faces show what matters for riding - wind, the
    phone's battery, the last trip, the next hours' temperature and rain, sunset - so they
    also read a navi_face_data_t, filled by the HAL (navi_face_get, hal/esp32/app_hal.cpp).
    Drawn with plain LVGL objects and the fonts in src/fonts_nm (Montserrat, OFL): a few KB
    per face, against the megabytes of the bitmap faces they replaced.
*/

#ifndef _NAVI_FACE_H
#define _NAVI_FACE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include <stdbool.h>

#define NAVI_FACE_HOURS 5

    typedef struct
    {
        int hour, minute, second; /* hour 0-23 */
        int day, month, year;     /* month 1-12 */
        int weekday;              /* 0 = Sunday */
        bool connected;
        bool weatherKnown;
        int temp, icon;           /* now */
        int windKmh;              /* -1 = unknown */
        int boardPct;             /* -1 = unknown */
        bool boardCharging;
        int phonePct;             /* -1 = unknown */
        bool phoneCharging;
        long tripMeters;          /* 0 = no trip recorded yet */
        unsigned long tripSeconds;
        /* the next NAVI_FACE_HOURS hours, starting with the next full hour */
        int hourHour[NAVI_FACE_HOURS];
        int hourTemp[NAVI_FACE_HOURS];
        int hourRain[NAVI_FACE_HOURS]; /* % chance, -1 = unknown */
        int sunsetMin;                 /* minutes after midnight, -1 = unknown */
        const char *city;              /* "" if unknown */
    } navi_face_data_t;

    /* Implemented in the HAL. */
    void navi_face_get(navi_face_data_t *d);

    /* The "Foto" face's background, sent from the phone (0xCA) and kept in the board's
       flash: the decoded image (RGB565 in PSRAM), or NULL if no photo was ever sent.
       photo_serial changes every time a new one is decoded, so the face knows to reload
       it; photo_pos is where the user placed the time: 0 top, 1 centre, 2 bottom. Also
       implemented in the HAL. */
    const lv_image_dsc_t *navi_face_photo(void);
    uint32_t navi_face_photo_serial(void);
    int navi_face_photo_pos(void);

    /* Spanish day / month names. */
    extern const char *NF_WEEKDAYS[7];      /* "Domingo".. */
    extern const char *NF_WEEKDAYS_ABBR[7]; /* "DOM".. */
    extern const char *NF_MONTHS[12];       /* "Enero".. */
    extern const char *NF_MONTHS_ABBR[12];  /* "ENE".. */
    extern const char *NF_WEEKDAYS_SHORT[7]; /* "Dom".. */
    extern const char *NF_MONTHS_SHORT[12];  /* "Ene".. */

    /* Fonts (src/fonts_nm and src/fonts_es). */
    LV_FONT_DECLARE(nm_time_110);
    LV_FONT_DECLARE(nm_time_86);
    LV_FONT_DECLARE(nm_digits_170);
    LV_FONT_DECLARE(nm_digits_120);
    LV_FONT_DECLARE(nm_text_28);

    /* Palette shared with the navigation screen. */
#define NF_AMBER 0xFFC24D
#define NF_WHITE 0xFFFFFF
#define NF_GREY 0x8C8C8C
#define NF_DARK 0x2C2C2A
#define NF_GREEN 0x2ECC71
#define NF_BLUE 0x3DA5FF
#define NF_CYAN 0x40D2E6
#define NF_RED 0xE0262B
#define NF_YELLOW 0xF5C423

    /* A black, non-scrollable full-screen face root. */
    lv_obj_t *nf_screen(void);
    /* Call at the end of a face's init: hooks the face into the watch's gestures, long
       press (face picker) etc. - onFaceEvent in ui.c, like every upstream face did - and
       makes every child click-through, so a press on a bar or icon reaches the face too.
       Without it the first build's faces ignored all touch. */
    void nf_finish(lv_obj_t *face);
    /* A label centred at (x, y) from the screen centre. */
    lv_obj_t *nf_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, const char *text);
    /* lv_label_set_text only when the text changes (no redraw otherwise). */
    void nf_set_text(lv_obj_t *label, const char *text);
    /* The same for colours and the weather icon. Every style write invalidates the object
       even when the value is unchanged, and the faces are updated on every pass of
       hal_loop(): with plain setters the face was redrawn ~30 times a second for nothing,
       screen off included. */
    void nf_set_bg_color(lv_obj_t *obj, lv_color_t c);
    void nf_set_text_color(lv_obj_t *obj, lv_color_t c);
    void nf_set_border_color(lv_obj_t *obj, lv_color_t c);
    void nf_set_weather_icon(lv_obj_t *img, int id, bool day);
    /* A thin edge arc, value 0-100. side 0 = left half (board), 1 = right half (phone),
       both filling bottom-up, like the control panel. */
    lv_obj_t *nf_edge_arc(lv_obj_t *parent, int side, int width, int inset);
    void nf_arc_battery(lv_obj_t *arc, int pct, bool charging);
    /* Keep a big lv_arc as a picture. LVGL's software arc is slow at this size: a
       screen-sized one added ~33 ms to every frame on the 466 px boards (measured on the
       1.75C). With this the arc is drawn the normal way once, right after it changes, and
       kept as thin ARGB strips (PSRAM) that each frame only copies. Call once after
       creating the arc; it looks the same. */
    void nf_arc_cache(lv_obj_t *arc);
    /* Battery colour: blue while charging, otherwise green / amber / red. */
    lv_color_t nf_battery_color(int pct, bool charging);
    /* "23,4 km" / "850 m" */
    void nf_format_km(char *buf, int len, long meters);

#ifdef __cplusplus
}
#endif

#endif
