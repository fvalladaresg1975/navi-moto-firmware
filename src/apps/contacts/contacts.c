
/*
    Copyright (c) 2024 Felix Biego. All rights reserved.
    This work is licensed under the terms of the MIT license.  
    For a copy, see <https://opensource.org/licenses/MIT>.
*/

#include "contacts.h"
#include "../../common/ui_prescale.h"
/* For UI_SCALE_PX/UI_SCALE - this app's rows were laid out in raw pixels for a 240x240
   screen, so on the 466x466 panel everything rendered at roughly half size. ui.c pulls
   the same header in the same way. */
#include "../../hal/esp32/displays/pins.h"

/* Fonts don't scale with UI_SCALE_PX, so bump them a step on large panels. Capped at 20:
   14/16/18/20 come from src/fonts_es with the accented Latin-1 range, while LVGL's
   built-in 24/30 are ASCII-only and would mangle names like "Muñoz" or "José". */
#if UI_SCALE >= 384
#define CONTACT_NAME_FONT &lv_font_montserrat_20
#define CONTACT_NUMBER_FONT &lv_font_montserrat_16
#else
#define CONTACT_NAME_FONT &lv_font_montserrat_16
#define CONTACT_NUMBER_FONT &lv_font_montserrat_14
#endif

#ifdef ENABLE_APP_CONTACTS

REGISTER_APP("Contactos", &ui_img_contact_list_png, ui_contactScreen, ui_contactScreen_screen_init);

lv_obj_t *ui_contactList;

void ui_event_contactScreen(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);
    if (event_code == LV_EVENT_SCREEN_LOAD_START)
    {
        lv_obj_scroll_by(ui_contactList, 0, -1, LV_ANIM_ON);
    }
    if (event_code == LV_EVENT_SCREEN_LOADED)
    {
        contacts_app_launched();
    }
    if (event_code == LV_EVENT_SCREEN_UNLOAD_START)
    {
    }
    if (event_code == LV_EVENT_SCREEN_UNLOADED)
    {
        
    }

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT)
    {
        ui_app_exit();
    }
}
#endif

void clearContactList()
{
#ifdef ENABLE_APP_CONTACTS
    if (!ui_contactScreen)
    {
        return;
    }
    lv_obj_clean(ui_contactList);
#endif
}

#ifdef ENABLE_APP_CONTACTS
/* Tapping a contact asks the phone to dial it. The row's own position in the list is the
   contact index the phone assigned when it synced them (they're appended in order by
   addContact), so lv_obj_get_index() is enough - no need to store the number on the
   watch or change addContact()'s signature. */
static void onContactClicked(lv_event_t *e)
{
    lv_obj_t *panel = (lv_obj_t *)lv_event_get_target(e);
    dial_contact((int)lv_obj_get_index(panel));
}
#endif

void addContact(const char *name, const char *number, bool sos)
{
#ifdef ENABLE_APP_CONTACTS
    if (!ui_contactScreen)
    {
        return;
    }
    lv_obj_t *ui_contactPanel = lv_obj_create(ui_contactList);
    lv_obj_add_flag(ui_contactPanel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_contactPanel, onContactClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(ui_contactPanel, UI_SCALE_PX(200));
    lv_obj_set_height(ui_contactPanel, UI_SCALE_PX(47));
    lv_obj_set_align(ui_contactPanel, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_contactPanel, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_radius(ui_contactPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_contactPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_contactPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_contactPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_contactPanel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_contactPanel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(ui_contactPanel, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_contactPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_contactPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_contactPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_contactPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_contactIcon = lv_image_create(ui_contactPanel);
    ui_image_src(ui_contactIcon, sos ? &ui_img_sos_contact_png : &ui_img_contact_png);
    lv_obj_set_width(ui_contactIcon, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_contactIcon, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_contactIcon, UI_SCALE_PX(10));
    lv_obj_set_y(ui_contactIcon, 0);
    lv_obj_set_align(ui_contactIcon, LV_ALIGN_LEFT_MID);
    lv_obj_add_flag(ui_contactIcon, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_remove_flag(ui_contactIcon, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    /* Grow the avatar with the panel, same trick the app/settings lists use. */
    ui_image_scale(ui_contactIcon, (256 * UI_SCALE) / 256);

    lv_obj_t *ui_contactNumber = lv_label_create(ui_contactPanel);
    lv_obj_set_width(ui_contactNumber, UI_SCALE_PX(140));
    lv_obj_set_height(ui_contactNumber, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_contactNumber, UI_SCALE_PX(50));
    lv_obj_set_y(ui_contactNumber, UI_SCALE_PX(-5));
    lv_obj_set_align(ui_contactNumber, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_text_font(ui_contactNumber, CONTACT_NUMBER_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_contactNumber, number);

    lv_obj_t *ui_contactName = lv_label_create(ui_contactPanel);
    lv_obj_set_width(ui_contactName, UI_SCALE_PX(140));
    lv_obj_set_height(ui_contactName, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_contactName, UI_SCALE_PX(50));
    lv_obj_set_y(ui_contactName, UI_SCALE_PX(3));
    /* Names longer than the row scroll back and forth on their own (already how the
       template had it - kept, and now with a properly scaled width so short names sit
       still instead of everything scrolling). */
    lv_label_set_long_mode(ui_contactName, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(ui_contactName, CONTACT_NAME_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_contactName, name);
#endif
}

void setNoContacts()
{
#ifdef ENABLE_APP_CONTACTS
    if (!ui_contactScreen)
    {
        return;
    }
    lv_obj_clean(ui_contactList);
    lv_obj_t *info = lv_label_create(ui_contactList);
    lv_obj_set_width(info, 180);
    lv_obj_set_y(info, 20);
    lv_obj_set_height(info, LV_SIZE_CONTENT); /// 1
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(info, "No hay contactos disponibles. Agrega contactos desde la app Navi Moto y sincronízalos");
#endif
}

void ui_contactScreen_screen_init()
{

#ifdef ENABLE_APP_CONTACTS
    ui_contactScreen = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_contactScreen, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_bg_color(ui_contactScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_contactScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_contactList = lv_obj_create(ui_contactScreen);
    lv_obj_set_width(ui_contactList, lv_pct(100));
    lv_obj_set_height(ui_contactList, lv_pct(100));
    lv_obj_set_align(ui_contactList, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_contactList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_contactList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_contactList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_contactList, LV_DIR_VER);
    lv_obj_set_style_bg_color(ui_contactList, lv_color_hex(0x070707), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_contactList, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_contactList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_contactList, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_contactList, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_contactList, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_contactList, 100, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_contactList, onScroll, LV_EVENT_SCROLL, NULL);

    lv_obj_add_event_cb(ui_contactScreen, ui_event_contactScreen, LV_EVENT_ALL, NULL);


    setNoContacts();


#endif
}
