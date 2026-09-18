#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Synchronise l'horloge système via NTP. Bloquant, avec timeout
 *        (CONFIG_NTP_SYNC_TIMEOUT_S) : si le serveur NTP est injoignable,
 *        log un avertissement et continue quand même (best-effort, ne
 *        bloque pas indéfiniment le démarrage).
 *
 * À appeler après que l'Ethernet a une IP (a besoin du réseau), avant de
 * démarrer les modules qui publient des données horodatées.
 */
void time_sync_start(void);

#ifdef __cplusplus
}
#endif
