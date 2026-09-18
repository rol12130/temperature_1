#include "json_helper.h"

#include <stdlib.h>
#include <time.h>
#include "cJSON.h"

struct json_builder_s {
    cJSON *root;
};

json_builder_t *json_builder_new(void)
{
    json_builder_t *b = malloc(sizeof(json_builder_t));
    if (b == NULL) {
        return NULL;
    }
    b->root = cJSON_CreateObject();
    if (b->root != NULL) {
        /* Timestamp Unix (secondes) automatique sur chaque payload. Vaut 0
         * (ou une petite valeur proche de l'epoch) tant que le NTP n'a pas
         * encore synchronisé l'horloge système au démarrage — voir
         * time_sync.c, appelé avant les modules qui publient. */
        cJSON_AddNumberToObject(b->root, "ts", (double)time(NULL));
    }
    return b;
}

void json_builder_add_number(json_builder_t *b, const char *key, double value)
{
    if (b == NULL || b->root == NULL) return;
    cJSON_AddNumberToObject(b->root, key, value);
}

void json_builder_add_string(json_builder_t *b, const char *key, const char *value)
{
    if (b == NULL || b->root == NULL) return;
    cJSON_AddStringToObject(b->root, key, value);
}

void json_builder_add_bool(json_builder_t *b, const char *key, bool value)
{
    if (b == NULL || b->root == NULL) return;
    cJSON_AddBoolToObject(b->root, key, value);
}

char *json_builder_finish(json_builder_t *b)
{
    if (b == NULL) {
        return NULL;
    }
    char *out = cJSON_PrintUnformatted(b->root); /* alloué avec malloc en interne */
    cJSON_Delete(b->root);
    free(b);
    return out;
}
