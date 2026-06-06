#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <ui/ui.h>

// Onboard 1.47" ST7789 wiring
// https://docs.waveshare.com/ESP32-C6-LCD-1.47?variant=ESP32-C6-LCD-1.47
#define LCD_SCLK 7
#define LCD_MOSI 6
#define LCD_DC   15
#define LCD_CS   14
#define LCD_RST  21
#define LCD_BL   22

#define LANDSCAPE_SCREEN_WIDTH 320
#define LANDSCAPE_SCREEN_HEIGHT 172

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus,
    LCD_RST,
    1, // Landscape mode
    true, // IPS Panel
    172, 320, // portrait resolution
    // The ST7789 is a generic driver IC with 240×320 pixels of internal memory (GRAM).
    // The panel manufacturer wired those 172 columns(172 pixels wide display) to the middle of the chip's 240 column outputs
    // (240 - 172) / 2 = 34
    34, 0,
    34, 0
);

// widgets (labels, cards)
//             ↓ rendered by
// LVGL  →  tempCanvasBuffer (RAM)  →  flushIntoDisplay()  →  gfx  →  SPI  →  ST7789 glass

// render buffer: LVGL draws into this, 40 lines at a time (~25 KB).
// Partial-buffer mode: small RAM cost, screen updates in strips.
static lv_color_t tempCanvasBuffer[LANDSCAPE_SCREEN_WIDTH * 40];

// LVGL needs a clock source to run animations/timers, which on Arduino is just millis()
static uint32_t tickCallback() {
    return millis();
}

// LVGL completed rendering -> flush it to the ST7789
static void flushIntoDisplay(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    const uint32_t width = lv_area_get_width(area);
    const uint32_t height = lv_area_get_height(area);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(px_map), width, height);
    lv_display_flush_ready(display); // tell LVGL it may reuse the buffer
}

void setup() {
    Serial.begin(115200);

    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH); // backlight on — screen stays black without this

    gfx->begin(); // SPI init + ST7789 init sequence + reset pulse
    gfx->fillScreen(RGB565_BLACK);

    lv_init();
    lv_tick_set_cb(tickCallback);

    lv_display_t *display = lv_display_create(LANDSCAPE_SCREEN_WIDTH, LANDSCAPE_SCREEN_HEIGHT);
    lv_display_set_flush_cb(display, flushIntoDisplay);
    lv_display_set_buffers(display, tempCanvasBuffer, nullptr, sizeof(tempCanvasBuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ui_init();
}

void loop() {
    ui_tick();
    lv_timer_handler();
    delay(5);
}
