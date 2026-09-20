#pragma once

#include "mqtt_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback pour commands/<site>/<device>/ota/update (voir main.c,
 *        enregistrée via mqtt_app_register_remote_command — reçue
 *        uniquement depuis le broker VPS).
 *
 * OTA volontairement simple, par choix (pas d'enjeu de contrôle critique
 * sur ce device) : PAS de partition factory séparée, PAS de mécanisme
 * rescue dédié comme sur esp32p4-master-banes. La seule protection
 * anti-brick est le rollback automatique du bootloader ESP-IDF
 * (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, voir sdkconfig.defaults et
 * main.c) : si le nouveau firmware plante avant confirmation, le
 * bootloader revient tout seul sur l'ancienne partition ota_x.
 *
 * @param payload    l'URL complète du firmware .bin à installer, en http://
 *                    ou https:// (esp_https_ota() gère les deux selon le
 *                    schéma — l'infra du projet sert actuellement en HTTP
 *                    simple, voir scripts-deploiement/generate_manifest.sh
 *                    — ex : "http://10.10.0.1:8080/firmware/temperature_1/temperature_1_v1.0.1.bin").
 *                    Payload MQTT brut, pas de JSON.
 *                    Une commande OTA déjà en cours est ignorée (log +
 *                    notification "busy") plutôt que d'en lancer une
 *                    deuxième en parallèle.
 * @param reply_link toujours REMOTE en pratique (seul lien sur lequel
 *                    cette commande est abonnée), paramètre gardé pour
 *                    respecter la signature mqtt_command_cb_t.
 */
void ota_app_handle_command(const char *payload, mqtt_link_t reply_link);

#ifdef __cplusplus
}
#endif
