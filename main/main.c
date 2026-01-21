/**
 * @file wifi_tester.c
 * @brief ESP32 Wi-Fi performance measurement application.
 *
 * Measures RSSI and TCP throughput and publishes
 * the results via Serial in JSON format.
 */

#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

/** Wi-Fi SSID */
#define WIFI_SSID      "NUOSphere"

/** Wi-Fi password */
#define WIFI_PASS      "1stSafeAGI"

/** PC IP address (iPerf server) */
#define SERVER_IP      "192.168.10.22"

/** TCP server port (iPerf default: 5001) */
#define SERVER_PORT    5001

/** Logging tag for ESP_LOGx */
static const char *TAG = "wifi_tester";

/** Connection status flag */
static volatile bool is_connected = false;

/** Latest RSSI value (updated by rssi_monitor_task) */
static volatile int latest_rssi  = 0;

/** Latest TCP throughput in Mbps (updated by throughput_task) */
static volatile float latest_mbps  = 0.0f;

/**
 * @brief Wi-Fi and IP event handler.
 *
 * Handles Wi-Fi connection events including start, disconnect,
 * and successful IP acquisition.
 *
 * @param arg        User-defined argument (unused)
 * @param event_base Event base (WIFI_EVENT or IP_EVENT)
 * @param event_id   Event ID
 * @param event_data Event-specific data
 */
static void wifi_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_connected = false;
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {
        is_connected = true;
        ip_event_got_ip_t* event =
            (ip_event_got_ip_t*)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief Initialize ESP32 Wi-Fi in station mode.
 *
 * Configures Wi-Fi, registers event handlers,
 * and starts connection to the access point.
 */
void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, NULL);

    esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, NULL);

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

/**
 * @brief FreeRTOS task for monitoring Wi-Fi RSSI.
 *
 * Periodically reads RSSI from the connected access point
 * and updates the shared RSSI variable.
 *
 * @param pvParameters Task parameters (unused)
 */
void rssi_monitor_task(void *pvParameters)
{
    wifi_ap_record_t ap_info;

    while (1) {
        if (is_connected &&
            esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {

            latest_rssi = ap_info.rssi;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1 Hz
    }
}

/**
 * @brief FreeRTOS task for measuring TCP throughput.
 *
 * Continuously sends TCP data to a PC server (iPerf)
 * and calculates application-level throughput in Mbps.
 *
 * @param pvParameters Task parameters (unused)
 */
void throughput_task(void *pvParameters)
{
    struct sockaddr_in dest_addr;
    char buffer[1400];
    memset(buffer, 'A', sizeof(buffer));

    uint64_t bytes = 0;
    int64_t calc_start = 0;

    while (1) {

        while (!is_connected) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct timeval timeout = {
            .tv_sec = 1,
            .tv_usec = 0
        };
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout));

        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP,
                  &dest_addr.sin_addr);

        if (connect(sock,
            (struct sockaddr *)&dest_addr,
            sizeof(dest_addr)) != 0) {

            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        bytes = 0;
        calc_start = esp_timer_get_time();

        while (is_connected) {

            int sent = send(sock,buffer,sizeof(buffer),0);
            if (sent <= 0) break;

            bytes += sent;

            int64_t now = esp_timer_get_time();

            if (now - calc_start >= 1000000) {
                latest_mbps = (bytes * 8.0f) / 1e6;
                bytes = 0;
                calc_start = now;
            }
        }

        close(sock);
    }
}

/**
 * @brief Publish Wi-Fi measurement data via Serial.
 *
 * Sends timestamp, RSSI, and throughput in JSON format
 * over the serial interface at 1 Hz.
 *
 * @param pvParameters Task parameters (unused)
 */
void data_publish_task(void *pvParameters)
{
    while (1) {
        if (is_connected) {
            printf(
                "DATA:{\"ts\":%lld,"
                "\"rssi\":%d,"
                "\"throughput\":%.2f}\n",
                esp_timer_get_time(),
                latest_rssi,
                latest_mbps
            );
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 Hz
    }
}

/**
 * @brief Application entry point.
 *
 * Initializes NVS, Wi-Fi, and creates FreeRTOS tasks
 * for RSSI monitoring, throughput measurement,
 * and data publishing.
 */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "Starting ESP32 Wi-Fi Tester");

    wifi_init_sta();

    xTaskCreate(rssi_monitor_task,"rssi_monitor",4096, NULL, 5, NULL);

    xTaskCreate(throughput_task,"throughput",4096, NULL, 5, NULL);

    xTaskCreate(data_publish_task,"data_publisher",4096, NULL, 5, NULL);
}
