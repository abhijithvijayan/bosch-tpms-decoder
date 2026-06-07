#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <NimBLEDevice.h>
#include <mbedtls/aes.h>
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

struct DataPacket {
    bool ok;
    int temperatureInCelsius;
    int pressureInPsi;
    int batteryPercent;
};

static const NimBLEUUID TPMS_SERVICE_UUID(static_cast<uint16_t>(0xFFE0)); // service UUID every Bosch sensor advertises
constexpr int TPMS_INVALID = 0xFF4C; // per-field "no reading" sentinel
// Static AES-128 key, hard-coded in every Bosch TPMS sensor.
static constexpr uint8_t kAesKey[16] = { '#','@','T','r','l','2','0','1','8','-','l','e','s','p','l','$' };

struct InMemoryRecord {
    DataPacket dataPacket;
    int rssi; // Radio metadata
    uint32_t lastUpdated;
};

constexpr size_t SENSOR_COUNT = 4;

static const char *WHITELISTED_SENSOR_MAC_ADDRESSES[SENSOR_COUNT] = {
    "f2:ca:ed:d5:6d:ca", // default FL
    "fc:6a:b1:1d:5f:2d", // default FR
    "d6:91:24:de:9b:e6", // default RL
    "c4:7a:7f:1b:57:30", // default RR
};

static int getSensorReadingMapIndex(const char *mac) {
    for (int index = 0; index < SENSOR_COUNT; index += 1) {
        if (strcasecmp(WHITELISTED_SENSOR_MAC_ADDRESSES[index], mac) == 0) {
            return index;
        }
    }

    return -1;
}

static InMemoryRecord inMemoryCache[SENSOR_COUNT];
static portMUX_TYPE mutexLock = portMUX_INITIALIZER_UNLOCKED;

static void recordPacket(const int sensorIndex, const DataPacket &dataPacket, const int rssi) {
    portENTER_CRITICAL(&mutexLock);
    inMemoryCache[sensorIndex].dataPacket = dataPacket;
    inMemoryCache[sensorIndex].rssi = rssi;
    inMemoryCache[sensorIndex].lastUpdated = millis();
    portEXIT_CRITICAL(&mutexLock);
}

// little-endian u16 from a 2-byte slice
static uint16_t leU16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}

// sign-magnitude temperature: > 0x8000 means negative, value is (raw - 0x8000); /100 -> whole °C
static int decodeTemp(const uint16_t raw) {
    if (raw > 0x8000) {
        return -(int)((raw - 0x8000) / 100);
    }

    return raw / 100;
}

// pressure: raw / 100 (native PSI)
static int decodePressure(const uint16_t raw) {
    return raw / 100;
}

// AES-128/ECB/NoPadding, single 16-byte block
static bool aesDecryptBlock(const uint8_t cipher[16], uint8_t plain[16]) {
    mbedtls_aes_context ctx; mbedtls_aes_init(&ctx);
    const bool ok = mbedtls_aes_setkey_dec(&ctx, kAesKey, 128) == 0
                 && mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, cipher, plain) == 0;
    mbedtls_aes_free(&ctx);

    return ok;
}

static DataPacket decodeFrame(const uint8_t *rawAdv, size_t len) {
    DataPacket dataPacket {
        false,
        TPMS_INVALID,
        TPMS_INVALID,
        0
    };

    // ciphertext lives at [15..31]
    if (len < 31) {
        return dataPacket;
    }

    uint8_t decryptedPayload[16];
    if (!aesDecryptBlock(rawAdv + 15, decryptedPayload)) {
        return dataPacket;
    }

    // structural check: header 0x16, trailer 0x15 0x14 — rejects non-TPMS junk on 0xFFE0
    if (decryptedPayload[0] != 0x16 || decryptedPayload[14] != 0x15 || decryptedPayload[15] != 0x14) {
        return dataPacket;
    }

    dataPacket.ok = true;

    uint16_t t = leU16(&decryptedPayload[1]);        // temp, valid [-40, 125] °C
    if (t != 0xFFFF) {
        const int c = decodeTemp(t);
        dataPacket.temperatureInCelsius = (c < -40 || c > 125) ? TPMS_INVALID : c;
    }

    uint16_t pr = leU16(&decryptedPayload[3]);       // pressure, valid [0, 217] PSI
    if (pr != 0xFFFF) {
        const int p = decodePressure(pr);
        dataPacket.pressureInPsi = (p < 0 || p > 217) ? TPMS_INVALID : p;
    }

    dataPacket.batteryPercent = min(decryptedPayload[5] & 0xFF, 100);   // battery, clamp ≤100

    return dataPacket;
}

