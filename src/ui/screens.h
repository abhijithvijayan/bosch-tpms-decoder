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
    lv_obj_t *header_label;
    lv_obj_t *header_toast_container;
    lv_obj_t *header_toast_label;
    lv_obj_t *cards;
    lv_obj_t *sensor_1_card;
    lv_obj_t *sensor_1_card_top_row;
    lv_obj_t *sensor_1_card_top_row_label;
    lv_obj_t *sensor_1_card_top_row_temperature;
    lv_obj_t *sensor_1_card_middle_row;
    lv_obj_t *sensor_1_card_middle_row_pressure;
    lv_obj_t *sensor_1_card_middle_row_pressure_unit;
    lv_obj_t *sensor_1_card_bottom_row;
    lv_obj_t *sensor_1_card_bottom_row_last_updated;
    lv_obj_t *sensor_1_card_bottom_row_battery_group;
    lv_obj_t *sensor_1_card_bottom_row_battery_percentage;
    lv_obj_t *sensor_1_card_bottom_row_battery_icon;
    lv_obj_t *sensor_1_card_bottom_row_battery_icon_bar;
    lv_obj_t *sensor_1_card_bottom_row_battery_icon_nub;
    lv_obj_t *sensor_2_card;
    lv_obj_t *sensor_2_card_top_row;
    lv_obj_t *sensor_2_card_top_row_label;
    lv_obj_t *sensor_2_card_top_row_temperature;
    lv_obj_t *sensor_2_card_middle_row;
    lv_obj_t *sensor_2_card_middle_row_pressure;
    lv_obj_t *sensor_2_card_middle_row_pressure_unit;
    lv_obj_t *sensor_2_card_bottom_row;
    lv_obj_t *sensor_2_card_bottom_row_last_updated;
    lv_obj_t *sensor_2_card_bottom_row_battery_group;
    lv_obj_t *sensor_2_card_bottom_row_battery_percentage;
    lv_obj_t *sensor_2_card_bottom_row_battery_icon;
    lv_obj_t *sensor_2_card_bottom_row_battery_icon_bar;
    lv_obj_t *sensor_2_card_bottom_row_battery_icon_nub;
    lv_obj_t *sensor_3_card;
    lv_obj_t *sensor_3_card_top_row;
    lv_obj_t *sensor_3_card_top_row_label;
    lv_obj_t *sensor_3_card_top_row_temperature;
    lv_obj_t *sensor_3_card_middle_row;
    lv_obj_t *sensor_3_card_middle_row_pressure;
    lv_obj_t *sensor_3_card_middle_row_pressure_unit;
    lv_obj_t *sensor_3_card_bottom_row;
    lv_obj_t *sensor_3_card_bottom_row_last_updated;
    lv_obj_t *sensor_3_card_bottom_row_battery_group;
    lv_obj_t *sensor_3_card_bottom_row_battery_percentage;
    lv_obj_t *sensor_3_card_bottom_row_battery_icon;
    lv_obj_t *sensor_3_card_bottom_row_battery_icon_bar;
    lv_obj_t *sensor_3_card_bottom_row_battery_icon_nub;
    lv_obj_t *sensor_4_card;
    lv_obj_t *sensor_4_card_top_row;
    lv_obj_t *sensor_4_card_top_row_label;
    lv_obj_t *sensor_4_card_top_row_temperature;
    lv_obj_t *sensor_4_card_middle_row;
    lv_obj_t *sensor_4_card_middle_row_pressure;
    lv_obj_t *sensor_4_card_middle_row_pressure_unit;
    lv_obj_t *sensor_4_card_bottom_row;
    lv_obj_t *sensor_4_card_bottom_row_last_updated;
    lv_obj_t *sensor_4_card_bottom_row_battery_group;
    lv_obj_t *sensor_4_card_bottom_row_battery_percentage;
    lv_obj_t *sensor_4_card_bottom_row_battery_icon;
    lv_obj_t *sensor_4_card_bottom_row_battery_icon_bar;
    lv_obj_t *sensor_4_card_bottom_row_battery_icon_nub;
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