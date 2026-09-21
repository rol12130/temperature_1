# Guide pas-à-pas — Flasher et mettre à jour temperature_1

Ce guide est écrit pour quelqu'un qui n'est pas encore à l'aise avec les
suites de commandes terminal. Pas besoin de tout comprendre en détail,
suis juste les étapes dans l'ordre.

Deux procédures différentes, à ne pas confondre :

- **Flasher** : tu branches la carte en USB à ton Mac, tu envoies le
  firmware directement dessus. Nécessaire la première fois, ou si tu
  changes le code et que tu veux le tester tout de suite.
- **OTA (mise à jour à distance)** : la carte est déjà en marche quelque
  part (pas forcément branchée à ton Mac), et tu lui envoies une commande
  pour qu'elle aille chercher elle-même le nouveau firmware sur le réseau.
  Utile une fois la carte posée à son emplacement définitif, quand la
  débrancher pour la reflasher en USB serait pénible.

---

## Procédure 1 — Flasher en USB

À faire la première fois, ou après une modification du code que tu veux
tester rapidement.

1. **Ouvre un terminal.**

2. **Va dans le dossier du projet** (si tu ne l'as pas encore, voir tout
   en bas de ce guide pour le cloner) :
   ```
   cd ~/workspace/temperature_1
   ```

3. **Active l'environnement ESP-IDF** (nécessaire à chaque nouveau
   terminal, ça ne persiste pas automatiquement) :
   ```
   source ~/esp/esp-idf-v5.5.1/export.sh
   ```
   (ou ton alias habituel, genre `get_idf`, si tu en as configuré un)

4. **(Optionnel) Si tu as changé du code** : `idf.py build` pour
   recompiler. Si tu veux juste reflasher sans rien avoir changé, tu peux
   sauter cette étape, `idf.py flash` s'en charge de toute façon.

5. **Branche la carte en USB-C**, puis flashe :
   ```
   idf.py -p /dev/cu.usbserial-110 flash monitor
   ```
   Le `flash` envoie le firmware sur la carte, et `monitor` ouvre tout de
   suite les logs série pour que tu voies le démarrage.

6. **Pour quitter le monitor** (sans débrancher la carte, elle continue de
   tourner) : `Ctrl+]`

Si le port n'est pas `/dev/cu.usbserial-110` chez toi (ça peut changer
selon le câble ou le port USB utilisé), débranche/rebranche la carte et
fais `ls /dev/cu.*` pour repérer le bon nom.

---

## Procédure 2 — Mise à jour OTA (à distance, sans USB)

