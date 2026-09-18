#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "wifi_init.h"
#include "mqtt_app.h"
#include "time_sync.h"
#include "ds18b20_task.h"
#include "ota_app.h"

static const char *TAG = "main";

void app_main(void)
{
    /* NVS : requis par le stockage interne d'esp-mqtt, esp_wifi et esp_ota
     * (état des partitions OTA). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "Démarrage app v%s", desc->version);

    EventGroupHandle_t net_events = wifi_init_start();

    ESP_LOGI(TAG, "Attente d'une adresse IP (WiFi)...");
    EventBits_t bits = xEventGroupWaitBits(
        net_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(CONFIG_WIFI_CONNECT_TIMEOUT_S * 1000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        /* Pas de mécanisme de rescue ici (voir ota_app.h) : un simple
         * restart suffit, ça ne touche à aucune partition. Si le SSID/mot
         * de passe sont mauvais, ça bouclera indéfiniment — c'est
         * volontairement laissé simple, à surveiller via les logs série
         * au premier déploiement. */
        ESP_LOGE(TAG, "Pas d'IP obtenue après %ds, redémarrage...", CONFIG_WIFI_CONNECT_TIMEOUT_S);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
        return;
    }
    ESP_LOGI(TAG, "Réseau prêt.");

    time_sync_start();

    /* La commande OTA doit être enregistrée avant mqtt_app_start()
     * (l'abonnement effectif se fait à la connexion). Volontairement sur
     * le broker REMOTE (VPS) uniquement, jamais le local : voir
     * ota_app.h et mqtt_app.h pour le raisonnement. */
    mqtt_app_register_remote_command("ota/update", ota_app_handle_command);

    mqtt_app_start();

    /* Démarrage confirmé : WiFi + démarrage MQTT engagé sans crash. Annule
     * le rollback bootloader si cette image tournait en mode "pending
     * verify" (cas d'un boot juste après une OTA, voir ota_app.c) — c'est
     * TOUTE la logique anti-brick de ce projet (pas de compteur de
     * boot-loop en NVS ni de partition factory comme sur le master, voir
     * partitions.csv). On ne bloque pas sur la confirmation effective de
     * connexion MQTT : une coupure temporaire du VPS ne doit pas être
     * traitée comme un firmware défaillant. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Démarrage confirmé, annulation du rollback bootloader");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    ds18b20_task_start();

    ESP_LOGI(TAG, "Init terminée : WiFi + MQTT + DS18B20 actifs.");
}
