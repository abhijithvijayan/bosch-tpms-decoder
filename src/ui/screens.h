#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *main_container;
    lv_obj_t *header;
    lv_obj_t *obj0;
    lv_obj_t *toast;
    lv_obj_t *obj1;
    lv_obj_t *cards;
    lv_obj_t *front_left_card;
    lv_obj_t *top_row;
    lv_obj_t *obj2;
    lv_obj_t *middle_row;
    lv_obj_t *obj3;
    lv_obj_t *obj4;
    lv_obj_t *bottom_row;
    lv_obj_t *obj5;
    lv_obj_t *battery_group;
    lv_obj_t *obj6;
    lv_obj_t *battery;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *front_right_card;
    lv_obj_t *top_row;
    lv_obj_t *obj9;
    lv_obj_t *middle_row;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *bottom_row;
    lv_obj_t *obj12;
    lv_obj_t *battery_group;
    lv_obj_t *obj13;
    lv_obj_t *battery;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *rear_left_card;
    lv_obj_t *top_row;
    lv_obj_t *obj16;
    lv_obj_t *middle_row;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *bottom_row;
    lv_obj_t *obj19;
    lv_obj_t *battery_group;
    lv_obj_t *obj20;
    lv_obj_t *battery;
    lv_obj_t *obj21;
    lv_obj_t *obj22;
    lv_obj_t *rear_right_card;
    lv_obj_t *top_row;
    lv_obj_t *obj23;
    lv_obj_t *obj24;
    lv_obj_t *middle_row;
    lv_obj_t *obj25;
    lv_obj_t *obj26;
    lv_obj_t *bottom_row;
    lv_obj_t *obj27;
    lv_obj_t *battery_group;
    lv_obj_t *obj28;
    lv_obj_t *battery;
    lv_obj_t *obj29;
    lv_obj_t *obj30;
} objects_t;

extern objects_t objects;

void create_screen_main();
void delete_screen_main();
void tick_screen_main();

void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/