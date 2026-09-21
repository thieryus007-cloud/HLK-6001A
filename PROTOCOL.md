# Référence technique — protocole radar et composant ESPHome

Voir PLAN.md pour la méthodologie de recoupement des sources (manuel
Hi-Link > datasheet MinewSemi MS72SF1 > code testé de Devristo) et le
journal des vérifications faites directement dans les PDF.

## Câblage

Identique au HLK-LD6001B (mêmes broches, même alimentation) — confirmé
directement dans le manuel Hi-Link V1.1 §4.1/§4.4, pas seulement supposé
par analogie.

| Radar (LD6001A) | XIAO ESP32-S3 (silkscreen / GPIO) | Fonction |
|---|---|---|
| TX  | D7 / GPIO44 | RX de l'ESP32 (croisé) |
| RX  | D6 / GPIO43 | TX de l'ESP32 (croisé) |
| 3V3 | 3.3V-OUT    | Alimentation radar |
| GND | GND         | Masse commune |

GPIO43/44 sont aussi l'UART0 par défaut du ROM bootloader de l'ESP32-S3 —
le logger applicatif reste forcé sur l'USB natif
(`logger: hardware_uart: USB_SERIAL_JTAG`).

**Règle absolue** : aucun second maître UART ne doit jamais être câblé en
parallèle sur les broches TX/RX du radar pendant que l'ESP32 y est
connecté — voir MAINTENANCE.md.

UART : **115200 bauds** (à vérifier au premier branchement — voir
"Débit UART" ci-dessous), 8 bits, 1 stop, pas de parité, pas de contrôle
de flux.

Alimentation : pic ~530mA (démarrage RF), veille ~80mA, moyenne ~110mA à
un cycle de 100ms — manuel Hi-Link §4.4, chiffres identiques au 6001B.
Alimentation ≥1A requise.

## Débit UART — à vérifier en premier à l'arrivée du matériel

Le manuel Hi-Link se contredit lui-même : le tableau `AT+BAUD` (réglages
courants, §6) dit "default value is 921600", mais l'outil hôte décrit en
§7.3 et l'usage général du document supposent 115200. **Essayer 115200
d'abord** (valeur par défaut de ce firmware ESPHome) ; si aucune trame ne
sort, réessayer à 921600 avant de suspecter le câblage ou le module —
voir DEPLOYMENT.md/MAINTENANCE.md pour la procédure.

## Architecture protocolaire : une seule source de position (différence majeure avec le 6001B)

Contrairement au HLK-LD6001B (où le protocole normal `AT+DEBUG=0` porte
déjà X/Y/Z et `AT+DEBUG=2` est additif), le HLK-LD6001A n'expose de
position/vitesse/ID **que** via `AT+DEBUG=3`. Le protocole normal
(`AT+DEBUG=0`, TYPE=0x04) ne donne qu'un compte de personnes, sans
coordonnées — ce composant démarre donc directement en `AT+DEBUG=3` et ne
décode pas le flux TYPE=0x04 au-delà de la reconnaissance de trame (voir
plus bas).

### Protocole normal (`AT+DEBUG=0`, défaut du module) — reconnu mais pas décodé

```
FH(2) | LENGTH(1) | TYPE(1) | DATA(LENGTH-4) | CHECK(1)
```
Confirmé identique octet pour octet au 6001B (même position de `CHECK`,
même algorithme XOR) via l'exemple chiffré du manuel Hi-Link §7.1 :
`55 AA 0A 04 00 00 00 00 00 0E` (TYPE=0x04, "0E" = XOR de `0A 04 00 00 00
00 00`, vérifié). Le composant reconnaît ce header et valide son checksum
pour "avaler" proprement la trame (et nourrir le watchdog) mais **ne
décode pas son contenu** : `AT+DEBUG=3` fournit déjà un compte de
personnes (`TRACKLEN/32`) qui rend ce compte redondant, sans qu'aucune
coordonnée ne soit disponible sur ce flux de toute façon.

### Protocole détaillé (`AT+DEBUG=3`) — source unique de position/vitesse/ID

```
HEAD(8) | LENGTH(4) | FRAME(4) | TLV1(4) | POINTLEN(4) | <point-cloud, POINTLEN octets> | TLV2(4) | TRACKLEN(4) | <personnes, TRACKLEN octets> | CHECK(1)
```

- `HEAD` : fixe, `01 02 03 04 05 06 07 08`.
- `LENGTH` : uint32 LE. **Ne compte PAS l'octet `CHECK` final** — la
  trame réelle sur le fil fait `LENGTH + 1` octets. Point non écrit
  explicitement dans le manuel, déduit et vérifié par calcul sur son
  propre exemple chiffré (65 octets réels pour `LENGTH=64`) — voir
  PLAN.md, Journal Phase 0.
