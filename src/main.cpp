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
    "c4:7a:7f:1b:57:30",
    "d6:91:24:de:9b:e6",
    "fc:6a:b1:1d:5f:2d",
    "f2:ca:ed:d5:6d:ca",
};

static int getSensorReadingMapIndex(const char *mac) {
    for (int index = 0; index < SENSOR_COUNT; index += 1) {
        if (strcasecmp(WHITELISTED_SENSOR_MAC_ADDRESSES[index], mac) == 0) {
            return index;
        }
    }

    return -1;
}

static InMemoryRecord records[SENSOR_COUNT];
static portMUX_TYPE mutexLock = portMUX_INITIALIZER_UNLOCKED;

static void recordPacket(const int sensorIndex, const DataPacket &dataPacket, const int rssi) {
    portENTER_CRITICAL(&mutexLock);
    records[sensorIndex].dataPacket = dataPacket;
    records[sensorIndex].rssi = rssi;
    records[sensorIndex].lastUpdated = millis();
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
    ui_tick();
    lv_timer_handler();
    delay(5);
}
