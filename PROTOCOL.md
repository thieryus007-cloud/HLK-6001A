# Référence technique — protocole radar et composant ESPHome

Ce qui suit décrit le module réellement livré et testé (radar HLK-LD6001A,
firmware radar `NOP_2.11-20260525-minesemi`). Les écarts avec les deux
manuels (Hi-Link V1.1, datasheet MinewSemi MS72SF1) sont signalés là où
ils existent ; le détail des tests qui les établissent est dans PLAN.md
(« Journal Phase 1 »).

## Câblage

Identique au HLK-LD6001B (mêmes broches, même alimentation).

| Radar (LD6001A) | XIAO ESP32-S3 Plus (silkscreen / GPIO) | Fonction |
|---|---|---|
| TX  | D7 / GPIO44 | RX de l'ESP32 (croisé) |
| RX  | D6 / GPIO43 | TX de l'ESP32 (croisé) |
| 3V3 | 3.3V-OUT    | Alimentation radar |
| GND | GND         | Masse commune |

GPIO43/44 sont l'UART0 par défaut du bootloader ROM : le logger reste sur
l'USB natif (`logger: hardware_uart: USB_SERIAL_JTAG`), et le bus radar est
déclaré en **deuxième** bloc `uart:` (un bus factice GPIO17/18 consomme
`UART_NUM_0`) pour que le radar soit sur `UART_NUM_1` — sur `UART_NUM_0`,
aucune commande `AT+` n'obtient de réponse (constaté sur le 6001B).

**Règle absolue** : aucun second maître UART ne doit être câblé en
parallèle sur TX/RX du radar pendant que l'ESP32 y est connecté.

UART : **115200 bauds** (confirmé sur le module ; la mention « 921600 par
défaut » de la table `AT+BAUD` du manuel Hi-Link ne s'applique pas), 8N1,
sans contrôle de flux.

Alimentation : pic ~530 mA, veille ~80 mA, moyenne ~110 mA à 100 ms
(manuel Hi-Link §4.4) ; alimentation ≥ 1 A requise.

## Modes de sortie (`AT+DEBUG=X`)

| Mode | Sortie | Utilisation |
|---|---|---|
| 0 (défaut du module) | trames `55 AA`, TYPE 0x04 = nombre de personnes seul | reconnues et validées (checksum), non décodées |
| 2 (« logiciel PC ») | trames TLV **avec nuage de points, sans octet de checksum** | **mode d'exploitation du composant** |
| 3 (« protocole détaillé ») | trames TLV avec octet de checksum, `POINTLEN` toujours 0 selon le manuel | accepté par le parseur, non utilisé |

Le mode d'exploitation est une constante (`RADAR_OPERATING_DEBUG_MODE` dans
`hlk_ld6001a.h`).

## Trame TLV (`AT+DEBUG=2` et `3`)

```
HEAD(8) | LENGTH(4) | FRAME(4) | TLV1(4) | POINTLEN(4) | <points> | TLV2(4) | TRACKLEN(4) | <personnes> | [CHECK(1)]
```

Tous les champs sont little-endian.

- `HEAD` : `01 02 03 04 05 06 07 08`.
- `LENGTH` : en `DEBUG=2`, taille exacte de la trame (l'en-tête suivant
  commence à l'offset `LENGTH`). En `DEBUG=3` (exemple chiffré du manuel
  §7.2.2), `LENGTH` n'inclut pas l'octet `CHECK` final.
- `FRAME` : compteur de trame (+1 par trame, ~10 trames/s avec `TIME=100`).
- `TLV1` = 1, puis `POINTLEN` octets de nuage de points (multiple de 25).
- `TLV2` = 2, puis `TRACKLEN` octets de personnes (32 octets chacune).
- `CHECK` (`DEBUG=3` seulement) : XOR de `FRAME` (4 octets) et de tous les
  octets des personnes.

**Validation structurelle** (les deux modes) : `TLV1 == 1`, `TLV2 == 2` et
`LENGTH == 24 + POINTLEN + 8 + TRACKLEN` exactement. En `DEBUG=2`, sans
checksum, c'est la seule vérification d'intégrité. Le cadrage (avec ou
sans `CHECK`) est détecté automatiquement (`decide_tlv_framing_()`), avec
les mêmes règles que le décodeur de référence Python
`testing/radar_protocol_tlv.py`, testé par `testing/test_protocol_tlv.py`
(octets réels `testing/fixtures/debug2_rx_2026-09-25.bin`, flux `DEBUG=3`
synthétique, corruption, texte intercalé, bascules de mode).

### Enregistrement personne (32 octets)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 4 | Q (réservé, uint32) |
| 4 | 4 | ID (identifiant persistant, uint32) |
| 8 | 4 | X (m, float32 — gauche/droite) |
| 12 | 4 | Y (m, float32 — avant/arrière) |
| 16 | 4 | Z (m, float32 — hauteur/profondeur) |
| 20 | 4 | Vx (m/s, float32) |
| 24 | 4 | Vy (m/s, float32) |
| 28 | 4 | Vz (m/s, float32) |