class TpmsScanCallback : public NimBLEScanCallbacks {
    public : void onResult(const NimBLEAdvertisedDevice *device) override {
        if (!device->isAdvertisingService(TPMS_SERVICE_UUID)) {
            return;
        }

        const std::string mac = device->getAddress().toString();
        const int inMemoryRecordIndex = getSensorReadingMapIndex(mac.c_str());
        if (inMemoryRecordIndex < 0) {
            return; // This TPMS Sensor is not whitelisted
        }

        const std::vector<uint8_t> &payload = device->getPayload();
        DataPacket dataPacket = decodeFrame(payload.data(), payload.size());
        if (!dataPacket.ok) {
            return;
        }

        recordPacket(inMemoryRecordIndex, dataPacket, device->getRSSI());

        Serial.printf("[%s] temp %d C  press %d PSI  batt %d%%  RSSI %d\n",
                      device->getAddress().toString().c_str(),
                      dataPacket.temperatureInCelsius, dataPacket.pressureInPsi, dataPacket.batteryPercent, device->getRSSI());
    }
};

#define COL_BG        0x0B0E14   // screen background
#define COL_CARD      0x161D2B   // card fill, normal
#define COL_CARD_BD   0x232C40   // card border, normal
#define COL_ALERT_BG  0x2A1113   // card fill, low/puncture
#define COL_ALERT_BD  0x7F1D1D   // card border, low/puncture
#define COL_WARN_BG   0x2A2410   // card fill, overinflated
#define COL_WARN_BD   0x7A5A12   // card border, overinflated
#define COL_TXT       0xFFFFFF   // psi number, temp
#define COL_MUTE      0x8A93A6   // "psi" unit
#define COL_DIM       0x5B6373   // age label, idle battery
#define COL_POS       0x5DA3FF   // FL/FR/RL/RR (blue)
#define COL_RED       0xFF5A5A   // low psi, battery ≤20%
#define COL_CRITRED   0xD32030   // battery ≤10%
#define COL_AMBER     0xFACC15   // high psi, battery ≤40%
#define COL_GREEN     0x4ADE80   // battery OK
#define COL_PILL_OK   0x166534   // header pill, all-OK green
#define COL_PILL_BAD  0xB91C1C   // header pill, alert red

#define PLACARD_PSI       32   // recommended cold pressure — sticker on driver's door
#define SIDEWALL_MAX_PSI  51   // tyre's printed limit — "MAX INFLATION PRESSURE" on sidewall

// LOW = 20% below recommended - EU rule ECE R141
#define PSI_LOW   (PLACARD_PSI * 80 / 100)    // 32 -> 25

// HIGH = 40% above recommended. Hot tyres legitimately run ~15% over (32 -> ~37),
// so +40% is far enough out to never false-alarm, yet still below the sidewall limit.
#define PSI_HIGH  (PLACARD_PSI * 140 / 100)   // 32 -> 44

// compile-time sanity: refuse to build if the math ever lands somewhere unsafe
static_assert(PSI_HIGH < SIDEWALL_MAX_PSI, "PSI_HIGH must stay below the tyre's printed max");
static_assert(PSI_LOW  < PLACARD_PSI,      "PSI_LOW must be below the recommended pressure");

#define STALE_MS  (30UL * 60UL * 1000UL)      // 30 min without a frame means data is stale

enum CardState {
    CARD_IDLE,
    CARD_LOW,
    CARD_HIGH,
    CARD_STALE,
    CARD_NORMAL
};

