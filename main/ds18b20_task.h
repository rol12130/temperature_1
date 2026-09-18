#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise le bus 1-Wire sur CONFIG_DS18B20_GPIO, énumère les
 *        sondes DS18B20 présentes (jusqu'à CONFIG_DS18B20_MAX_SENSORS) et
 *        lance une tâche qui publie leur température toutes les
 *        CONFIG_DS18B20_POLL_INTERVAL_MS millisecondes, sous
 *        metrics/<site>/<device>/ds18b20 (voir mqtt_app.h — dupliqué
 *        automatiquement sur les deux brokers).
 *
 * À appeler après mqtt_app_start() (la tâche publie directement via
 * mqtt_app_publish, pas besoin d'attendre une connexion confirmée : les
 * publications sont simplement ignorées avec un warning tant que le
 * client MQTT n'est pas connecté).
 *
 * Si aucune sonde n'est trouvée au démarrage, log une erreur et la tâche
 * réessaie l'énumération périodiquement plutôt que d'abandonner (cas
 * probable : sonde pas encore câblée, ou pull-up manquant).
 */
void ds18b20_task_start(void);

#ifdef __cplusplus
}
#endif