### Enregistrement point (25 octets) — format supposé

| Offset | Taille | Champ | Statut |
|---|---|---|---|
| 0 | 4 | X (m, float32) | valeurs plausibles, non validé contre une vérité terrain |
| 4 | 4 | Y (m, float32) | idem |
| 8 | 4 | Z (m, float32) | idem |
| 12 | 1 | tag (int8) | sens inconnu |
| 13 | 4 | D (float32) | pilote la couleur (légende constructeur : gris<2, cyan 2-3, bleu 3-4, vert 4-5, jaune 5-8, rouge>8) |
| 17 | 4 | E (float32) | non utilisé (valeurs discrètes 255 / 8 / 7 observées) |
| 21 | 4 | F (float32) | non utilisé |

Taille de 25 octets confirmée sur le module ; contenu repris du format
rétro-conçu sur le HLK-LD6001B, à valider par une campagne de vérité
terrain sur ce module. Seuls X/Y/Z/D sont exposés.

## Commandes `AT+` du firmware radar livré

Chaque commande ci-dessous a été testée sur le module et reliée à sa clé
`AT+READ` par un test changement → relecture → restauration. Réponse en
cas de succès : `AT+OK` ou `AT+OK=<valeur>`.

### Utilisées par le composant

| Commande | Clé `AT+READ` | Rôle | Plage retenue / défaut |
|---|---|---|---|
| `AT+STOP` / `AT+START` | — | arrêt / démarrage du radar | séquence de configuration |
| `AT+RESET` | — | réinitialisation | `on_boot`, watchdog, bouton « Redémarrer le radar » |
| `AT+DEBUG=2` | — | mode de sortie | séquence de configuration |
| `AT+DPKTHF=X` | `DPKF` | seuil de détection lointain (équivalent du `AT+DPKTH` du manuel : plus grand = moins sensible) | 1-9 / 4 |
| `AT+DPKTHN=X` | `DPKN` | seuil de détection proche (absent des manuels, sens de variation non documenté) | 1-9 supposée / 5 (valeur à la livraison) |
| `AT+RANGE=X` | `Range` | rayon du cercle de détection au sol (cm) | 10-500 / 450 |
| `AT+HEIGHT=X` | `Height` | hauteur d'installation (cm) | 250-320 (datasheet MinewSemi) / 270 |
| `AT+HRANGE=X` | `Hrange` | absent des manuels ; probablement la hauteur de balayage (non confirmé) | 50-500 supposée / 200 (valeur à la livraison) |
| `AT+HEATIME=X` | `Heart_Time` | intervalle heartbeat (s) | 10-999 / 60 |
| `AT+XNegaD=X` `AT+XPosiD=X` `AT+YNegaD=X` `AT+YPosiD=X` | `XdetectionN/P`, `YdetectionN/P` | bornes de la zone de détection (cm) | ±20..500 ; défaut du radar ±300 |
| `AT+READ` | — | lecture des paramètres | radar **arrêté** uniquement |
| toute autre commande | — | envoi libre avec accusé réel | panneau « Advanced Commands » |

`AT+HEIGHT` : 250 et 320 sont acquittés ; 321 aussi ; 249 a été appliqué
mais sans accusé. La plage documentée (250-320) est la seule retenue.

### Refusées par ce firmware (`AT+ERR`)

`AT+DPKTH`, `AT+HEIGHTD`, `AT+Moving`, `AT+Static`, `AT+Exit` (commandes du
manuel Hi-Link), `AT+XNega`/`AT+XPosi`/`AT+YNega`/`AT+YPosi` (forme sans
« D »). Les autres clés de `AT+READ` (`PointTH`, `PointTHV`, `FreeTime`,
`FreeTimeNoise`, `FreeNumNoise`, `TIME`, `PROG`) n'ont pas de commande
d'écriture connue.

### Documentées, non utilisées

`AT+BAUD` (risque de rompre la liaison), `AT+RESTORE` (réglages usine —
accessible via Advanced Commands).

## Réponse `AT+READ`

Uniquement radar arrêté (en streaming, la réponse est `AT+ERR`). Une ligne
pseudo-JSON, sans `AT+OK` :

```
{ "SoftVerison":"NOP_2.11-20260525-minesemi", "RangeRes":0.055664, "VelRes":0.111289,
  "TIME":100, "PROG":2, "BautRate":115200, "Heart_Time":60, "DPKN":5, "DPKF":4,
  "PointTHV":2, "PointTH":10, "FreeTime":20, "FreeTimeNoise":100, "FreeNumNoise":5,
  "Hrange":200, "Height":270, "Range":450, "XdetectionN":-300, "XdetectionP":300,
  "YdetectionN":-300, "YdetectionP":300 }
```

Le composant capture le bloc (`check_read_capture_()`), en extrait les clés
ci-dessus (`parse_read_response_()`) et les expose dans
`/radar_settings` → `live` : bandeau « Etat radar » et conformité par champ
de la page Configure.

## Trame heartbeat

