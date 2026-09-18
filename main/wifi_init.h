#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bit levé dans le groupe d'événements réseau dès qu'une IP est obtenue */
#define WIFI_CONNECTED_BIT BIT0

/**
 * @brief Démarre l'interface WiFi en mode station (CONFIG_WIFI_SSID /
 *        CONFIG_WIFI_PASSWORD). Contrairement à l'Ethernet du master
 *        (lien géré au niveau matériel), le WiFi nécessite une reconnexion
 *        explicite en cas de coupure : gérée en interne (retries avec
 *        backoff, voir CONFIG_WIFI_MAXIMUM_RETRY / CONFIG_WIFI_RETRY_BACKOFF_MS),
 *        sans jamais redémarrer le device toute seule après le démarrage
 *        initial (seul l'appelant, au boot, décide d'un timeout — voir
 *        main.c).
 *
 * Non bloquant : la fonction retourne immédiatement, la connexion se fait
 * en tâche de fond.
 *
 * @return handle du groupe d'événements réseau, à utiliser avec
 *         xEventGroupWaitBits(handle, WIFI_CONNECTED_BIT, ...) pour
 *         attendre la première IP avant de démarrer MQTT.
 */
EventGroupHandle_t wifi_init_start(void);

/**
 * @return true si l'interface a une IP au moment de l'appel.
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
