#ifndef DRIVER_H
#define DRIVER_H

#ifdef NO_WATCHFACES


#elif defined(ESPS3_1_69) || defined(ELECROW_S3) || defined(ESP32_CYD) || defined(VIEWE_2_8)
// 240x280 watchfaces

#if (FLASH_SIZE >= 8)
#endif
#if (FLASH_SIZE >= 16)
#endif

#elif defined(ESPS3_2_06)

#elif defined(VIEWE_SMARTRING) || defined(VIEWE_KNOB_15) || defined(ESPS3_1_75) || defined(VIEWE_S3_1_5)

/* Navi Moto's own faces (src/faces/nm_*), drawn with LVGL objects - they replaced the
   upstream bitmap faces (Red, Radar, Analog, Shadow, Blue, Pixel), which took several MB. */
#define ENABLE_FACE_NM_PIXEL    // (Pixel Moto)
#define ENABLE_FACE_NM_TABLERO  // (Tablero)
#define ENABLE_FACE_NM_CLIMA    // (Clima de ruta)
#define ENABLE_FACE_NM_COPILOTO // (Copiloto)

#elif defined(VIEWE_ECHO_EAR)


#else
// 240x240 watchfaces


#if (FLASH_SIZE >= 8)
#endif
#if (FLASH_SIZE >= 16)
#endif

#endif

#if defined(ESPS3_1_69) || defined(ESPS3_1_28) || defined(VIEWE_SMARTRING) || defined(ESPS3_1_75) || defined(ESPS3_2_06)
// #define ENABLE_APP_QMI8658C
// #define ENABLE_APP_ATTITUDE
#endif

#if defined(ELECROW_C3) || defined(ELECROW_S3)
#define ENABLE_RTC

#ifndef ENABLE_FACE_ELECROW
#if defined(ELECROW_C3)
#undef ENABLE_FACE_34_2
#endif
#endif
#endif

#if defined(M5_STACK_DIAL) || defined(VIEWE_KNOB_15) || defined(ELECROW_S3)
#define ENABLE_APP_RANGE
#endif

#define ENABLE_APP_CALENDAR
// #define ENABLE_APP_SAMPLE   // template/demo app, not useful on a real watch
#define ENABLE_APP_TOUCH_TEST
// #define ENABLE_GAME_TASK
// #define ENABLE_GAME_RACING

// #define ENABLE_GAME_SIMON

#define ENABLE_APP_NAVIGATION
#define ENABLE_APP_CONTACTS
#define ENABLE_APP_TIMER
#define ENABLE_APP_TRIP

#ifdef __cplusplus
extern "C" {
#endif


void hal_setup(void);
void hal_loop(void);


void vibratePin(bool state);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*DRIVER_H*/
