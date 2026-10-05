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

Répète cette procédure **dans le dossier de chaque sonde** que tu veux
mettre à jour (`temp-banes-rdc`, `temp-banes-ch-nous`, ...) — un binaire
différent par sonde (WiFi + identifiant compilés en dur dedans), mais
**avec le même numéro de version** pour toutes, tant qu'elles tournent le
même code : ça permet de vérifier depuis MQTT que le déploiement a bien
touché toute la flotte (voir le champ `version` publié sur
`notifications/.../status`).

1. ```
   cd ~/workspace/temp-banes-rdc
   source ~/esp/esp-idf-v5.5.1/export.sh
   ```

2. **Change le numéro de version** (remplace `1.0.2` par le numéro que tu
   veux — **le même dans chaque dossier de sonde** pour ce déploiement) :
   ```
   ./bump_version.sh 1.0.2
   ```

3. **Recompile :**
   ```
   idf.py build
   ```

4. **Publie le nouveau `.bin` sur le VPS.** ⚠️ Le premier argument est
   l'**identifiant de cette sonde** (`CONFIG_MQTT_DEVICE`, celui que tu as
   mis dans `menuconfig` — ex. `esp32-ds18b20-1` pour `temp-banes-rdc`),
   **pas** un nom générique : utiliser le même nom pour deux sondes
   différentes écraserait le binaire de l'une avec celui de l'autre sur le
   VPS.
   ```
   ~/workspace/scripts-deploiement/publish.sh esp32-ds18b20-1 1.0.2 build/esp32-ds18b20-banes.bin
   ```
   Le script affiche une ligne `Fichier : /home/roland/ota-infra/firmware/esp32-ds18b20-1/esp32-ds18b20-1_v1.0.2.bin`
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
   http://10.10.0.1:8080/firmware/esp32-ds18b20-1/esp32-ds18b20-1_v1.0.2.bin
   ```

8. **Envoie la commande** (tout sur une seule ligne — vérifie que le
   `<device>` dans le topic correspond bien à la sonde que tu mets à jour,
   même chose que l'identifiant utilisé à l'étape 4) :
   ```
   mosquitto_pub -h 10.10.0.1 -u mqtt_admin -P R0l4nd57I0T -t commands/banes/esp32-ds18b20-1/ota/update -m "http://10.10.0.1:8080/firmware/esp32-ds18b20-1/esp32-ds18b20-1_v1.0.2.bin"
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
   idf.py set-target esp32      # carte HW-394 ; esp32c3 pour une ESP32-C3 SuperMini (voir plus bas)
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

---

## Utiliser une carte ESP32-C3 SuperMini (au lieu de la HW-394)

Le dépôt supporte les deux types de carte, avec le même code. Ce qui
change, c'est la **puce** (ESP32-C3 : RISC-V monocœur, au lieu de l'ESP32
classique) et l'**USB** (natif sur la C3, sans convertisseur USB-série).
Tout le reste (WiFi, MQTT, OTA, DS18B20) fonctionne pareil.

1. **Cible : `esp32c3` au lieu de `esp32`**, dans un dossier cloné
   (comme pour toute nouvelle sonde, voir plus haut) :
   ```
   idf.py set-target esp32c3
   idf.py menuconfig
   ```
   ⚠️ Si tu **remplaces une HW-394 par une C3 dans un dossier existant**
   (même sonde, même identifiant), `idf.py set-target esp32c3` régénère
   `sdkconfig` : le WiFi et le MQTT sont à ressaisir dans `menuconfig`
   (l'ancienne config reste lisible dans `sdkconfig.old` pour recopier les
   valeurs). Cloner dans un nouveau dossier évite ce piège.

2. **Le port USB n'a pas le même nom** : `/dev/cu.usbmodemXXXX` au lieu de
   `/dev/cu.usbserial-XXX` (repère-le avec `ls /dev/cu.*`).

3. **Premier flash : mode téléchargement manuel si ça ne se connecte pas.**
   Si `idf.py flash` n'arrive pas à se connecter à la carte :
   - maintiens le bouton **BOOT**
   - appuie brièvement sur **RESET** puis relâche-le
   - relâche **BOOT**
   - relance la commande de flash

   Ça ne devrait être nécessaire qu'au tout premier flash, et
   occasionnellement ensuite (notamment si un firmware plante).
   Après un flash, le port USB disparaît puis réapparaît une seconde
   pendant que la carte redémarre — normal. Si le monitor ne trouve pas
   le port juste après, relance simplement `idf.py -p ... monitor`.