static CardState getCardState(const InMemoryRecord &record, const uint32_t now) {
    if (!record.dataPacket.ok) {
        return CARD_IDLE;
    }

    const int psi = record.dataPacket.pressureInPsi;
    if (record.dataPacket.pressureInPsi != TPMS_INVALID && psi <= PSI_LOW) {
        return CARD_LOW;
    }

    if (record.dataPacket.pressureInPsi != TPMS_INVALID && psi >= PSI_HIGH) {
        return CARD_HIGH;
    }

    if (now - record.lastUpdated > STALE_MS) {
        return CARD_STALE;
    }

    return CARD_NORMAL;
}

// stale look = base color blended 0.55 over the screen bg
static lv_color_t staleFade(const uint32_t color) {
    return lv_color_mix(lv_color_hex(color), lv_color_hex(COL_BG), 0.55 * 255);
}

struct CardStyleSpec {
    uint32_t bg;
    uint32_t border;
    uint32_t psi;
    uint32_t unit;
    uint32_t pos;
};

static const CardStyleSpec CARD_STYLE[] = {
    /* IDLE */   { .bg = COL_CARD,     .border = COL_CARD_BD,  .psi = COL_TXT,   .unit = COL_MUTE, .pos = COL_POS },
    /* LOW */    { .bg = COL_ALERT_BG, .border = COL_ALERT_BD, .psi = COL_RED,   .unit = COL_MUTE, .pos = COL_RED },
    /* HIGH */   { .bg = COL_WARN_BG,  .border = COL_WARN_BD,  .psi = COL_AMBER, .unit = COL_MUTE, .pos = COL_AMBER },
    /* STALE */  { .bg = COL_CARD,     .border = COL_CARD_BD,  .psi = COL_TXT,   .unit = COL_MUTE, .pos = COL_POS },
    /* NORMAL */ { .bg = COL_CARD,     .border = COL_CARD_BD,  .psi = COL_TXT,   .unit = COL_MUTE, .pos = COL_POS },
};

static int sensorIndexForPosition[SENSOR_COUNT] = {3, 2, 1, 0}; // Current state of sensors in wheels

struct CardWidgets {
    lv_obj_t *card;
    lv_obj_t *posLabel;
    lv_obj_t *temperature;
    lv_obj_t *pressure;
    lv_obj_t *unit;
    lv_obj_t *lastUpdated;
    lv_obj_t *batteryPercentage;
    lv_obj_t *batteryBar;   // the lv_bar inside the icon — value + fill + outline
    lv_obj_t *batteryNib;   // the terminal bump — recolored with the bar
};

static CardWidgets cards[SENSOR_COUNT];

static void bindCards() {
  cards[0] = {
      .card              = objects.sensor_1_card,
      .posLabel          = objects.sensor_1_card_top_row_label,
      .temperature       = objects.sensor_1_card_top_row_temperature,
      .pressure          = objects.sensor_1_card_middle_row_pressure,
      .unit              = objects.sensor_1_card_middle_row_pressure_unit,
      .lastUpdated       = objects.sensor_1_card_bottom_row_last_updated,
      .batteryPercentage = objects.sensor_1_card_bottom_row_battery_percentage,
      .batteryBar        = objects.sensor_1_card_bottom_row_battery_icon_bar,
      .batteryNib        = objects.sensor_1_card_bottom_row_battery_icon_nub,
  };
  cards[1] = {
      .card              = objects.sensor_2_card,
      .posLabel          = objects.sensor_2_card_top_row_label,
      .temperature       = objects.sensor_2_card_top_row_temperature,
      .pressure          = objects.sensor_2_card_middle_row_pressure,
      .unit              = objects.sensor_2_card_middle_row_pressure_unit,
      .lastUpdated       = objects.sensor_2_card_bottom_row_last_updated,
      .batteryPercentage = objects.sensor_2_card_bottom_row_battery_percentage,
      .batteryBar        = objects.sensor_2_card_bottom_row_battery_icon_bar,
      .batteryNib        = objects.sensor_2_card_bottom_row_battery_icon_nub,
  };
  cards[2] = {
      .card              = objects.sensor_3_card,
      .posLabel          = objects.sensor_3_card_top_row_label,
      .temperature       = objects.sensor_3_card_top_row_temperature,
      .pressure          = objects.sensor_3_card_middle_row_pressure,
      .unit              = objects.sensor_3_card_middle_row_pressure_unit,
      .lastUpdated       = objects.sensor_3_card_bottom_row_last_updated,
      .batteryPercentage = objects.sensor_3_card_bottom_row_battery_percentage,
      .batteryBar        = objects.sensor_3_card_bottom_row_battery_icon_bar,
      .batteryNib        = objects.sensor_3_card_bottom_row_battery_icon_nub,
  };
  cards[3] = {
      .card              = objects.sensor_4_card,
      .posLabel          = objects.sensor_4_card_top_row_label,
      .temperature       = objects.sensor_4_card_top_row_temperature,
      .pressure          = objects.sensor_4_card_middle_row_pressure,
      .unit              = objects.sensor_4_card_middle_row_pressure_unit,
      .lastUpdated       = objects.sensor_4_card_bottom_row_last_updated,
      .batteryPercentage = objects.sensor_4_card_bottom_row_battery_percentage,
      .batteryBar        = objects.sensor_4_card_bottom_row_battery_icon_bar,
      .batteryNib        = objects.sensor_4_card_bottom_row_battery_icon_nub,
  };
}

