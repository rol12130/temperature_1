#include "wifi_init.h"

#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/task.h"

static const char *TAG = "wifi_init";

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_connected = false;
static int s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (s_retry_count < CONFIG_WIFI_MAXIMUM_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Déconnecté, tentative de reconnexion %d/%d...",
                     s_retry_count, CONFIG_WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            /* Pas de restart automatique ici (contrairement au timeout
             * initial du boot, voir main.c) : une coupure WiFi passagère
             * en cours de fonctionnement ne doit pas être traitée comme un
             * firmware défaillant. On attend juste un peu avant de
             * relancer une nouvelle série de tentatives. */
            ESP_LOGW(TAG, "Échec après %d tentatives, nouvel essai dans %d ms",
                     CONFIG_WIFI_MAXIMUM_RETRY, CONFIG_WIFI_RETRY_BACKOFF_MS);
            vTaskDelay(pdMS_TO_TICKS(CONFIG_WIFI_RETRY_BACKOFF_MS));
            s_retry_count = 0;
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP obtenue : " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

EventGroupHandle_t wifi_init_start(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connexion WiFi à \"%s\"...", CONFIG_WIFI_SSID);

    return s_wifi_event_group;
}

bool wifi_is_connected(void)
{
    return s_connected;
}