- `FRAME` : compteur de trame, uint32 LE.
- `TLV1` : attendu `1`.
- `POINTLEN` : longueur en octets du nuage de points qui suit. Le manuel
  affirme "toujours 0" — **décodé dynamiquement dès le premier jour sans
  jamais supposer 0**, leçon directement héritée du 6001B (où cette même
  hypothèse s'est révélée fausse sur le terrain, voir PLAN.md).
- `TLV2` : attendu `2`.
- `TRACKLEN` : longueur en octets des enregistrements personne ;
  `nombre_personnes = TRACKLEN / 32`.
- Chaque enregistrement personne (32 octets, toutes valeurs little-endian) :

| Offset (relatif à l'enregistrement) | Taille | Champ |
|---|---|---|
| 0 | 4 | Q (réservé, uint32) |
| 4 | 4 | ID (identifiant persistant, uint32) |
| 8 | 4 | X (m, float32) |
| 12 | 4 | Y (m, float32) |
| 16 | 4 | Z (m, float32) |
| 20 | 4 | Vx (m/s, float32) |
| 24 | 4 | Vy (m/s, float32) |
| 28 | 4 | Vz (m/s, float32) |

- `CHECK` (1 octet, **hors** `LENGTH`) : XOR de `FRAME` (4 octets) +
  **tous** les octets des enregistrements personne (`TRACKLEN` octets) —
  **PAS** sur `LENGTH`/`TLV1`/`POINTLEN`/`TLV2`/`TRACKLEN`, et **PAS** sur
  les octets du nuage de points. Confirmé et vérifié par calcul à la main
  contre l'exemple chiffré du manuel (`0xCC`) — voir
  `testing/test_protocol_debug3.py`. **Validé par ce composant** (trame
  rejetée si le XOR ne correspond pas), contrairement au flux détaillé du
  6001B où ce même mécanisme n'était pas confirmé et donc pas vérifié.

Décodeur de référence : `testing/radar_protocol_debug3.py`, validé contre
l'exemple chiffré exact du manuel Hi-Link §7.2.2 (voir
`testing/test_protocol_debug3.py`). Portage C++ direct dans
`esphome/components/hlk_ld6001a/hlk_ld6001a.cpp::process_debug3_frame_()`.

## Trame heartbeat (TYPE 0x02) — existence probable mais format non confirmé

`AT+HEATIME` (intervalle heartbeat, 10-999s, défaut 60s) **existe** dans
le manuel Hi-Link §6 — contrairement à une lecture précédente moins
approfondie qui l'avait classée "non documentée". Mais **aucun des deux
manuels ne documente le format binaire de la trame heartbeat elle-même**
(pas de tableau de champs, pas d'exemple chiffré, contrairement au
6001B). Ce composant n'essaie donc pas de la décoder pour l'instant — voir
"Etat radar via AT+READ" ci-dessous pour le mécanisme de remplacement, et
PLAN.md pour le test précis à faire en Phase 1 pour trancher si une trame
`TYPE=0x02` apparaît réellement sur le fil.

## Etat radar via `AT+READ` (pas via heartbeat, pour l'instant)

`AT+READ` est une vraie commande (confirmée manuel Hi-Link §6 et
Devristo) qui renvoie un bloc quasi-JSON avec les réglages courants —
mais son format exact est **variable et mal formé selon la version de
firmware** (aucune source ne donne un exemple chiffré exploitable). Ce
composant l'envoie périodiquement (toutes les 15s, valeur arbitraire de
ce premier portage — voir `AT_READ_POLL_INTERVAL_MS` dans
`hlk_ld6001a.h`) via la file de commandes AT+, et affiche la réponse
texte **brute, non analysée**, dans le bandeau "Etat radar" de la page
web — pas de comparaison automatique "commandé vs. réel" comme sur le
6001B tant que le format exact n'a pas été capturé sur du vrai matériel
(Phase 1, voir PLAN.md).

## Table des commandes `AT+`

### Implémentées dans ce composant

