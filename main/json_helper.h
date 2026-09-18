#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Petit constructeur de JSON "à plat" (un seul niveau de clés,
 *        pas d'imbrication), pensé pour les payloads metrics/notifications.
 *
 * Usage typique dans une future tâche capteur :
 *
 *   json_builder_t *b = json_builder_new();
 *   json_builder_add_number(b, "temperature", 21.5);
 *   json_builder_add_number(b, "humidity", 55);
 *   char *payload = json_builder_finish(b);
 *   mqtt_app_publish(MQTT_TOPIC_METRICS, "salon", payload, false);
 *   free(payload);   // json_builder_finish alloue avec malloc, à libérer
 */
typedef struct json_builder_s json_builder_t;

json_builder_t *json_builder_new(void);
void json_builder_add_number(json_builder_t *b, const char *key, double value);
void json_builder_add_string(json_builder_t *b, const char *key, const char *value);
void json_builder_add_bool(json_builder_t *b, const char *key, bool value);

/**
 * @brief Sérialise le JSON accumulé et libère le builder.
 * @return chaîne allouée avec malloc (à libérer avec free() par l'appelant),
 *         ou NULL en cas d'erreur de sérialisation.
 */
char *json_builder_finish(json_builder_t *b);

#ifdef __cplusplus
}
#endif
