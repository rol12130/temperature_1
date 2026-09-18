#include "ota_app.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "json_helper.h"

static const char *TAG = "ota_app";

static volatile bool s_ota_in_progress = false;

static void publish_status(const char *status, const char *url, const char *error)
{
    json_builder_t *b = json_builder_new();
    json_builder_add_string(b, "status", status);
    if (url != NULL) {
        json_builder_add_string(b, "url", url);
    }
    if (error != NULL) {
        json_builder_add_string(b, "error", error);
    }
    char *payload = json_builder_finish(b);
    /* notifications/ : VPS uniquement (comportement par défaut de
     * mqtt_app_publish pour cette catégorie) — la gestion OTA est une
     * opération de flotte côté VPS, pas un besoin du site local. */
    mqtt_app_publish(MQTT_TOPIC_NOTIFICATIONS, "ota_status", payload, false);
    free(payload);
}

static void ota_task(void *arg)
{
    char *url = (char *)arg;

    ESP_LOGI(TAG, "Démarrage OTA depuis %s", url);
    mqtt_app_publish(MQTT_TOPIC_LOGS, "ota", "Téléchargement du firmware démarré", false);
    publish_status("downloading", url, NULL);

    esp_http_client_config_t http_config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach, /* voir CONFIG_MBEDTLS_CERTIFICATE_BUNDLE */
        .keep_alive_enable = true,
        .timeout_ms = 15000,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t err = esp_https_ota(&ota_config);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA réussie, redémarrage...");
        publish_status("success", url, NULL);
        mqtt_app_publish(MQTT_TOPIC_LOGS, "ota", "OTA reussie, redemarrage", false);
        /* Laisser le temps aux publications ci-dessus de partir sur le
         * réseau avant de couper : mqtt_app_publish() est asynchrone
         * (esp-mqtt les met en file, elles ne sont pas garanties parties
         * à l'instant où la fonction retourne). */
        vTaskDelay(pdMS_TO_TICKS(1000));
        free(url);
        esp_restart();
        /* esp_restart() ne retourne jamais */
    }

    ESP_LOGE(TAG, "OTA échouée : %s", esp_err_to_name(err));
    publish_status("failed", url, esp_err_to_name(err));
    mqtt_app_publish(MQTT_TOPIC_LOGS, "ota", "OTA echouee, firmware actuel conserve", false);

    free(url);
    s_ota_in_progress = false;
    vTaskDelete(NULL);
}

void ota_app_handle_command(const char *payload, mqtt_link_t reply_link)
{
    (void)reply_link; /* toujours REMOTE, voir mqtt_app_register_remote_command dans main.c */

    if (s_ota_in_progress) {
        ESP_LOGW(TAG, "Commande OTA ignorée : une mise à jour est déjà en cours");
        publish_status("busy", NULL, NULL);
        return;
    }

    if (payload == NULL || strlen(payload) == 0) {
        ESP_LOGE(TAG, "Commande OTA reçue sans URL, ignorée");
        publish_status("failed", NULL, "url manquante dans le payload");
        return;
    }

    /* payload n'est valide que pendant cet appel (voir mqtt_command_cb_t) :
     * on le copie pour la tâche OTA, qui le libère elle-même à la fin. */
    char *url = strdup(payload);
    if (url == NULL) {
        ESP_LOGE(TAG, "strdup() a échoué (mémoire insuffisante), OTA annulée");
        publish_status("failed", NULL, "memoire insuffisante");
        return;
    }

    s_ota_in_progress = true;
    /* Tâche dédiée plutôt qu'un appel direct dans le callback MQTT : le
     * téléchargement peut prendre plusieurs dizaines de secondes, pas
     * question de bloquer aussi longtemps le contexte interne d'esp-mqtt. */
    if (xTaskCreate(ota_task, "ota_task", 8192, url, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() a échoué, OTA annulée");
        publish_status("failed", url, "xTaskCreate a echoue");
        free(url);
        s_ota_in_progress = false;
    }
}