static void applyCardStyle(const size_t pos, const CardState state) {
    const CardStyleSpec &spec = CARD_STYLE[state];
    const bool stale = (state == CARD_STALE);

    // local helper: raw palette color -> lv color, faded when stale
    auto getColor = [stale](const uint32_t color) {
        return stale ? staleFade(color) : lv_color_hex(color);
    };

    lv_obj_set_style_bg_color(cards[pos].card, getColor(spec.bg), LV_PART_MAIN);
    lv_obj_set_style_border_color(cards[pos].card, getColor(spec.border), LV_PART_MAIN);
    lv_obj_set_style_text_color(cards[pos].pressure, getColor(spec.psi), LV_PART_MAIN);
    lv_obj_set_style_text_color(cards[pos].unit, getColor(spec.unit), LV_PART_MAIN);
    lv_obj_set_style_text_color(cards[pos].posLabel, getColor(spec.pos), LV_PART_MAIN);
    lv_obj_set_style_text_color(cards[pos].temperature, getColor(COL_TXT), LV_PART_MAIN);
}

static void applyBatteryStyle(const size_t pos, const int percent, const CardState state) {
    uint32_t color = percent <= 10 ? COL_CRITRED
       : percent <= 20 ? COL_RED
       : percent <= 40 ? COL_AMBER
       : COL_GREEN;
    if (state == CARD_IDLE) {
        color = COL_DIM;   // no data yet: neutral gray
    }

    const lv_color_t c = (state == CARD_STALE) ? staleFade(color) : lv_color_hex(color);
    lv_obj_set_style_text_color(cards[pos].batteryPercentage, c, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cards[pos].batteryBar, c, LV_PART_INDICATOR);  // the fill
    lv_obj_set_style_border_color(cards[pos].batteryBar, c, LV_PART_MAIN);   // the outline
    lv_obj_set_style_bg_color(cards[pos].batteryNib, c, LV_PART_MAIN);       // nub follows
}

// age label: dim normally; faded amber when stale
static void applyAgeStyle(const size_t pos, const CardState state) {
    const lv_color_t c = (state == CARD_STALE) ? staleFade(COL_AMBER) : lv_color_hex(COL_DIM);
    lv_obj_set_style_text_color(cards[pos].lastUpdated, c, LV_PART_MAIN);
}

