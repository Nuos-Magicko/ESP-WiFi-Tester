#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "driver/uart.h"
#include "iperf.h" 

/* --- USER CONFIGURATION --- */
#define WIFI_SSID      "TEST"
#define WIFI_PASS      "12345678"
/* -------------------------- */

static const char *TAG = "wifi_tester";
bool is_connected = false;

// --- 1. WIFI HANDLER ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_connected = false;
        ESP_LOGI(TAG, "Disconnected. Retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        is_connected = true;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// --- 2. RSSI MONITOR TASK ---
void rssi_monitor_task(void *pvParameters) {
    wifi_ap_record_t ap_info;
    while (1) {
        if (is_connected && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            // Print JSON for Python
            printf("DATA:{\"rssi\": %d, \"ch\": %d}\n", ap_info.rssi, ap_info.primary);
        }
        vTaskDelay(pdMS_TO_TICKS(200)); 
    }
}

// --- 3. COMMAND PARSER ---
void run_iperf_client(char* ip_str) {
    iperf_cfg_t cfg;
    
    // 1. Zero out the configuration
    memset(&cfg, 0, sizeof(cfg));

    // 2. Set strict fields for Client Mode
    cfg.flag = IPERF_FLAG_CLIENT | IPERF_FLAG_TCP;
    
    // 3. Set Destination IP safely (Fixes the struct error)
    // inet_addr converts string "192.168.1.50" to integer
    uint32_t ip_int = inet_addr(ip_str);
    
    // Use the LwIP helper to set the address into the struct
    ip_addr_set_ip4_u32(&cfg.destination, ip_int);

    cfg.time = 6000;        // Duration in seconds
    cfg.interval = 1;       // Report every 1s
    
    ESP_LOGI(TAG, "Starting iPerf Client targeting: %s", ip_str);
    iperf_start(&cfg);
}

void console_task(void *pvParameters) {
    char line[128];
    while (1) {
        // Read from stdin (UART)
        if (fgets(line, sizeof(line), stdin) != NULL) {
            // Remove newline
            line[strcspn(line, "\n")] = 0;

            // Check if it's an iPerf command
            // Python sends: "iperf -c 192.168.1.50 -i 1 -t 6000"
            if (strncmp(line, "iperf -c", 8) == 0) {
                // Extract IP (simple parsing)
                char *ip_start = line + 9;
                char *ip_end = strchr(ip_start, ' ');
                if (ip_end != NULL) {
                    *ip_end = '\0'; // Terminate string at the space
                    run_iperf_client(ip_start);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    // NVS Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting ESP32 Wi-Fi Tester...");
    
    wifi_init_sta();
    
    // Start Tasks
    xTaskCreate(rssi_monitor_task, "rssi_monitor", 4096, NULL, 5, NULL);
    xTaskCreate(console_task, "console_task", 4096, NULL, 5, NULL);
}