`AT+HEATIME` est accepté, mais aucune trame `55 AA` n'a été observée en
`DEBUG=2`. Le composant journalise en hexadécimal la première trame `55 AA`
de chaque TYPE reçue (`first 55 AA frame of TYPE 0x..`), ce qui permettra de
relever le format si une trame heartbeat apparaît. « Etat radar » repose
sur `AT+READ`, pas sur un heartbeat.

## Séquence de configuration

0. `setup()` du composant (avant celui du WiFi) : `AT+STOP` puis 2 s
   d'attente — le radar ne consomme plus pendant le démarrage radio.
1. `on_boot` (YAML) : 8 s, activation du WiFi (`enable_on_boot: false`),
   attente d'une connexion confirmée (30 s max), 2 s, puis `AT+RESET`.
2. Composant (`loop()`) : 5 s après la connexion WiFi (secours : 32 s après
   le boot), `apply_radar_settings_()` met en file, chacune attendant son
   accusé réel (`AT+OK`/`AT+ERR`/`Save Para Fail`, sinon abandon après 5 s) :
   `AT+STOP`, `AT+DPKTHF`, `AT+DPKTHN`, `AT+RANGE`, `AT+HEIGHT`, `AT+HRANGE`,
   `AT+HEATIME`, les 4 bornes de zone (bornes configurées si la zone est
   activée, ±500 sinon), `AT+READ`.
3. File vidée (ou après 20 s) : `AT+DEBUG=2`, puis `AT+START` 500 ms plus tard.

La même séquence (précédée de `AT+RESET`) est rejouée par le watchdog (90 s
sans trame, 30 s minimum entre deux déclenchements) et par le bouton
« Redémarrer le radar ». Un enregistrement des réglages rejoue les étapes
2 et 3. Durée observée : ~1,4 s, 12 accusés `AT+OK` sur 12.

Étapes 0 et 1 et `wifi: output_power: 8.5db` : protections brownout,
voir MAINTENANCE.md.

## Publication vers Home Assistant

- `Presence` : immédiate, à chaque trame.
- `People Count` : une hausse n'est publiée qu'après 60 s sans changement
  (tout changement relance l'attente), une baisse aussitôt — même règle
  que le 6001B (`TARGET_COUNT_INCREASE_DEBOUNCE_MS`). Au démarrage, la
  valeur part de 0 : une personne déjà présente n'apparaît qu'après 60 s.
- Cibles (X/Y/Z, Vx/Vy/Vz, ID) : au plus une fois par seconde.
- La page web (`/hlk_targets.json`) montre toujours le compte brut.

## API HTTP du composant (serveur embarqué, port 80)

| Route | Méthode | Rôle |
|---|---|---|
| `/` | GET | page web (HLK / Plots / Configure) |
| `/vendor/three.min.js`, `/vendor/OrbitControls.js` | GET | bibliothèques 3D embarquées (gzip), aucune dépendance Internet |
| `/hlk_targets.json` | GET | cibles, nuage de points et indicateurs de santé (voir ci-dessous) |
| `/room_config` | GET/POST | géométrie de pièce et rotations d'affichage (affichage seulement) |
| `/radar_settings` | GET/POST | réglages radar commandés + `live` (dernière réponse `AT+READ`) |
| `/at_command` | POST (`cmd=...`) | Advanced Commands, non bloquant, `409` si une commande est déjà en cours |
| `/at_command_result` | GET | résultat de la dernière commande (`done`, `ok`, `timedOut`, `raw`) |
| `/radar_restart` | POST | cycle complet (`AT+RESET` + séquence de configuration) |

`/hlk_targets.json` : `count`, `targets[{id,x,y,z,vx,vy,vz}]`,
`points[{x,y,z,d}]` (150 max), `pointLen`, `frameCount`, `rejectedFrames`,
`framing` (`no_check`/`with_check`/`unknown`), `debugMode`, `uptimeS`,
`freeHeap`, `rssi`, `recoveryCount`, `ssid`. Aucune autre route n'est
sondée en boucle (risque d'épuisement du pool de sockets LWIP).

`/radar_settings` : `sensFar`, `sensNear`, `rangeCm`, `heightCm`,
`hrangeCm`, `heartbeatS`, `zone{enabled,xNeg,xPos,yNeg,yPos}`,
`zoneDisabledBoundCm`, `debugMode`, `live{seen, ageS, version, sensFar,
sensNear, rangeCm, heightCm, hrangeCm, heartbeatS, zone, raw}` (`null` =
clé absente de la réponse). POST : `sens_far`, `sens_near`, `range_cm`,
`height_cm`, `hrange_cm`, `heartbeat_s`, `zone_en`, `zone_x_neg`,
`zone_x_pos`, `zone_y_neg`, `zone_y_pos` (validés côté serveur ; un champ
invalide garde sa valeur précédente).

`/room_config` : `width`, `length` (≤ 5 m), `height` (≤ 3 m), `radarHeight`,
`wallOffsetM`, `mount` (0 plafond, 1 mur), `hlkViewRotation`,
`topViewRotation` (0-3 × 90°, indépendantes). Persisté en flash.