// set a label's text only if it actually changed
static void setLabelText(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

void refreshUI() {
    // Take a copy of the current records so that we don't try to read mid write action.
    InMemoryRecord snapshot[SENSOR_COUNT];
    portENTER_CRITICAL(&mutexLock);
    memcpy(snapshot, inMemoryCache, sizeof(inMemoryCache));
    portEXIT_CRITICAL(&mutexLock);

    const uint32_t now = millis();
    char text[16];
    int highCount = 0;
    int lowCount = 0;
    int idleCount = 0;
    int staleCount = 0;

    for (size_t pos = 0; pos < SENSOR_COUNT; pos += 1) {
        const InMemoryRecord &record = snapshot[sensorIndexForPosition[pos]];

        const CardState state = getCardState(record, now);
        if (state == CARD_HIGH) {
            highCount += 1;
        } else if (state == CARD_LOW) {
            lowCount += 1;
        } else if (state == CARD_STALE) {
            staleCount += 1;
        } else if (state == CARD_IDLE) {
            idleCount += 1;
        }

        if (record.dataPacket.ok) {
            if (record.dataPacket.pressureInPsi != TPMS_INVALID) {
                snprintf(text, sizeof(text), "%d", record.dataPacket.pressureInPsi);
            } else {
                strlcpy(text, "--", sizeof(text));
            }
            setLabelText(cards[pos].pressure, text);

            if (record.dataPacket.temperatureInCelsius != TPMS_INVALID) {
                snprintf(text, sizeof(text), "%d°C", record.dataPacket.temperatureInCelsius);
            } else {
                strlcpy(text, "--°C", sizeof(text));
            }
            setLabelText(cards[pos].temperature, text);

            snprintf(text, sizeof(text), "%d%%", record.dataPacket.batteryPercent);
            setLabelText(cards[pos].batteryPercentage, text);
            lv_bar_set_value(cards[pos].batteryBar, record.dataPacket.batteryPercent, LV_ANIM_OFF);

            const uint32_t age = now - record.lastUpdated;
            if (age < 60000UL) {
                snprintf(text, sizeof(text), "%lus", age / 1000UL);
            }
            else if (age < 3600000UL) {
                snprintf(text, sizeof(text), "%lum", age / 60000UL);
            }
            else {
                snprintf(text, sizeof(text), "%luh", age / 3600000UL);
            }
            setLabelText(cards[pos].lastUpdated, text);
        } else {
            setLabelText(cards[pos].pressure, "--");
            setLabelText(cards[pos].temperature, "--°C");
            setLabelText(cards[pos].batteryPercentage, "--%");
            setLabelText(cards[pos].lastUpdated, "--");
            lv_bar_set_value(cards[pos].batteryBar, 0, LV_ANIM_OFF);
        }

        applyCardStyle(pos, state);
        applyBatteryStyle(
            pos,
            // For now, if we don't have data for battery, we simply show 0%
            record.dataPacket.ok ? record.dataPacket.batteryPercent : 0,
            state
        );
        applyAgeStyle(pos, state);
    }

    if (staleCount + idleCount == SENSOR_COUNT) {
        lv_obj_add_flag(objects.header_toast_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        uint32_t pillColor;
        const char *pillText;
        if (lowCount > 0 && highCount > 0) {
            pillColor = COL_PILL_BAD;
            pillText = "Multiple Issues Detected";
        }
        else if (lowCount) {
            pillColor = COL_PILL_BAD;
            pillText = "Low Pressure Detected";
        }
        else if (highCount) {
            pillColor = COL_WARN_BD;
            pillText = "High Pressure Detected";
        }
        else {
            pillColor = COL_PILL_OK;
            pillText = "All tires OK";
        }

        lv_obj_set_style_bg_color(objects.header_toast_container, lv_color_hex(pillColor), LV_PART_MAIN);
        setLabelText(objects.header_toast_label, pillText);
        lv_obj_remove_flag(objects.header_toast_container, LV_OBJ_FLAG_HIDDEN);
    }
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
    bindCards();
    refreshUI();

    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(new TpmsScanCallback());
    scan->setActiveScan(false); // We only get advertisements passively
    scan->setInterval(160); // 160 × 0.625ms = 100ms cycle
    scan->setWindow(160); // window == interval -> radio listening 100% of the time
    scan->setDuplicateFilter(false); // deliver EVERY advertisement, even from known devices
    scan->start(0, false); // 0 = scan forever
}

void loop() {
    static uint32_t lastRefreshed = 0;
    const uint32_t now = millis();

    if (now - lastRefreshed >= 400) {
        lastRefreshed = now;
        refreshUI();
    }

    ui_tick();
    lv_timer_handler();
    delay(5);
}
