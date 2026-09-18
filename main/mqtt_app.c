#include "mqtt_app.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "json_helper.h"

static const char *TAG = "mqtt_app";

#define MQTT_APP_MAX_COMMANDS 8

static esp_mqtt_client_handle_t s_client_remote = NULL; /* VPS (10.10.0.1) */
static esp_mqtt_client_handle_t s_client_local = NULL;  /* site (192.168.20.2, voir esp32p4-broker-banes) */

/* Base "<site>/<device>" (sans catégorie), réutilisée pour construire
 * les 4 branches. Le device_id vient de CONFIG_MQTT_DEVICE ; le client_id
 * MQTT (distinct, pour la connexion TCP) reste dérivé de la MAC pour
 * garantir l'unicité même si deux devices partagent la même config. */
static char s_site_device[64];
static char s_client_id[32];

static char s_topic_status[96]; /* notifications/.../status (LWT), publié sur les 2 brokers */

/* Table des commandes enregistrées via mqtt_app_register_command()/
 * mqtt_app_register_remote_command() — une table par broker, puisque
 * chacun a ses propres abonnements et sa propre connexion. */
typedef struct {
    char topic[96];
    mqtt_command_cb_t cb;
} mqtt_command_entry_t;

static mqtt_command_entry_t s_commands_local[MQTT_APP_MAX_COMMANDS];
static size_t s_command_local_count = 0;
static mqtt_command_entry_t s_commands_remote[MQTT_APP_MAX_COMMANDS];
static size_t s_command_remote_count = 0;

static void build_base_topics(void)
{
    uint8_t mac[6] = {0};
    /* ESP_MAC_WIFI_STA (pas ESP_MAC_ETH comme sur le master P4-ETH) :
     * cette carte n'a que le WiFi. */
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(s_client_id, sizeof(s_client_id), "%s-%02x%02x%02x",
             CONFIG_MQTT_CLIENT_ID_PREFIX, mac[3], mac[4], mac[5]);

    snprintf(s_site_device, sizeof(s_site_device), "%s/%s",
             CONFIG_MQTT_SITE, CONFIG_MQTT_DEVICE);

    snprintf(s_topic_status, sizeof(s_topic_status), "notifications/%s/status", s_site_device);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    mqtt_link_t link = (mqtt_link_t)(intptr_t)handler_args;
    esp_mqtt_client_handle_t client = (link == MQTT_LINK_LOCAL) ? s_client_local : s_client_remote;
    mqtt_command_entry_t *commands = (link == MQTT_LINK_LOCAL) ? s_commands_local : s_commands_remote;
    size_t command_count = (link == MQTT_LINK_LOCAL) ? s_command_local_count : s_command_remote_count;
    const char *link_name = (link == MQTT_LINK_LOCAL) ? "local" : "distant (VPS)";

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connecté au broker MQTT %s (client_id=%s)", link_name, s_client_id);
        {
            /* notifications/... est en JSON à plat (convention du projet) —
             * publié indépendamment sur les 2 brokers dès que chacun se
             * connecte, pour que le statut reste visible même si un seul
             * des deux liens est up. */
            json_builder_t *b = json_builder_new();
            json_builder_add_string(b, "status", "online");
            char *payload = json_builder_finish(b);
            esp_mqtt_client_publish(client, s_topic_status, payload, 0, 1, true);
            free(payload);
        }
        for (size_t i = 0; i < command_count; i++) {
            esp_mqtt_client_subscribe(client, commands[i].topic, 1);
            ESP_LOGI(TAG, "Abonné (%s) à %s", link_name, commands[i].topic);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Déconnecté du broker MQTT %s", link_name);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Message reçu (%s) sur %.*s", link_name, event->topic_len, event->topic);
        for (size_t i = 0; i < command_count; i++) {
            if (event->topic_len == (int)strlen(commands[i].topic) &&
                strncmp(event->topic, commands[i].topic, event->topic_len) == 0) {

                /* Copie défensive : le payload du event n'est pas garanti \0-terminé */
                char payload[256];
                size_t len = event->data_len < sizeof(payload) - 1 ? event->data_len : sizeof(payload) - 1;
                memcpy(payload, event->data, len);
                payload[len] = '\0';

                commands[i].cb(payload, link);
                break;
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Erreur MQTT (%s)", link_name);
        break;

    default:
        break;
    }
}

static bool register_command_on(mqtt_command_entry_t *table, size_t *count,
                                 const char *topic_suffix, mqtt_command_cb_t cb)
{
    if (*count >= MQTT_APP_MAX_COMMANDS) {
        ESP_LOGE(TAG, "Table de commandes pleine (max %d), '%s' non enregistrée",
                 MQTT_APP_MAX_COMMANDS, topic_suffix);
        return false;
    }
    /* build_base_topics() n'a peut-être pas encore tourné si on enregistre
     * avant mqtt_app_start() (cas normal) : on le garantit ici aussi. */
    if (s_site_device[0] == '\0') {
        build_base_topics();
    }
    mqtt_command_entry_t *entry = &table[(*count)++];
    snprintf(entry->topic, sizeof(entry->topic), "commands/%s/%s", s_site_device, topic_suffix);
    entry->cb = cb;
    return true;
}

bool mqtt_app_register_command(const char *topic_suffix, mqtt_command_cb_t cb)
{
    return register_command_on(s_commands_local, &s_command_local_count, topic_suffix, cb);
}

bool mqtt_app_register_remote_command(const char *topic_suffix, mqtt_command_cb_t cb)
{
    return register_command_on(s_commands_remote, &s_command_remote_count, topic_suffix, cb);
}

void mqtt_app_start(void)
{
    if (s_site_device[0] == '\0') {
        build_base_topics();
    }

    /* Broker distant (VPS), authentifié — metrics dupliquées + seule
     * commande de ce device (ota/update, voir main.c). */
    esp_mqtt_client_config_t remote_cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URI,
        .credentials.client_id = s_client_id,
        /*
         * Last Will : si le device meurt sans se déconnecter proprement,
         * le broker publie ce JSON à sa place, sous
         * notifications/<site>/<device>/status.
         */
        .session.last_will.topic = s_topic_status,
        .session.last_will.msg = "{\"status\":\"offline\"}",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };
    if (strlen(CONFIG_MQTT_USERNAME) > 0) {
        remote_cfg.credentials.username = CONFIG_MQTT_USERNAME;
    }
    if (strlen(CONFIG_MQTT_PASSWORD) > 0) {
        remote_cfg.credentials.authentication.password = CONFIG_MQTT_PASSWORD;
    }

    s_client_remote = esp_mqtt_client_init(&remote_cfg);
    esp_mqtt_client_register_event(s_client_remote, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                    (void *)(intptr_t)MQTT_LINK_REMOTE);
    esp_mqtt_client_start(s_client_remote);

    /* Broker local du site (esp32p4-broker-banes, 192.168.20.2) — reçoit
     * uniquement les metrics dupliquées ici (ce device n'a pas de
     * commande de contrôle local). Pas de TLS/auth par défaut (réseau de
     * confiance) ; client_id distinct pour éviter toute ambiguïté dans
     * les logs du broker local. */
    char local_client_id[40];
    snprintf(local_client_id, sizeof(local_client_id), "%s-local", s_client_id);
    esp_mqtt_client_config_t local_cfg = {
        .broker.address.uri = CONFIG_MQTT_LOCAL_BROKER_URI,
        .credentials.client_id = local_client_id,
        .session.last_will.topic = s_topic_status,
        .session.last_will.msg = "{\"status\":\"offline\"}",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };
    if (strlen(CONFIG_MQTT_LOCAL_USERNAME) > 0) {
        local_cfg.credentials.username = CONFIG_MQTT_LOCAL_USERNAME;
    }
    if (strlen(CONFIG_MQTT_LOCAL_PASSWORD) > 0) {
        local_cfg.credentials.authentication.password = CONFIG_MQTT_LOCAL_PASSWORD;
    }

    s_client_local = esp_mqtt_client_init(&local_cfg);
    esp_mqtt_client_register_event(s_client_local, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                    (void *)(intptr_t)MQTT_LINK_LOCAL);
    esp_mqtt_client_start(s_client_local);
}