La carte doit déjà être allumée et connectée au WiFi/MQTT pour recevoir la
commande (regarde le monitor série, ou vérifie que les metrics arrivent
bien, pour confirmer qu'elle est en ligne avant de commencer).

Cette procédure utilise **deux terminaux en même temps** — un pour
regarder ce qui se passe, un pour envoyer la commande.

### Terminal 1 — préparer et publier le nouveau firmware

1. ```
   cd ~/workspace/temperature_1
   source ~/esp/esp-idf-v5.5.1/export.sh
   ```

2. **Change le numéro de version**, pour pouvoir vérifier après coup que
   la mise à jour a bien eu lieu (remplace `1.0.2` par le numéro que tu
   veux) :
   ```
   ./bump_version.sh 1.0.2
   ```

3. **Recompile :**
   ```
   idf.py build
   ```

4. **Publie le nouveau `.bin` sur le VPS** (remplace `1.0.2` par le
   numéro que tu as choisi à l'étape 2) :
   ```
   ~/workspace/scripts-deploiement/publish.sh temperature_1 1.0.2 build/esp32-ds18b20-banes.bin
   ```
   Le script affiche une ligne `Fichier : /home/roland/ota-infra/firmware/temperature_1/temperature_1_v1.0.2.bin`
   — note ce chemin, il te servira à l'étape suivante.

5. **Ouvre le monitor série** (pour voir la mise à jour se dérouler en
   direct — la carte n'a pas besoin d'être branchée en USB pour l'OTA en
   elle-même, mais si elle l'est déjà, autant regarder) :
   ```
   idf.py -p /dev/cu.usbserial-110 monitor
   ```
   Laisse cette fenêtre ouverte, tu vas juste la regarder défiler.

### Terminal 2 — déclencher la mise à jour

6. **Ouvre un nouveau terminal** (`Cmd+T` pour un nouvel onglet, ou une
   nouvelle fenêtre).

7. **Construis l'URL** à partir du chemin noté à l'étape 4 : remplace
   `/home/roland/ota-infra/firmware/...` par
   `http://10.10.0.1:8080/firmware/...`. Par exemple :
   ```
   http://10.10.0.1:8080/firmware/temperature_1/temperature_1_v1.0.2.bin
   ```

8. **Envoie la commande** (tout sur une seule ligne, remplace l'URL à la
   fin par celle que tu viens de construire) :
   ```
   mosquitto_pub -h 10.10.0.1 -u mqtt_admin -P R0l4nd57I0T -t commands/banes/esp32-ds18b20-1/ota/update -m "http://10.10.0.1:8080/firmware/temperature_1/temperature_1_v1.0.2.bin"
   ```
   Cette commande ne doit rien afficher — un terminal silencieux qui rend
   la main tout de suite veut dire que ça a marché.

9. **Reviens sur le Terminal 1** et regarde le monitor. Tu dois voir dans
   l'ordre : `Démarrage OTA depuis ...`, un temps d'attente (1 à 3
   minutes selon le réseau — les metrics continuent de s'afficher
   normalement pendant ce temps, c'est normal), `OTA réussie,
   redémarrage...`, puis un redémarrage avec `App version: 1.0.2` (ou le
   numéro que tu as choisi) dans les nouvelles lignes de boot.

### Si quelque chose ne colle pas

- Rien ne se passe dans le monitor après la commande `mosquitto_pub` :
  vérifie que la carte est bien allumée et connectée (metrics qui
  arrivent), et que tu as bien copié l'URL exacte à l'étape 7 sans faute
  de frappe.
- `notifications/banes/esp32-ds18b20-1/ota_status` (avec un
  `mosquitto_sub` dans un 3e terminal) donne le statut si le monitor
  série n'est pas ouvert : `downloading` → `success` ou `failed`.
- Si le nouveau firmware plante après le redémarrage, la carte revient
  automatiquement toute seule sur l'ancienne version au redémarrage
  suivant (rollback automatique du bootloader) — rien à faire de ton
  côté dans ce cas.

---

## Ajouter une nouvelle sonde (2e, 3e...)

Le code est **identique** pour toutes les sondes — seule la config change
(identifiant, site, WiFi). Pas besoin de dupliquer le dépôt : un dossier
local séparé par sonde physique, tous clonés depuis ce même dépôt GitHub.

1. **Clone dans un nouveau dossier**, nommé par emplacement plutôt que par
   numéro (plus facile à s'y retrouver quand tu en auras plusieurs) :
   ```
   cd ~/workspace
   git clone https://github.com/rol12130/temperature_1.git temp-banes-salon
   cd temp-banes-salon
   ```

2. **Configure cette instance :**
   ```
   source ~/esp/esp-idf-v5.5.1/export.sh
   idf.py set-target esp32
   idf.py menuconfig
   ```
   Dans *Capteur DS18B20 - Configuration app*, change au minimum :
   - **WiFi** → SSID/mot de passe du réseau où cette sonde sera posée
     (celui de Banes, différent de celui de Luceau utilisé pour les
     premiers tests)
   - **MQTT → Identifiant du device** → un identifiant **jamais utilisé
     ailleurs** (`esp32-ds18b20-2` pour la 2e sonde du projet, peu importe
     le site — incrémente simplement à chaque nouvelle sonde). Deux
     sondes avec le même identifiant publieraient sur les mêmes topics et
     recevraient les mêmes commandes OTA en même temps, à éviter.
   - **MQTT → Identifiant du site** → `banes` (déjà la valeur par défaut)
     ou `luceau` selon où tu déploies

3. **Build et flashe normalement** (voir Procédure 1 plus haut) :
   ```
   idf.py build
   idf.py -p /dev/cu.usbserial-XXX flash monitor
   ```
   (le port sera probablement différent si les deux cartes sont branchées
   en même temps — vérifie avec `ls /dev/cu.*`)

Pour les mises à jour de code plus tard : fais le changement dans un seul
dossier, commit/push, puis `git pull` dans chacun des autres dossiers
avant de rebuilder/reflasher — pas besoin de retaper le code partout.


```
cd ~/workspace
git clone https://github.com/rol12130/temperature_1.git
git clone https://github.com/rol12130/scripts-deploiement.git
cd temperature_1
source ~/esp/esp-idf-v5.5.1/export.sh
idf.py set-target esp32
idf.py menuconfig
```
Dans `menuconfig`, renseigne le WiFi et les identifiants MQTT (menu
*Capteur DS18B20 - Configuration app*), sauvegarde (`S`) et quitte (`Q`).
Puis reprends à la Procédure 1, étape 5.