4. **Choix de la broche 1-Wire** (`menuconfig` → *DS18B20 (1-Wire)* →
   *GPIO du bus*) : évite **GPIO2, 8 et 9** (broches de démarrage — GPIO8
   porte aussi la LED, GPIO9 le bouton BOOT), ainsi que GPIO18/19 (USB) et
   GPIO20/21 (UART). Les plus "propres" : **GPIO0, 1, 3, 10**. GPIO4 (la
   valeur par défaut, utilisée sur la HW-394) fonctionne aussi : ce n'est
   pas une broche de démarrage. Une des références consultées la signale
   par prudence comme broche JTAG, l'autre n'en parle pas. **Vérifie la sérigraphie de ta carte** : le brochage varie
   légèrement selon les fabricants, et c'est elle qui te dira quelle
   broche est voisine de la 3V3 (pour souder le pull-up 4.7 kΩ directement
   sur la carte, comme sur la HW-394).

5. ⚠️ **Alimentation : une seule source à la fois.** Soit l'USB-C, soit
   une alimentation externe de 5V sur la broche **5V** (+ GND) — pas les
   deux ensemble. Sur ces cartes la broche 5V est reliée au 5V du
   connecteur USB-C : elle sert d'entrée si tu alimentes de l'extérieur,
   de sortie si l'USB est branché. Les deux sources consultées donnent la
   même règle ; l'une précise qu'aucun circuit n'isole les deux sources et
   que ça peut abîmer la carte, l'alimentation ou le port USB de
   l'ordinateur, l'autre se contente de dire de ne pas le faire. Je n'ai
   pas vu le schéma de ta carte exacte, la règle reste donc prudente. La
   plage de tension acceptée sur la broche 5V varie selon les sources
   (3,3–6 V pour l'une, 4,3–6 V pour l'autre) : reste à 5 V.
   Pour une sonde en service, le plus simple est un chargeur USB classique
   sur le port USB-C : tu n'as alors pas besoin de toucher à la broche 5V.

### Migrer une sonde existante (HW-394 → C3) en gardant son identité

La HW-394 n'est plus utilisée pour les nouvelles sondes. Pour passer une
sonde déjà en service (ex. `temp-banes-rdc`) sur une C3, **en gardant le
même identifiant MQTT** (les données de la sonde restent sur les mêmes
topics) :

1. ```
   cd ~/workspace/temp-banes-rdc
   git pull                                   # récupère le support C3
   cp sdkconfig ~/sdkconfig-rdc-hw394.bak     # copie de sécurité, hors dépôt
   source ~/esp/esp-idf-v5.5.1/export.sh
   idf.py set-target esp32c3
   idf.py menuconfig
   ```
   (`set-target` renomme l'ancien `sdkconfig` en `sdkconfig.old` — mais si
   tu le relances une 2e fois, ce `.old` est écrasé : d'où la copie de
   sécurité ci-dessus. Ne le lance donc qu'**une seule fois**.)

2. Dans `menuconfig`, tout est remis aux valeurs par défaut — à ressaisir :
   WiFi (SSID + mot de passe), MQTT (utilisateur + mot de passe), la
   broche 1-Wire (voir plus haut), et surtout **l'identifiant du device** :
   remets celui de la sonde d'origine (`esp32-ds18b20-1` pour
   `temp-banes-rdc`, `esp32-ds18b20-2` pour `temp-banes-ch-nous`, etc.).
   ⚠️ La valeur par défaut est toujours `esp32-ds18b20-1` : si tu l'oublies
   pour une autre sonde, deux sondes se retrouvent avec le même
   identifiant.

3. Puis `idf.py build` et le flash (port `usbmodem`, mode téléchargement
   manuel si besoin — voir plus haut).

À savoir côté OTA : un binaire compilé pour une puce ne peut pas être
installé sur l'autre — ESP-IDF vérifie l'identifiant de puce de l'image et
refuse avec `Mismatch chip id`. Comme chaque sonde a de toute façon son
propre binaire (un nom de projet par device dans `publish.sh`), il n'y a
pas de risque de les mélanger.

---

## Premier clonage (si tu repars de zéro sur une nouvelle machine)

```
cd ~/workspace
git clone https://github.com/rol12130/temperature_1.git temp-banes-rdc
git clone https://github.com/rol12130/scripts-deploiement.git
cd temp-banes-rdc
source ~/esp/esp-idf-v5.5.1/export.sh
idf.py set-target esp32      # carte HW-394 ; esp32c3 pour une ESP32-C3 SuperMini
idf.py menuconfig
```
Dans `menuconfig`, renseigne le WiFi et les identifiants MQTT (menu
*Capteur DS18B20 - Configuration app*), sauvegarde (`S`) et quitte (`Q`).
Puis reprends à la Procédure 1, étape 5.
