#include "time_sync.h"

#include <time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "time_sync";

void time_sync_start(void)
{
    ESP_LOGI(TAG, "Synchronisation NTP (%s)...", CONFIG_NTP_SERVER);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_NTP_SERVER);
    esp_netif_sntp_init(&config);

    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(CONFIG_NTP_SYNC_TIMEOUT_S * 1000));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Synchronisation NTP échouée/timeout (%s) — les timestamps "
                 "seront faux jusqu'à une synchro ultérieure éventuelle.",
                 esp_err_to_name(err));
        return;
    }

    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
    ESP_LOGI(TAG, "Heure synchronisée : %s (epoch=%lld)", buf, (long long)now);
}
