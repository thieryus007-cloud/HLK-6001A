# HLK-LD6001A (MS72SF1) — déploiement plafond sur XIAO ESP32-S3 Plus

Firmware ESPHome pour le radar mmWave Hi-Link HLK-LD6001A (suivi
multi-cibles, montage plafond), piloté par un Seeed XIAO ESP32-S3 Plus.
Architecture, interface web et API HTTP reprises de l'état final du projet
HLK-LD6001B (même carte) ; protocole et jeu de commandes propres à ce
module (firmware radar `NOP_2.11-20260525-minesemi`) — voir PROTOCOL.md.

**État** : firmware en service sur l'unité `28:84:85:8a:be:00`
(http://192.168.1.90/, nom réseau `hlk-ld6001a-xiao`). Liaison radar,
séquence de configuration (12 commandes acquittées, réglages relus
conformes par `AT+READ`), décodage des trames et page web vérifiés sur le
matériel ; interface et intégration Home Assistant validées par
l'utilisateur. Image de référence de la flash : `Clone_ESP32S3/`. Restent
à faire : voir « Suite » dans PORTAGE-6001B-VERS-6001A.md (installation au
plafond, validation du nuage de points contre une vérité terrain,
stabilité sur plusieurs heures).

## Interface web

Trois pages, sélecteur dans le bandeau rouge supérieur (même interface que
le HLK-LD6001B) :

- **HLK** (page par défaut) : nuage de points `AT+DEBUG=2` (plan XY avec
  zoom molette/boutons/pincement tactile, rotation par pas de 90°
  enregistrée, échelle), compteurs (trames, points, `POINTLEN`, personnes),
  légende couleur, cibles suivies avec leur ID, vue Élévation X/Z. Pause,
  rémanence 5/10 trames, export JSON de la trame affichée, inspection d'un
  point au clic/tap. Le format de point est celui du HLK-LD6001B (taille
  confirmée, contenu non encore validé sur ce module).
- **Plots** : vue 3D orbitale + vues Dessus/Face/Profil (zoom, recentrage,
  rotation de la vue de dessus), 6 emplacements de cibles fixes (ID,
  position, vitesse), état du système.
- **Configure** : à gauche Setup Details (protocole et cadrage détecté,
  firmware radar — réponse `AT+READ` complète au survol —, fonctionnement,
  mémoire, WiFi, récupérations watchdog, cadence) et Advanced Commands
  (console `AT+` avec accusé réel ; `READ` affiche le bloc de paramètres) ;
  à droite Real-Time Tuning (sensibilités lointaine/proche, rayon de
  détection, hauteur d'installation, hauteur de balayage, heartbeat —
  infobulles sourcées, commande envoyée et conformité relue sous chaque
  champ, bouton « Redémarrer le radar »), Scene Selection (géométrie de
  pièce, affichage seulement) et Zone de détection (4 bornes signées,
  guide pas à pas ; désactivée = bornes au maximum).

Bandeau rouge : état de connexion, nombre de personnes, « Etat radar »
(dernière réponse `AT+READ`, en ambre si un réglage relu diffère du
réglage commandé). Au-delà de 3 sondages échoués, un bandeau « Données
figées » apparaît.

## Documents de ce dossier

- **[PROTOCOL.md](PROTOCOL.md)** — référence technique : câblage, modes de
  sortie, trame TLV, commandes `AT+` réellement acceptées, réponse
  `AT+READ`, séquence de configuration, API HTTP.
- **[DEPLOYMENT.md](DEPLOYMENT.md)** — prérequis, build, flash,
  installation physique, vérification post-déploiement.
- **[MAINTENANCE.md](MAINTENANCE.md)** — règles opérationnelles,
  récupération automatique, limitations connues, dépannage.
- **[PLAN.md](PLAN.md)** — journal complet du projet (sources, décisions,
  tests matériels).
- **[PORTAGE-6001B-VERS-6001A.md](PORTAGE-6001B-VERS-6001A.md)** — portage
  depuis le HLK-LD6001B : ce qui a été repris, adapté, et ce qui reste.

## Sous-dossiers

- **`esphome/`** — firmware : `hlk-ld6001a-xiao.yaml`, composant
  `components/hlk_ld6001a/`, `secrets.yaml.example` (le `secrets.yaml`
  réel n'est jamais commité).
- **`testing/`** — décodeurs de référence Python validés avant le C++ :
  `radar_protocol_tlv.py` + `test_protocol_tlv.py` (les deux cadrages,
  octets réels dans `fixtures/`), `radar_protocol_debug3.py` +
  `test_protocol_debug3.py` (exemple chiffré du manuel).

## Référence matérielle (racine de ce dossier)

- `HLK-LD6001A-60G Human Tracking Sensor Module Manual V1.1.pdf` — manuel
  Hi-Link. Son tableau de commandes ne correspond qu'en partie au firmware
  livré (voir PROTOCOL.md).
- `MS72SF1_Datasheet_K.pdf` — datasheet MinewSemi (éditeur du firmware
  radar livré) : source de `AT+HEIGHT` et de la plage 250-320 cm.
- `Links.txt` — composant tiers `github.com/Devristo/esphome-hlk-ld6001`,
  consulté comme témoin (ses commandes de zone sans « D » sont refusées par
  ce firmware).