void mqtt_app_publish(mqtt_topic_category_t category, const char *suffix,
                      const char *payload, bool retain)
{
    /* Les metrics sont dupliquées sur les deux brokers (même choix que le
     * master) : le VPS pour l'historisation Influx (Telegraf n'y écoute
     * que là), le local pour un dashboard/consommateur du site qui doit
     * fonctionner même sans internet. Les autres catégories (logs,
     * notifications, dont ota_status) restent VPS uniquement : la gestion
     * OTA est une opération de flotte côté VPS, pas un besoin du site. */
    mqtt_app_publish_on(MQTT_LINK_REMOTE, category, suffix, payload, retain);
    if (category == MQTT_TOPIC_METRICS) {
        mqtt_app_publish_on(MQTT_LINK_LOCAL, category, suffix, payload, retain);
    }
}

void mqtt_app_publish_on(mqtt_link_t link, mqtt_topic_category_t category,
                          const char *suffix, const char *payload, bool retain)
{
    esp_mqtt_client_handle_t client = (link == MQTT_LINK_LOCAL) ? s_client_local : s_client_remote;
    if (client == NULL) {
        ESP_LOGW(TAG, "publish() appelé avant mqtt_app_start()");
        return;
    }

    static const char *category_names[] = {
        [MQTT_TOPIC_METRICS] = "metrics",
        [MQTT_TOPIC_LOGS] = "logs",
        [MQTT_TOPIC_NOTIFICATIONS] = "notifications",
        [MQTT_TOPIC_COMMANDS] = "commands",
    };

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/%s", category_names[category], s_site_device, suffix);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, retain);
}
