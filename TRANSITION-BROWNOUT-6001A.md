# Transition — enquête sur les brownouts de la carte 6001A-01 (branchée sur le hub USB)

Document de reprise pour une nouvelle conversation, rédigé le 2026-09-28.
But : **trouver pourquoi la carte 6001A-01 fait des brownouts** (reset
de l'ESP32-S3 par son détecteur de sous-tension) et y remédier durablement.

À lire d'abord, en entier : `C:\ncs\CLAUDE.md`, puis `CLAUDE.md` de ce
dossier (règles du projet). Journal détaillé : `PLAN.md` (journaux du
2026-09-26 au 2026-09-28). Outils : `testing/outils/README.md`.

---

## 1. Consignes impératives de l'utilisateur

- **Carte 6001A-01 (`28:84:85:8a:be:00`) : ne rien faire dessus sans son
  accord explicite** — ni écriture, ni esptool, ni port série (la
  fermeture du port redémarre la carte), ni redémarrage, ni commande
  radar, ni réglage, ni surveillance continue. Lectures d'état ponctuelles
  seulement, en le disant (voir § 4 : un brownout a suivi de peu des
  lectures d'état).
- Ne jamais mélanger les cartes 6001A et 6001B (firmware, fichiers, images).
- Ne jamais supposer une topologie d'alimentation ou de câblage non dite :
  demander.
- Chercher la cause dans le code et la configuration d'abord ; les faits
  matériels ci-dessous ont été donnés ou mesurés, ils ne sont pas à
  remettre en question.
- Toujours énoncer une prédiction précise et falsifiable avant chaque test.

## 2. Matériel et alimentation (faits donnés par l'utilisateur)

| Carte | MAC XIAO | Nom / IP | Radar | Alimentation |
|---|---|---|---|---|
| **6001A-01** | `28:84:85:8a:be:00` | `hlk-ld6001a-xiao` / 192.168.1.90 | HLK-LD6001A | **hub USB auto-alimenté** — avec le 6001B ; le 2026-09-27 l'utilisateur a décrit « un hub USB et un prolongateur USB » pour ces deux cartes |
| 6001A-02 | `7c:4f:ad:1e:98:fc` | `hlk-ld6001a-xiao-02` / 192.168.1.201 | HLK-LD6001A | **port USB du PC, seule** |
| 6001B | `68:ee:8f:4d:19:88` | `hlk-ld6001b-xiao-plus` / 192.168.1.17 | HLK-LD6001B | hub USB auto-alimenté (même hub que la 01) |

- Les trois sont des XIAO ESP32-S3 Plus identiques (ESP32-S3 rév. 0.2,
  PSRAM 8 Mo `AP_3v3`, flash 16 Mo Puya `85`/`2018`).
- Chaque radar est alimenté par la broche **3V3 du XIAO** (câblage
  PROTOCOL.md : 3V3, GND, TX/RX sur D6/D7).
- Informations **non connues** à demander à l'utilisateur : modèle du hub
  et puissance de son bloc d'alimentation, longueur/section du
  prolongateur, autres appareils sur le hub, port du PC utilisé par le hub
  et par la 02 (USB 2 / USB 3).
- Historique propre à la carte 01 : avant de devenir la 6001A-01 (le
  2026-09-25), elle était la carte du radar HLK-LD6001B et avait fait des
  **boucles de brownout le 2026-09-15** (journal du projet HLK-LD6001B,
  Phases 1-8, et `HLK-LD6001B/ESP32S3_Plus/Transition-ESP32S3-Plus-HLK-LD6001B.md`).

## 3. Firmware en service sur la carte 01

Build `2026-09-27 14:21:30` (`config_hash 0xe10cef7a`), ESPHome 2026.8.0,
`esphome/hlk-ld6001a-xiao.yaml`. Contenu : anti-rebond de People Count,
historique de détection (`/history.json`), sans `uart debug`, et trois
protections brownout :

1. `AT+STOP` au début de `setup()` puis `delay(2000)` avant le démarrage du
   WiFi — **efficace sur ce radar** (le MS72SF1 du 6001A répond `AT+OK` à
   `AT+STOP` en fonctionnement ; ce n'est pas le cas du radar 6001B) ;
2. `wifi: enable_on_boot: false`, radio activée 8 s après le démarrage ;
3. `wifi: output_power: 8.5db` (le pilote rapporte 10 dBm).

Configuration relevée : `CONFIG_ESP_BROWNOUT_DET=y`,
**`CONFIG_ESP_BROWNOUT_DET_LVL=7`** (défaut ESP-IDF ; valeur en volts à
vérifier dans la documentation ESP-IDF de l'ESP32-S3), CPU 240 MHz,
`power_save_mode: none`, `CONFIG_ESP_PHY_CALIBRATION_MODE=0`. Réglages
radar : `DPKTHF 3`, `DPKTHN 5`, rayon 400 cm, hauteur 250 cm, balayage
200 cm, heartbeat 60 s, zone désactivée ; intervalle de balayage du radar
100 ms (non réglable). Le mécanisme de retour arrière OTA d'ESPHome est
actif (`boot_is_good_after: 1min`).

## 4. Chronologie des redémarrages de la carte 01

| Date et heure | Contexte | Raison | Suite |
|---|---|---|---|
| 2026-09-26 ~17:05 | 1er démarrage après OTA (build 17:01:55) ; alimentation de la carte à ce moment non documentée | brownout ~44 s après le démarrage | retour automatique à l'image du 25/09 |
| 2026-09-26 ~19:30 | 1er démarrage après OTA (build 19:24:54 : protections 1+2, pleine puissance WiFi) | brownout | retour arrière |
| 2026-09-26 ~19:35 | 1er démarrage après OTA (build 19:31:56 : + `output_power 8.5db`) | — | OK |
| 2026-09-26 ~19:46 | 1er démarrage après OTA (build 19:40:49 : sans `output_power`) | brownout ~10 s après le démarrage, avant la connexion API (démarrage du WiFi) | retour arrière |
| 2026-09-26 ~20:00 | coupure/rebranchement par l'utilisateur (repositionnement) | power-on | OK |
| 2026-09-26 ~20:02 | 1er démarrage après OTA (build 19:59:27 : puissance réduite seulement pendant la connexion) | brownout | retour arrière ; 2e essai 20:05 OK |
| 2026-09-26 ~20:10 | fonctionnement (build 19:59:27) | **interrupt watchdog** | reparti ; variante abandonnée |
| 2026-09-26 20:57 | OTA build 20:55:59 (= config 19:31, même hash) | — | OK |
| 2026-09-27 09:52 | rebranchement par l'utilisateur (hub) | power-on | OK |
| 2026-09-27 14:17 / 14:21 / 14:25 | écritures USB + lecture d'image (esptool) | USB peripheral | OK |
| **2026-09-28 ~03:01** | **fonctionnement normal, personne ne touchait à rien** | **brownout** | reparti seul |
| **2026-09-28 11:29:31** | fonctionnement ; 37 s après le rebranchement de la carte 02 **sur un port du PC** (pas sur le hub) | **brownout** | reparti seul |
| **2026-09-28 11:31:46** | fonctionnement ; ~10–20 s après des lectures d'état de la carte (HTTP 11:31:29 puis API) | **brownout** | reparti seul |

Heures obtenues par `maintenant − uptime` (HTTP `/hlk_targets.json`) et
capteur « Reset Reason » (API). Les trois derniers événements (en gras)
se produisent **en fonctionnement**, avec les trois protections en place.
Le lien de cause à effet avec la carte 02 (11:29) ou avec les lectures
(11:31) **n'est pas établi** : coïncidences à vérifier, pas conclusions.

Pour comparaison le même jour : **6001A-02 (port du PC)** — démarrages
power-on 11:28:53 et « Reboot request from api » 15:48:07 (action de
l'utilisateur ou de Home Assistant), aucun brownout observé, mais sur
quelques heures seulement. **6001B (même hub que la 01)** — brownouts au
démarrage les 26 et 27/09 (capturé en série : `[wifi:649]: Starting` puis
`E BOD` dans la même milliseconde) ; le 2026-09-28 à 15:49, il ne répondait
plus sur le réseau (présent en USB) — non investigué.

## 5. Faits établis (sources vérifiées)

- **Radar** (manuels HLK-LD6001A V1.1, HLK-LD6001B, datasheet MS72SF1) :
  alimentation 3,0–3,3 V (maximum absolu 3,6 V, pas de 5 V) ; **~530 mA
  pendant l'émission RF**, ~80 mA hors émission, ~110 mA en moyenne à
  100 ms ; le fabricant exige une alimentation **capable de ≥ 1 A**.
- **XIAO ESP32-S3 Plus** (schéma `HLK-LD6001B/ESP32S3_Plus/XIAO_ESP32S3_Plus_V1.1_SCH_260115.pdf`) :
  3,3 V produit par un convertisseur DC-DC **SGM6029** (U3, 470 nH),
  annotation « Imax=600mA » sur le schéma ; datasheet SG Micro : 0,6 à 1 A
  selon la tension d'entrée et la fréquence. Ce rail alimente l'ESP32-S3
  (WiFi : pointes de plusieurs centaines de mA) **et** le radar.
- Les « 700 mA » annoncés par Seeed pour le 3V3 des XIAO sont la capacité
  totale du régulateur, consommation de l'ESP32 comprise (forum Seeed,
  cas du XIAO ESP32C3).
- Premier démarrage d'une image neuve : ESPHome ne retrouve pas ses
  données `fast_connect` (clé = hash de configuration) et sonde tous les
  canaux → 3 brownouts sur 3 à pleine puissance le 26/09.
- Le radar du 6001A accepte `AT+STOP` en fonctionnement ; celui du 6001B
  le refuse (`AT+ERR`).

## 6. Hypothèses à départager (aucune n'est prouvée)

| # | Hypothèse | Test proposé | Résultat attendu si vraie / si fausse |
|---|---|---|---|
| H1 | Le rail 3,3 V du XIAO (SGM6029) ne tient pas les pointes cumulées ESP32 + radar | a) oscilloscope sur 3V3 (déclenchement sur front descendant sous ~3,0 V) en fonctionnement ; b) radar alimenté par un régulateur 3,3 V séparé ≥ 1 A depuis le 5 V, masses communes (accord utilisateur requis) | a) creux de tension synchrones des émissions radar / WiFi ; b) plus aucun brownout sur 24–48 h et sur des redémarrages de test — sinon H1 écartée |
| H2 | Le 5 V qui arrive à la carte 01 s'affaisse (hub + prolongateur, charge du 6001B) | mesure de VBUS au XIAO pendant le fonctionnement ; **permutation d'emplacements** (01 sur un port du PC, ou 02 sur le hub) avec accord | les brownouts suivent l'emplacement « hub » → H2 ; ils restent sur la carte 01 → H2 écartée, voir H3 |
| H3 | Marge matérielle propre à la carte 01 (déjà sujette aux brownouts le 2026-09-15) | même permutation que H2 | les brownouts suivent la carte 01 quel que soit l'emplacement → H3 |
| H4 | Pointes liées au firmware ou à l'activité réseau (trafic, reconnexions WiFi, commandes radar) | corréler chaque brownout avec les journaux Home Assistant et du point d'accès UniFi (reconnexion, itinérance, 03:01) ; puis, une variable à la fois : CPU 160 MHz, mode d'économie WiFi, etc. | brownouts groupés autour d'événements réseau → H4 plausible ; aucune corrélation → H4 affaiblie |
| H5 | Interaction avec le 6001B sur le même hub | relevé simultané des redémarrages des deux cartes du hub ; alimenter le 6001B à part (accord requis) | brownouts simultanés ou disparition quand le 6001B est séparé → H5 |

