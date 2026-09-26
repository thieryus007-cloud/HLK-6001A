# Maintenance

Règles opérationnelles identiques au HLK-LD6001B quand la cause est
commune (même carte, même architecture de composant) — voir
`HLK-LD6001B/ESP32S3_Plus/MAINTENANCE.md` pour l'origine de chacune. Ce
document liste ce qui s'applique ici.

## Règle opérationnelle absolue

**Jamais deux maîtres UART actifs simultanément sur TX/RX du radar**
(adaptateur USB-TTL en parallèle de l'ESP32, etc.) : les deux côtés se
corrompent. Pour un test direct du radar, débrancher d'abord l'un des
deux. Pour dialoguer avec le radar depuis le PC sans débrancher, utiliser
le firmware pont `HLK-LD6001B/Pass-through-tests/plus-passthrough/`
(relais transparent, aucune commande propre) à la place du firmware de
production.

## Récupération automatique

Watchdog : 90 s sans trame valide (TLV ou `55 AA`) → séquence complète
`AT+RESET`, configuration, `AT+READ`, `AT+DEBUG=2`, `AT+START`, avec au
moins 30 s entre deux déclenchements. La file de commandes en attente est
abandonnée au déclenchement. Compteur exposé : `Radar Recovery Count`
(Home Assistant) et « Récupérations watchdog » (Setup Details). Une valeur
qui grimpe signale un problème persistant côté radar.

Laisser le radar arrêté (`AT+STOP` manuel) plus de 90 s déclenche aussi
le watchdog : c'est le comportement attendu, pas une panne.

## Etat radar et conformité des réglages

Le bandeau « Etat radar » et les lignes de conformité de la page Configure
reflètent la dernière réponse `AT+READ`, relue au démarrage, après chaque
enregistrement et à chaque « Redémarrer le radar » (pas de trame
heartbeat exploitable sur ce module). Ambre / « ≠ » : le radar rapporte
une valeur différente de celle commandée — comparer la valeur affichée
« radar : … » et la réponse complète (survol de « Firmware radar » dans
Setup Details). `AT+READ` ne répond qu'avec le radar arrêté : envoyé à la
main depuis Advanced Commands pendant le streaming, il renvoie `AT+ERR`
(normal). Envoyer `STOP`, puis `READ`, puis `START`.

## Jeu de commandes du firmware radar

Le firmware radar livré (`NOP_2.11-20260525-minesemi`) refuse plusieurs
commandes du manuel Hi-Link (`AT+DPKTH`, `AT+HEIGHTD`, `AT+Moving`,
`AT+Static`, `AT+Exit`, zone sans « D ») — liste complète et équivalents
dans PROTOCOL.md. Une commande qui répond `AT+ERR` dans Advanced Commands
n'existe généralement pas sous ce nom sur ce firmware : vérifier
PROTOCOL.md avant de suspecter la liaison. `READ` (radar arrêté) ou
`STOP` répondent toujours et permettent de vérifier que la liaison
fonctionne.

## Zone de détection

Le radar applique toujours des bornes de zone (±300 cm trouvés à la
livraison). Zone « désactivée » = bornes envoyées à ±500 cm : seul le
cercle `AT+RANGE` limite alors la détection. La ligne de statut de la zone
indique les bornes réellement commandées et relues.

## Persistance des réglages

L'ESP32 est la source de vérité : réglages en NVS, rejoués à chaque
(ré)initialisation du radar, que le radar les conserve lui-même ou non.

## Brownout au démarrage WiFi

Brownout observé sur cette carte (`Reset Reason` = « brownout »), surtout
au premier démarrage d'un firmware tout juste installé : ESPHome y sonde
tous les canaux WiFi, ses données `fast_connect` étant liées au hash de
configuration. Protections en place :

1. `AT+STOP` en début de `setup()` puis 2 s d'attente, avant le démarrage
   du WiFi ;
2. `wifi: enable_on_boot: false`, radio activée 8 s après le démarrage
   depuis `on_boot` ;
3. `wifi: output_power: 8.5db` (le pilote rapporte 10 dBm).

S'y ajoute l'attente d'une connexion WiFi confirmée avant `AT+RESET`. Sans
la protection 3, 3 brownouts sur 3 au premier démarrage d'une image neuve.
La variante « puissance réduite pendant la connexion seulement »
(`on_connect`/`on_disconnect`) a été essayée puis abandonnée (PLAN.md,
2026-09-26).

**Conséquence d'un brownout pendant la première minute après une OTA** :
l'image n'est pas encore validée par `safe_mode` (`boot_is_good_after:
1min`), le bootloader revient automatiquement au firmware précédent. Rien
n'est abîmé, mais la mise à jour n'a pas eu lieu : toujours vérifier la
date de compilation annoncée par la carte **après** 90 s d'uptime (voir
Dépannage).

## Dépannage

- **Page web inaccessible / `httpd_accept_conn: error in accept`** :
  épuisement du pool de sockets LWIP. Vérifier qu'aucune session de logs
  orpheline ne tourne. `CONFIG_LWIP_MAX_SOCKETS` est à 24 et toute la
  télémétrie passe par `/hlk_targets.json` (1 sondage/s par onglet).
- **Redémarrage inattendu avec `Reset Reason` = « USB peripheral »** :
  ouverture ou fermeture du port série USB par un outil du PC (capture de
  logs, esptool). Ce n'est pas un défaut du firmware. Préférer les logs
  via l'API WiFi.
- **Vérifier qu'une commande atteint le fil** : les logs du composant
  affichent `sending queued AT command` puis `AT response received`. Pour
  voir les octets, ajouter **temporairement** `debug: direction: BOTH`
  sous `uart: radar_uart` (absent en fonctionnement normal : il recopie
  chaque octet reçu en hexadécimal, ~25 Ko/s de logs avec un gros nuage
  de points, ce qui a fortement dégradé le réseau de la carte — ping
  jusqu'à 1,9 s, OTA de 150–200 s au lieu de 6–10 s).
- **Compilation « réussie » sans firmware produit** : commande lancée hors
  PowerShell (voir DEPLOYMENT.md).
- **OTA « successful » sans changement** : lire la date de compilation
  annoncée par la carte (API ESPHome `device_info`) **après 90 s
  d'uptime**, et `Reset Reason` : « brownout » ou « interrupt watchdog »
  pendant la première minute = retour automatique au firmware précédent ; après une modification de la page web, `esphome clean` puis
  recompilation, et contrôle du contenu servi (`curl http://<IP>/`).
- **Advanced Commands répond 409 « busy »** : une autre session a une
  commande en cours.
- **Trames rejetées** (Setup Details) : en `DEBUG=2`, seule la
  validation structurelle s'applique. Des rejets en `DEBUG=3` signalent
  des erreurs de checksum (liaison bruitée).
