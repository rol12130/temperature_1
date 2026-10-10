# Sondes de température sous ESPHome (test)

**État (2026-10-07)** : configuration validée avec ESPHome 2026.9.1 (`esphome config`), mais **jamais flashée sur une carte**. Le but est de décider si ESPHome remplace le firmware C de ce dépôt pour les sondes (voir « Choses à discuter » dans la fiche chapeau de `fiches-projet`).

## Fichiers

- `sonde-base.yaml` : configuration commune à toutes les sondes
- `sonde-01.yaml` : une carte (nom, site, emplacement, adresse de la sonde, broker). Une nouvelle sonde = une copie de ce fichier avec 4 lignes changées
- `secrets.yaml.example` : à copier en `secrets.yaml` (ignoré par git)

Elle publie la convention décidée le 2026-10-07 : `metrics/<site>/sonde-NN/<emplacement>` avec `{"ts", "air_temp_c", "addr"}`, le statut sur `notifications/<site>/sonde-NN/status` et les logs sur `logs/<site>/sonde-NN/esphome`.

## Installer ESPHome

Dans un environnement isolé, version figée : `esphome==2026.9.1`. Éviter le Python 3.14 de l'environnement ESP-IDF, dont la compatibilité avec ESPHome n'est pas vérifiée (préférer 3.12 ou 3.13, avec `uv tool` ou `pipx`).

## Carte utilisée : C3 ou C6

La carte et la broche 1-Wire sont des variables de `sonde-01.yaml` (`board`, `onewire_pin`). Les deux variantes passent la validation d'ESPHome (2026-10) :

- **C3 SuperMini** (décision documentée) : `esp32-c3-devkitm-1`, `GPIO4`.
- **C6** : `esp32-c6-devkitc-1`, avec une autre broche. À ma connaissance, GPIO4 est une broche de strapping sur le C6 ; à confirmer avec la fiche de la carte. Les notes de la fiche Capteur de température (diode sur VBUS, brochage, GPIO4 voisine du 3V3) concernent la SuperMini C3 et ne s'appliquent pas telles quelles.

## Le test

1. Copier `secrets.yaml.example` en `secrets.yaml` et le remplir.
2. Premier flash en USB : `esphome run sonde-01.yaml`. Si la carte n'est pas reconnue, la brancher en maintenant le bouton BOOT.
3. Écouter les messages sur le broker du VPS (c'est le réglage par défaut du test, le broker de la maison n'est pas nécessaire) :
   `mosquitto_sub -h 10.10.0.1 -u mqtt_admin -P <mot de passe> -t 'metrics/banes/sonde-01/#' -v`
4. Vérifier dans InfluxDB ou Grafana : `host=sonde-01`, `device=rdc`, champ `air_temp_c`.
5. OTA à distance : connecter le Mac au partage de connexion du téléphone, activer WireGuard, puis `esphome run sonde-01.yaml --device <IP de la sonde>`.
6. Passer au broker du site : `mqtt_broker: 192.168.20.2` dans `sonde-01.yaml`, identifiants MQTT vides dans `secrets.yaml`, puis OTA.

## Critères de réussite

- Le JSON arrive tel que la convention le décrit, et la donnée apparaît dans InfluxDB.
- L'OTA fonctionne à travers WireGuard.
- Les logs sont lisibles à distance (topic `logs/...` ou `esphome logs`).
- La C3 tient le Wi-Fi 24 heures sans décrocher (point de vigilance noté dans la fiche Capteur de température).

## Correction du 2026-10-10

- `api: reboot_timeout: 0s` : par défaut, ESPHome redémarre la carte au bout de 15 minutes si aucun client ne se connecte à l'API native. Sans Home Assistant, la sonde aurait redémarré toutes les 15 minutes et le test de stabilité aurait été faussé. Revalidé avec 2026.9.1. La compilation complète reste à faire sur le Mac.

## Pas encore inclus

- Heartbeat et version annoncée (uptime, version) : à ajouter si le test est concluant.
- L'adresse `addr` n'est une étiquette qu'après l'ajout de `tag_keys = ["addr"]` dans Telegraf ; d'ici là le parseur JSON de Telegraf l'ignore et elle n'arrive pas dans InfluxDB, sans gravité pour le test (la température arrive).
