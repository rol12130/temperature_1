#include "ds18b20_task.h"

#include <inttypes.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "onewire_bus.h"
#include "ds18b20.h"

#include "mqtt_app.h"
#include "json_helper.h"

static const char *TAG = "ds18b20_task";

static onewire_bus_handle_t s_bus = NULL;
static ds18b20_device_handle_t s_sensors[CONFIG_DS18B20_MAX_SENSORS];
static onewire_device_address_t s_addresses[CONFIG_DS18B20_MAX_SENSORS];
static int s_sensor_count = 0;

/* (Ré)énumère les sondes présentes sur le bus. Idempotent : recrée le bus
 * RMT seulement s'il n'existe pas encore. Retourne le nombre de sondes
 * DS18B20 trouvées (peut être 0, ce n'est pas une erreur bloquante — voir
 * ds18b20_task_run). */
static int enumerate_sensors(void)
{
    esp_err_t err;

    if (s_bus == NULL) {
        onewire_bus_config_t bus_config = {
            .bus_gpio_num = CONFIG_DS18B20_GPIO,
#if CONFIG_DS18B20_ENABLE_INTERNAL_PULLUP
            .flags.en_pull_up = true,
#endif
        };
        /* 1 octet commande ROM + 8 octets numéro ROM + 1 octet commande
         * device (voir doc du composant espressif/onewire_bus). */
        onewire_bus_rmt_config_t rmt_config = {
            .max_rx_bytes = 10,
        };
        err = onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Échec création du bus 1-Wire (GPIO%d) : %s",
                      CONFIG_DS18B20_GPIO, esp_err_to_name(err));
            s_bus = NULL;
            return 0;
        }
    }

    int count = 0;
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_device;

    ESP_ERROR_CHECK(onewire_new_device_iter(s_bus, &iter));
    esp_err_t search_result;
    do {
        search_result = onewire_device_iter_get_next(iter, &next_device);
        if (search_result != ESP_OK) {
            break;
        }
        if (count >= CONFIG_DS18B20_MAX_SENSORS) {
            ESP_LOGW(TAG, "Sonde supplémentaire ignorée (adresse %016" PRIX64
                          "), CONFIG_DS18B20_MAX_SENSORS=%d atteint",
                     next_device.address, CONFIG_DS18B20_MAX_SENSORS);
            continue;
        }
        ds18b20_config_t ds_cfg = {}; /* config par défaut (résolution 12 bits) */
        if (ds18b20_new_device_from_enumeration(&next_device, &ds_cfg, &s_sensors[count]) == ESP_OK) {
            ds18b20_get_device_address(s_sensors[count], &s_addresses[count]);
            ESP_LOGI(TAG, "Sonde DS18B20 #%d trouvée, adresse %016" PRIX64,
                     count, s_addresses[count]);
            count++;
        } else {
            ESP_LOGW(TAG, "Device 1-Wire non-DS18B20 ignoré (adresse %016" PRIX64 ")",
                     next_device.address);
        }
    } while (search_result != ESP_ERR_NOT_FOUND);

    ESP_ERROR_CHECK(onewire_del_device_iter(iter));

    if (count == 0) {
        ESP_LOGW(TAG, "Aucune sonde DS18B20 trouvée sur GPIO%d (câblage ? pull-up ?)",
                 CONFIG_DS18B20_GPIO);
    } else {
        ESP_LOGI(TAG, "%d sonde(s) DS18B20 prête(s)", count);
    }

    return count;
}

static void ds18b20_task_run(void *arg)
{
    (void)arg;

    s_sensor_count = enumerate_sensors();

    while (1) {
        if (s_sensor_count == 0) {
            /* Pas de sonde : on retente une énumération périodiquement
             * plutôt que d'abandonner (cas probable : sonde pas encore
             * câblée au boot). */
            vTaskDelay(pdMS_TO_TICKS(10000));
            s_sensor_count = enumerate_sensors();
            continue;
        }

        esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Échec du déclenchement de conversion : %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DS18B20_POLL_INTERVAL_MS));
            continue;
        }

        json_builder_t *b = json_builder_new();
        json_builder_add_number(b, "sensor_count", s_sensor_count);

        for (int i = 0; i < s_sensor_count; i++) {
            float temperature = 0.0f;
            err = ds18b20_get_temperature(s_sensors[i], &temperature);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Lecture sonde #%d échouée : %s", i, esp_err_to_name(err));
                continue; /* on n'ajoute pas la clé plutôt que publier une valeur fausse */
            }

            char key[24];
            snprintf(key, sizeof(key), "temp_%d", i + 1);
            json_builder_add_number(b, key, (double)temperature);

            char addr_key[32];
            char addr_val[20];
            snprintf(addr_key, sizeof(addr_key), "temp_%d_addr", i + 1);
            snprintf(addr_val, sizeof(addr_val), "%016" PRIX64, s_addresses[i]);
            json_builder_add_string(b, addr_key, addr_val);

            ESP_LOGI(TAG, "Sonde #%d : %.2f°C", i, temperature);
        }

        char *payload = json_builder_finish(b);
        mqtt_app_publish(MQTT_TOPIC_METRICS, "ds18b20", payload, false);
        free(payload);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_DS18B20_POLL_INTERVAL_MS));
    }
}

void ds18b20_task_start(void)
{
    xTaskCreate(ds18b20_task_run, "ds18b20_task", 4096, NULL, 5, NULL);
}
