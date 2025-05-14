#include <cstdint>
#include <cstring>
#include <cstdio>
#include <Arduino.h>
#include <HardwareSerial.h>
#include <cstdlib>
#include <cstdarg>
#include <string>  // for std::string

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>

#include "NimBLEDevice.h"
#include <WiFi.h>           // Arduino WiFi init
#include <esp_timer.h>      // for esp_timer_get_time()
#include <esp_wifi.h>       // for promiscuous sniffing

// UART definitions
#define UART_BUF_SIZE 1024
#define UART_PORT     UART_NUM_1
#define UART_TX_PIN   6
#define UART_RX_PIN   5

// Baseline timing
static const uint32_t BASELINE_MS = 300000; // 5 minutes
static uint32_t baselineStartMs = 0;
static volatile bool isBaseline = true;

// Counts
static volatile size_t currBLECount   = 0,
                      currWiFiCount  = 0,
                      currProbeCount = 0;

// Limits
#define MAX_BLE   200
#define MAX_WIFI  200
#define MAX_PROBE 500

// Storage arrays
static char bleSeenArr[MAX_BLE][18];
static int  bleSeenCount = 0;
static char wifiSeenArr[MAX_WIFI][18];
static int  wifiSeenCount = 0;
static char probeSeenArr[MAX_PROBE][18];
static int  probeSeenCount = 0;

// Static buffer for Wi-Fi scan records (avoid malloc/free)
static wifi_ap_record_t apsBuf[MAX_WIFI];

// Baseline copies
static char baselineBleArr[MAX_BLE][18];
static int  baselineBleCount = 0;
static char baselineWifiArr[MAX_WIFI][18];
static int  baselineWifiCount = 0;
static char baselineProbeArr[MAX_PROBE][18];
static int  baselineProbeCount = 0;

// Logging
static const char* TAG = "Deepwoods";
static const char* DETECT_PREFIX = "Detected non-baseline";

struct PrintMsg { char buf[128]; };
static QueueHandle_t printQ = nullptr;

struct ProbeEvent { char mac[18]; };
static QueueHandle_t probeQ = nullptr;

//--------------------------------------------------------------------------------
// Enqueue formatted log + UART output
//--------------------------------------------------------------------------------
static void enqueueFmt(const char* fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (strncmp(buf, DETECT_PREFIX, strlen(DETECT_PREFIX)) == 0) {
        // Non-baseline detections -> UART1 only via Serial1
        Serial1.println(buf);
        // also mirror detections to USB serial
        Serial.printf("%s\r\n", buf);
    } else {
        // All other logs -> USB serial via Arduino Serial
        Serial.printf("%s\r\n", buf);
    }
}

//--------------------------------------------------------------------------------
// Wi-Fi probe sniffer callback
//--------------------------------------------------------------------------------
static void snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* hdr = p->payload;
    if (((hdr[0] >> 4) & 0xF) != 4) return;  // probe-req only

    ProbeEvent ev;
    snprintf(ev.mac, sizeof(ev.mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             hdr[10], hdr[11], hdr[12],
             hdr[13], hdr[14], hdr[15]);
    // Send from ISR, drop or wake higher priority task if needed
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(probeQ, &ev, &hpw);
    if (hpw) portYIELD_FROM_ISR();
}

//--------------------------------------------------------------------------------
// BLE advertised-device callback (NimBLE)
//--------------------------------------------------------------------------------
class MyScanCallbacks : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        char mac[18];
        // Convert the BLE address to string
        std::string addrStr = advertisedDevice->getAddress().toString();
        // Copy into C-style buffer
        strncpy(mac, addrStr.c_str(), sizeof(mac));
        mac[sizeof(mac)-1] = '\0';
        bool found = false;
        for (int i = 0; i < bleSeenCount; i++) {
            if (strcmp(bleSeenArr[i], mac) == 0) {
                found = true;
                break;
            }
        }
        if (!found && bleSeenCount < MAX_BLE) {
            strcpy(bleSeenArr[bleSeenCount++], mac);
            currBLECount = bleSeenCount;
            if (!isBaseline) {
                bool inBase = false;
                for (int j = 0; j < baselineBleCount; j++) {
                    if (strcmp(baselineBleArr[j], mac) == 0) {
                        inBase = true;
                        break;
                    }
                }
                if (!inBase) {
                    enqueueFmt("Detected non-baseline BLE: %s", mac);
                }
            }
        }
    }
    // optionally implement onDiscovered or onScanEnd if needed
};

//--------------------------------------------------------------------------------
// ProbeTask: process Wi-Fi probe events
//--------------------------------------------------------------------------------
static void ProbeTask(void*) {
    ProbeEvent ev;
    while (xQueueReceive(probeQ, &ev, portMAX_DELAY)) {
        bool found = false;
        for (int i = 0; i < probeSeenCount; i++) {
            if (strcmp(probeSeenArr[i], ev.mac) == 0) {
                found = true;
                break;
            }
        }
        if (!found && probeSeenCount < MAX_PROBE) {
            strcpy(probeSeenArr[probeSeenCount++], ev.mac);
            currProbeCount = probeSeenCount;
            if (!isBaseline) {
                bool inBase = false;
                for (int j = 0; j < baselineProbeCount; j++) {
                    if (strcmp(baselineProbeArr[j], ev.mac) == 0) {
                        inBase = true;
                        break;
                    }
                }
                if (!inBase) {
                    enqueueFmt("Detected non-baseline ProbeReq: %s", ev.mac);
                }
            }
        }
    }
}

