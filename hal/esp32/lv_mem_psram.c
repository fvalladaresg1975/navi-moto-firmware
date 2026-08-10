/*
 * LVGL allocator backed by the board's external PSRAM.
 *
 * Why this exists: this board has 8 MB of PSRAM sitting essentially unused
 * ("[PSRAM] size=8386071 free=8287979" at boot) while LVGL was configured with
 * LV_USE_STDLIB_MALLOC = LV_STDLIB_CLIB, i.e. plain malloc(), which on
 * Arduino-ESP32 serves *internal* RAM only. That internal heap is 192 KB total
 * with just ~31 KB free at boot, and it is the same heap the BLE stack draws
 * from - so every screen, label and style LVGL created was competing with
 * Bluetooth for a very small pool.
 *
 * Measured consequence: app screens are created on first open and never
 * destroyed (REGISTER_APP + screen_init), so browsing the watch permanently
 * consumed internal heap. After hours of use BLE could no longer allocate
 * ("E BLE_INIT: Malloc failed"), the watch accepted connections but never
 * completed GATT service discovery, and the phone dropped it every ~15 s
 * (status=22). A reboot always cleared it. Serial measurements confirmed the
 * heap does NOT decay while idle (two readings 5 min apart were byte-identical:
 * free=20428 maxAlloc=9716), so this was consumption by events, not a leak.
 *
 * Pointing LVGL at PSRAM moves all of that off the contended internal heap.
 * Note the LVGL draw buffers (lvBuffer in app_hal.cpp) are a separate static
 * array and deliberately stay in fast internal RAM - those are the
 * performance-critical, DMA-facing ones.
 */

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"
#include <stdlib.h>

void lv_mem_init(void)
{
    return; /*Nothing to init - the PSRAM heap is set up by the Arduino core*/
}

void lv_mem_deinit(void)
{
    return;
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    /*Not supported - allocations go straight to the PSRAM heap*/
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
    return;
}

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL)
    {
        /*Fall back to internal RAM rather than failing outright - keeps the UI
          alive if PSRAM is ever unavailable or exhausted.*/
        p = malloc(size);
    }
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    void *n = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (n == NULL)
    {
        /*p is still valid when heap_caps_realloc fails, so this is safe.*/
        n = realloc(p, new_size);
    }
    return n;
}

void lv_free_core(void *p)
{
    /*free() handles both heaps on ESP-IDF, whichever the block came from.*/
    free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    /*Not supported*/
    LV_UNUSED(mon_p);
    return;
}

lv_result_t lv_mem_test_core(void)
{
    /*Not supported*/
    return LV_RESULT_OK;
}

#endif /*LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM*/