| Commande | Rôle | Plage / défaut (manuel Hi-Link) |
|---|---|---|
| `AT+RESET` | Réinitialise le module | `on_boot`, watchdog |
| `AT+DEBUG=3` | Démarre directement en mode détaillé (jamais 0) | `on_boot` |
| `AT+START` / `AT+STOP` | Démarre/arrête le radar | `on_boot`, console web |
| `AT+DPKTH=X` | Sensibilité longue distance (plus grand = moins sensible) | 1-9, défaut 4 |
| `AT+RANGE=XXX` | Rayon du cercle de détection au sol (cm) | 10-500, défaut 450 |
| `AT+HEIGHTD=XXX` | Distance verticale d'installation (cm) | 50-500, défaut 300 |
| `AT+XNega=`/`AT+XPosi=`/`AT+YNega=`/`AT+YPosi=` | Zone de détection rectangulaire unique (cm, entiers signés, **sans le "D"** — voir note ci-dessous) | X-: -500..-20 déf. -450 · X+: 20..500 déf. 450 · Y-: -500..-20 déf. -450 · Y+: 20..500 déf. 450 |
| `AT+READ` | Sondage périodique (15s) pour le bandeau "Etat radar" | — |
| toute autre commande `AT+...` | Envoi libre, avec accusé de réception réel | Onglet "Console AT" |

**Note sur le "D" des commandes de zone** : le manuel Hi-Link documente
`AT+XNegaD=`/`AT+XPosiD=`/`AT+YNegaD=`/`AT+YPosiD=` (AVEC un "D"). Ce
composant envoie la forme **sans** "D" (`AT+XNega=`, etc.) parce que le
code testé de Devristo (empiriquement validé sur du vrai 6001A,
`assert(100<=x<=500)` etc.) utilise cette forme. **Cette décision n'a
pas été re-vérifiée directement dans ce PDF ni sur du matériel réel** —
voir PLAN.md, Journal Phase 0, point 4. Si `AT+ERR` est reçu en Phase 1,
retomber sur la forme avec "D".

### Documentées (manuel officiel) mais non exposées dans l'UI

| Commande | Rôle | Statut |
|---|---|---|
| `AT+BAUD=xx` | Débit série (défaut documenté 921600, contredit ailleurs) | Non exposé — risque de casser la communication |
| `AT+RESTORE` | Restauration réglages usine | Non exposé — accessible via Console AT si besoin |
| `AT+Moving=XXX` | Délai de disparition cible en mouvement (100ms, 5-1000, défaut 110) | Non exposé — pas de décision utilisateur prise sur ce réglage, voir PLAN.md |
| `AT+Static=XXX` | Délai de disparition cible statique (100ms, 5-1000, défaut 100) | Idem |
| `AT+Exit=XXX` | Délai de sortie de zone (100ms, 2-1000, défaut 5) | Idem |

## Séquence de démarrage (`on_boot`)

Identique dans l'esprit au 6001B (attente WiFi confirmée avant tout envoi
au radar, pour éviter le brownout documenté dans MAINTENANCE.md du
6001B) — adaptée pour aller directement en `AT+DEBUG=3` :

1. Attente d'une connexion WiFi confirmée (timeout 30s), puis 2s, puis
   `AT+RESET` (YAML, `on_boot`).
2. Côté composant C++ (`loop()`) : dès WiFi confirmé (ou après 32s de
   secours), attente de 5s puis envoi des réglages sauvegardés
   (`AT+DPKTH`, `AT+RANGE`, `AT+HEIGHTD`, et les 4 commandes de zone si
   activée) via la file de commandes, chacun confirmé par un vrai
   `AT+OK` ou abandonné après timeout.
3. Une fois la file vidée : `AT+DEBUG=3` (pas `0` comme le 6001B), puis
   `AT+START`.

## API HTTP du composant (serveur embarqué, port 80)

Identique au 6001B (mêmes routes, même conception non bloquante pour
`/at_command`) à une exception près : `/hlk_targets.json` ne porte
**qu'un seul** tableau `targets` (position + vitesse + ID directement
dans chaque entrée), il n'y a pas de second tableau `debug2Targets` à
faire correspondre puisqu'il n'y a qu'une seule source de cibles.

| Route | Méthode | Rôle |
|---|---|---|
| `/` | GET | Page web (onglets Pièce / Radar / Console AT) |
| `/hlk_targets.json` | GET | Cibles décodées en temps réel : `{count, targets:[{id,x,y,z,vx,vy,vz}]}` |
| `/room_config` | GET/POST | Géométrie de la pièce (affichage 3D uniquement) |
| `/radar_settings` | GET/POST | Réglages radar commandés + `live.raw`/`live.seen` (réponse brute du dernier `AT+READ` réussi) |
| `/at_command` | POST (`cmd=...`) | Console AT, non bloquant (voir `/at_command_result`) |
| `/at_command_result` | GET | Résultat de la dernière commande AT envoyée via `/at_command` |
