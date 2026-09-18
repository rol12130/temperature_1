#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Les 4 branches racines du plan de topics (identique à
 *        esp32p4-master-banes, pour homogénéité entre devices du projet) :
 *   metrics/<site>/<device>/...        télémétrie (température)
 *   logs/<site>/<device>/...           logs de debug (dont progression OTA)
 *   notifications/<site>/<device>/...  événements significatifs (statut, OTA success/failure)
 *   commands/<site>/<device>/...       ce que le device reçoit
 */
typedef enum {
    MQTT_TOPIC_METRICS,
    MQTT_TOPIC_LOGS,
    MQTT_TOPIC_NOTIFICATIONS,
    MQTT_TOPIC_COMMANDS,
} mqtt_topic_category_t;

/**
 * @brief Les deux brokers auxquels ce firmware se connecte (voir
 *        mqtt_app_start) : LOCAL = broker du site (192.168.20.2, hébergé
 *        par esp32p4-broker-banes, sans TLS/auth), REMOTE = VPS (10.10.0.1,
 *        authentifié). Contrairement au master, ce device n'a AUCUNE
 *        commande de contrôle local : la seule commande gérée
 *        (ota/update) arrive volontairement par REMOTE uniquement (voir
 *        mqtt_app_register_remote_command dans main.c) — le déclenchement
 *        OTA est une opération de gestion de flotte depuis le VPS, pas un
 *        contrôle du site qui devrait rester utilisable sans internet.
 */
typedef enum {
    MQTT_LINK_LOCAL,
    MQTT_LINK_REMOTE,
} mqtt_link_t;

/**
 * @brief Callback invoqué quand un message arrive sur une commande
 *        enregistrée via mqtt_app_register_command() ou
 *        mqtt_app_register_remote_command().
 * @param payload    chaîne C (terminée par \0) du contenu du message. Valide
 *                   uniquement pendant l'appel : copier si besoin de la garder
 *                   au-delà (voir ota_app.c, qui fait un strdup avant de
 *                   passer la main à sa propre tâche).
 * @param reply_link broker d'où la commande est arrivée.
 */
typedef void (*mqtt_command_cb_t)(const char *payload, mqtt_link_t reply_link);

/**
 * @brief Démarre les deux clients MQTT (esp-mqtt) et s'authentifie sur le
 *        broker distant si CONFIG_MQTT_USERNAME/PASSWORD sont renseignés
 *        (le broker local n'a pas d'auth par défaut, voir
 *        CONFIG_MQTT_LOCAL_USERNAME/PASSWORD si besoin). À appeler
 *        seulement après que le WiFi a une IP, et après avoir enregistré
 *        toutes les commandes voulues via mqtt_app_register_command()/
 *        mqtt_app_register_remote_command() (les abonnements sont faits
 *        à la connexion).
 */
void mqtt_app_start(void);

/**
 * @brief Enregistre un callback pour une commande, sur
 *        commands/<site>/<device>/<topic_suffix> — reçue via le broker
 *        LOCAL du site. Doit être appelé avant mqtt_app_start().
 *
 * @param topic_suffix chemin sous commands/<site>/<device>/, sans slash de
 *                      tête
 * @param cb            callback invoqué à chaque message reçu sur ce topic
 * @return true si l'enregistrement a réussi (false si la table interne est
 *         pleine — voir MQTT_APP_MAX_COMMANDS dans mqtt_app.c)
 */
bool mqtt_app_register_command(const char *topic_suffix, mqtt_command_cb_t cb);

/**
 * @brief Comme mqtt_app_register_command(), mais l'abonnement se fait sur
 *        le broker DISTANT (VPS) plutôt que le broker local. Utilisé ici
 *        pour "ota/update" (voir main.c) : c'est la seule commande de ce
 *        device, et elle doit arriver du VPS.
 */
bool mqtt_app_register_remote_command(const char *topic_suffix, mqtt_command_cb_t cb);

/**
 * @brief Publie sur <category>/<site>/<device>/<suffix>. Pour
 *        MQTT_TOPIC_METRICS, duplique automatiquement sur les DEUX
 *        brokers (VPS + local) : le VPS pour l'historisation Influx
 *        (Telegraf n'écoute que là), le local pour qu'un consommateur du
 *        site fonctionne même sans internet. Les autres catégories
 *        (logs, notifications) restent VPS uniquement. Ne fait rien (log
 *        un warning) si le client concerné n'est pas connecté.
 *
 * @param category branche racine (voir mqtt_topic_category_t)
 * @param suffix    chemin sous <category>/<site>/<device>/, sans slash
 *                  de tête (ex: "ds18b20", "ota_status")
 * @param payload   contenu du message (chaîne C)
 * @param retain    true pour que le broker garde ce message comme dernière
 *                  valeur connue du topic
 */
void mqtt_app_publish(mqtt_topic_category_t category, const char *suffix,
                      const char *payload, bool retain);

/**
 * @brief Comme mqtt_app_publish(), mais sur le broker de son choix
 *        explicitement (pas de duplication automatique, même pour
 *        METRICS).
 */
void mqtt_app_publish_on(mqtt_link_t link, mqtt_topic_category_t category,
                          const char *suffix, const char *payload, bool retain);

#ifdef __cplusplus
}
#endif
