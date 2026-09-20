# esp32-ds18b20-banes

Petit firmware ESP-IDF pour une carte **HW-394 (WR-32)** — ESP32 classique,
WiFi, USB-C, 4 Mo de flash — qui lit 1 ou 2 sondes de température **DS18B20**
(bus 1-Wire) et publie les mesures en MQTT, dans le même style que
[esp32p4-master-banes](https://github.com/rol12130/esp32p4-master-banes) :
plan de topics `metrics|logs|notifications|commands/<site>/<device>/...`,
payloads JSON à plat via le même petit `json_helper`, double publication des
metrics sur le broker local du site et le broker VPS.

Voir la [fiche chapeau](https://github.com/rol12130/fiches-projet/blob/main/fiche-chapeau.md)
du projet pour le contexte général. Site concerné : **Banes**.

## Différences volontaires avec esp32p4-master-banes

| | esp32p4-master-banes | esp32-ds18b20-banes |
|---|---|---|
| Cible | ESP32-P4 (Waveshare ESP32-P4-ETH) | ESP32 classique (HW-394 / WR-32) |
| Réseau | Ethernet (PHY IP101, RMII) | WiFi (station) |
| OTA | Mécanisme **rescue** dédié (partition factory + compteur de boot-loop NVS + dépôts `ota_rescue_*` séparés) | **Volontairement simple** : 2 partitions `ota_0`/`ota_1` standards ESP-IDF, `esp_https_ota()` classique déclenché par une commande MQTT, pas de partition factory ni de projet rescue. Anti-brick assuré uniquement par le rollback automatique du bootloader ESP-IDF (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) |
| Commande OTA | `factory_reset` sur le broker distant | `ota/update` sur le broker distant (même principe : reste joignable même si le broker local est down) |
| Contrôle local | Plusieurs commandes (chargers, relais, charge_safety...) sur le broker local | Aucune — ce device ne fait que publier des metrics |

Tout le reste (plan de topics, `json_helper`, double publication MQTT des
metrics, synchronisation NTP) reprend telle quelle la logique du master pour
rester homogène avec le reste du projet.

## Câblage

- **DS18B20 → GPIO4** (configurable, voir `menuconfig` → *Capteur DS18B20 -
  Configuration app* → *DS18B20 (1-Wire)*)
- Résistance de pull-up **4.7 kΩ entre GPIO4 et 3.3V** (recommandé même en
  usage normal ; le pull-up interne du GPIO, activable via
  `CONFIG_DS18B20_ENABLE_INTERNAL_PULLUP`, est un dépannage, pas une
  solution définitive)
- 1 ou 2 sondes peuvent partager le même GPIO/pull-up : chaque DS18B20 a une
  adresse ROM unique en usine, l'énumération au démarrage les distingue
  automatiquement (voir `ds18b20_task.c`)

## Configuration avant build

```
idf.py menuconfig
```

À renseigner au minimum (menu *Capteur DS18B20 - Configuration app*) :

- **WiFi** → SSID / mot de passe
- **MQTT** → nom d'utilisateur / mot de passe du broker VPS (le broker local
  n'a pas d'authentification par défaut, voir `esp32p4-broker-banes`)
- **MQTT → Identifiant du device** : à changer si tu déploies un 2e device
  DS18B20 sur Banes (chaque device doit être unique sur le plan de topics)

`sdkconfig.defaults` pré-remplit le reste (site `banes`, IP des deux
brokers, table de partitions, etc.) à l'identique de ce qui est déjà
utilisé sur `esp32p4-master-banes`.

⚠️ Le SSID/mot de passe WiFi et les identifiants MQTT sont laissés vides
dans `sdkconfig.defaults` (contrairement au `sdkconfig.defaults` actuel du
master, qui committe le mot de passe MQTT en clair) — à toi de voir si tu
veux les committer ici aussi ou les garder seulement en local via
`menuconfig` (non versionné par défaut avec ESP-IDF, sauf si tu forces
l'ajout de `sdkconfig`).

## Build / flash

```
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Au premier `idf.py build`, le component manager télécharge automatiquement
`espressif/onewire_bus` et `espressif/ds18b20` (voir `main/idf_component.yml`)
— nécessite un accès réseau à `components.espressif.com` à ce moment-là.

## Déclencher une mise à jour OTA

Le firmware accepte une URL `http://` ou `https://` (`esp_https_ota()` gère
les deux selon le schéma de l'URL — voir `ota_app.c`). L'infra existante du
projet (dépôt [scripts-deploiement](https://github.com/rol12130/scripts-deploiement))
sert les `.bin` en **HTTP simple** sur le VPS, port `8080` — ça fonctionne
tel quel, aucune adaptation nécessaire côté firmware.

⚠️ **Ne pas utiliser `deploy.sh`** de ce dépôt : il attend un zip et fait
`rm -rf` du dossier cible (pas adapté à un clone git existant), et son
déclenchement automatique publie en dur sur `commands/<site>/<device>/factory_reset`
— le mécanisme rescue du master, incompatible avec `ota/update` ici.
`publish.sh`/`generate_manifest.sh`, en revanche, sont génériques et
réutilisables tels quels.

```bash
# 1. Build (dans ce dépôt, après avoir bumpé CONFIG_APP_PROJECT_VER dans
#    sdkconfig.defaults si tu veux distinguer la version installée)
idf.py build

# 2. Publier le .bin + générer le manifest sur le VPS
~/workspace/scripts-deploiement/publish.sh temperature_1 1.0.1 build/esp32-ds18b20-banes.bin

# 3. Récupérer l'URL exacte générée
ssh vps "cat ~/ota-infra/firmware/temperature_1/manifest.json"

# 4. Déclencher manuellement (payload = URL du .bin lui-même, PAS celle du
#    manifest.json — voir ota_app.c, le payload attendu est l'URL directe)
mosquitto_pub -h 10.10.0.1 -u <user> -P <password> \
  -t commands/banes/esp32-ds18b20-1/ota/update \
  -m "http://10.10.0.1:8080/firmware/temperature_1/temperature_1_v1.0.1.bin"
```

⚠️ **À vérifier avant de tester** : la règle iptables `DOCKER-USER` du VPS
(voir [reseau-vps.md](https://github.com/rol12130/fiches-projet/blob/main/reseau-vps.md))
autorise Luceau (`192.168.1.0/24`) sur les ports `1883,3000,3001,1880,8086,9001,8088`
— **le port `8080` n'y figure pas**. Si le device est sur Luceau au moment
du test, le téléchargement risque d'échouer en timeout silencieux, comme
le MQTT avant le correctif de ce même fichier.

Suivre la progression sur :

- `notifications/banes/esp32-ds18b20-1/ota_status` (`downloading` →
  `success`/`failed`)
- `logs/banes/esp32-ds18b20-1/ota` (texte brut)

Si le nouveau firmware plante avant d'avoir confirmé son démarrage (voir
`main.c`), le bootloader ESP-IDF revient automatiquement sur l'ancienne
partition au redémarrage suivant — pas d'action manuelle nécessaire.

## Metrics publiées

`metrics/banes/esp32-ds18b20-1/ds18b20` (dupliqué sur le broker local et le
VPS), toutes les `CONFIG_DS18B20_POLL_INTERVAL_MS` (30 s par défaut) :

```json
{
  "ts": 1758000000,
  "sensor_count": 2,
  "temp_1": 21.31,
  "temp_1_addr": "28FF641F04170123",
  "temp_2": 18.87,
  "temp_2_addr": "28FF12AB04170456"
}
```

Une sonde dont la lecture échoue est simplement absente du payload ce
cycle-là (pas de valeur fausse publiée).

## Fichiers repris tels quels du master (aucune adaptation nécessaire)

- `json_helper.c/h` — constructeur JSON à plat, indépendant de la cible
- `time_sync.c/h` — synchronisation NTP, idem

## Fichiers adaptés

- `mqtt_app.c/h` — même architecture double-broker ; MAC lue via
  `ESP_MAC_WIFI_STA` au lieu de `ESP_MAC_ETH` ; pas de commandes de
  contrôle local (ce device n'en a pas besoin)
- `main.c` — WiFi au lieu d'Ethernet ; confirmation de rollback simplifiée
  (pas de compteur de boot-loop en NVS)

## Fichiers nouveaux

- `wifi_init.c/h` — station WiFi avec reconnexion automatique
- `ds18b20_task.c/h` — bus 1-Wire (composants managés `onewire_bus` +
  `ds18b20`), énumération des sondes, publication périodique
- `ota_app.c/h` — OTA simple déclenchée par MQTT

## Points ouverts

- [ ] Vérifier si le module ESP-WROOM-32 de la carte HW-394 utilisée a de
      la PSRAM (sdkconfig.defaults la laisse désactivée par défaut)
- [ ] Décider si les identifiants WiFi/MQTT doivent être committés en clair
      dans `sdkconfig.defaults` (comme sur le master) ou gardés locaux
- [ ] Pas de vérification de version avant OTA (contrairement au
      `manifest.json` comparé côté rescue sur le master) — une commande
      `ota/update` réinstalle toujours l'image pointée par l'URL, même si
      c'est la même version ou une version plus ancienne