//--------------------------------------------------------------------------------
// ChannelHopTask: cycle through Wi-Fi channels
//--------------------------------------------------------------------------------
static void ChannelHopTask(void*) {
    uint8_t ch = 1;
    while (true) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        ch = (ch % 13) + 1;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//--------------------------------------------------------------------------------
// WiFiTask: active scan + promiscuous sniff
//--------------------------------------------------------------------------------
static void WiFiTask(void*) {
    wifiSeenCount = 0;
    while (true) {
        WiFi.scanNetworks(true, true, false, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num > 0) {
            // use static buffer
            esp_wifi_scan_get_ap_records(&ap_num, apsBuf);
            for (int i = 0; i < ap_num; i++) {
                const uint8_t* b = apsBuf[i].bssid;
                char mac[18];
                snprintf(mac, sizeof(mac),
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         b[0], b[1], b[2], b[3], b[4], b[5]);
                bool found = false;
                for (int j = 0; j < wifiSeenCount; j++) {
                    if (strcmp(wifiSeenArr[j], mac) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found && wifiSeenCount < MAX_WIFI) {
                    strcpy(wifiSeenArr[wifiSeenCount++], mac);
                    currWiFiCount = wifiSeenCount;
                    if (!isBaseline) {
                        bool inBase = false;
                        for (int k = 0; k < baselineWifiCount; k++) {
                            if (strcmp(baselineWifiArr[k], mac) == 0) {
                                inBase = true;
                                break;
                            }
                        }
                        if (!inBase) {
                            enqueueFmt("Detected non-baseline WiFi: %s", mac);
                        }
                    }
                }
            }
        }

        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

//--------------------------------------------------------------------------------
// StatusTask: print baseline progress
//--------------------------------------------------------------------------------
static void StatusTask(void*) {
    size_t lastMin = 0;
    while (isBaseline) {
        enqueueFmt("Baseline progress: %u BLE, %u WiFi, %u ProbeReq",
                   (unsigned)currBLECount,
                   (unsigned)currWiFiCount,
                   (unsigned)currProbeCount);

        uint32_t elapsed = (esp_timer_get_time() / 1000) - baselineStartMs;
        size_t mins = elapsed / 60000;
        if (mins > lastMin) {
            size_t rem = (BASELINE_MS - elapsed + 59999) / 60000;
            if (rem > 0) {
                enqueueFmt("Baseline Scan has %u minutes remaining.", (unsigned)rem);
            }
            lastMin = mins;
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }

    enqueueFmt("Baseline complete: %u BLE, %u WiFi, %u ProbeReq",
               (unsigned)currBLECount,
               (unsigned)currWiFiCount,
               (unsigned)currProbeCount);
    enqueueFmt("=== Baseline complete ===");
    vTaskDelete(nullptr);
}

//--------------------------------------------------------------------------------
// BaselineTask: freeze baseline arrays after timeout
//--------------------------------------------------------------------------------
static void BaselineTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(BASELINE_MS));
    baselineBleCount = bleSeenCount;
    for (int i = 0; i < bleSeenCount; i++) {
        memcpy(baselineBleArr[i], bleSeenArr[i], 18);
    }
    baselineWifiCount = wifiSeenCount;
    for (int i = 0; i < wifiSeenCount; i++) {
        memcpy(baselineWifiArr[i], wifiSeenArr[i], 18);
    }
    baselineProbeCount = probeSeenCount;
    for (int i = 0; i < probeSeenCount; i++) {
        memcpy(baselineProbeArr[i], probeSeenArr[i], 18);
    }
    isBaseline = false;
    vTaskDelete(nullptr);
}

// Periodically clear NimBLE scan cache to prevent stale buildup
static void ClearBLECacheTask(void*) {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // clear every 60 seconds
        pScan->clearResults();
    }
}

void setup() {
    // disable brown-out detector
    REG_CLR_BIT(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_ENA);
    Serial.begin(115200);
    while (!Serial) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Initialize Serial1 for UART1 output on the defined pins
    Serial1.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    // give USB time to enumerate
    vTaskDelay(pdMS_TO_TICKS(100));
    // initial USB startup messages
    enqueueFmt("Deepwoods Device Detection");
    enqueueFmt("5 minute Baseline Scan Started");
    // give time for brown-out delay
    vTaskDelay(pdMS_TO_TICKS(5000));

    // record baseline start
    baselineStartMs = esp_timer_get_time() / 1000;

    // init NVS
    nvs_flash_init();

    // init UART1
    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, nullptr, 0);
    uart_param_config(UART_PORT, &uart_cfg);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    printQ = xQueueCreate(20, sizeof(PrintMsg));
    probeQ = xQueueCreate(100, sizeof(ProbeEvent));

    // start Wi-Fi tasks
    xTaskCreate(ChannelHopTask, "ChHop",   2048, nullptr, 1, nullptr);
    xTaskCreate(ProbeTask,      "Probe",   4096, nullptr, 1, nullptr);
    xTaskCreate(WiFiTask,       "WiFi",    8192, nullptr, 1, nullptr);
    xTaskCreate(StatusTask,     "Status",  4096, nullptr, 1, nullptr);
    xTaskCreate(BaselineTask,   "Baseline",4096, nullptr, 1, nullptr);
    xTaskCreate(ClearBLECacheTask, "ClearBLE", 2048, nullptr, 1, nullptr);

    // Initialize NimBLE
    NimBLEDevice::init("");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(new MyScanCallbacks(), true);
    pScan->setActiveScan(true);
    pScan->setInterval(45);
    pScan->setWindow(15);
    pScan->start(0, true); // continuous scan in background
}

void loop() {
    // Nothing to do here; FreeRTOS tasks handle everything
}