Réglage à envisager seulement en dernier recours, avec accord et mesures :
abaisser le seuil du détecteur (`CONFIG_ESP_BROWNOUT_DET_LVL`) — masque le
symptôme sans corriger l'alimentation, et la PSRAM/flash peuvent mal se
comporter sous tension basse.

## 7. Première étape proposée (sans toucher à la carte 01)

1. Demander à l'utilisateur les informations manquantes du § 2 (hub, bloc
   d'alimentation, prolongateur, ports).
2. Récupérer l'historique des entités **« Reset Reason »** et **« Uptime »**
   des trois cartes dans Home Assistant (déjà enregistré, aucune action sur
   les cartes) : liste complète des brownouts et de leurs heures.
3. Avec l'accord de l'utilisateur, lancer
   `testing/outils/surveillance_resets.py` (lecture HTTP 1/min, API
   seulement après un redémarrage) sur la 02 et le 6001B, et sur la 01
   uniquement si l'utilisateur l'accepte, pour 24–48 h.
4. Présenter la liste des brownouts avec les hypothèses H1–H5 et proposer
   le premier test physique (permutation d'emplacements ou alimentation
   séparée du radar), à réaliser par l'utilisateur.

## 8. Ce qui a déjà été essayé

- Protections 1+2 seules : brownouts au premier démarrage d'image neuve.
- + `output_power 8.5db` : plus de brownout au premier démarrage d'image
  neuve (1/1), mais brownouts **en fonctionnement** les 28/09 à 03:01,
  11:29:31 et 11:31:46.
- Puissance réduite seulement pendant la connexion (`on_connect` → 20 dBm) :
  passage à 20 dBm sans effet (pilote resté à 10 dBm), un brownout sur deux
  premiers démarrages, un reset « interrupt watchdog » — abandonnée.
- Retrait de `uart debug` : a réglé la **lenteur réseau** (sans effet
  démontré sur les brownouts).

## 9. Erreurs à ne pas reproduire

- Conclure sur la version en service sans la relire après un redémarrage
  imprévu (le 26/09, des heures de tests ont porté sur l'image précédente,
  restaurée par le retour arrière OTA).
- Supposer que plusieurs cartes partagent la même alimentation (le 28/09,
  explication fausse fondée sur « même hub » alors que la 02 est sur un port
  du PC).
- Ouvrir puis fermer le port série d'une carte en croyant l'écouter
  « passivement » : la fermeture la redémarre.
- Enchaîner des redémarrages de l'ESP32 sans couper le radar : son module
  peut se bloquer (constaté sur le 6001B et sur la 02 ; déblocage par
  coupure d'alimentation > 1 min).
