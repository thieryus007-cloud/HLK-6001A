# Portage du travail 6001B (2026-09-16 → 2026-09-22) vers le 6001A

## État au 2026-09-25 — portage réalisé et en service

Matériel reçu (XIAO ESP32-S3 Plus `28:84:85:8a:be:00` + radar HLK-LD6001A).
Sur demande de l'utilisateur (« effacer l'ancien soft et mettre en place
le nouveau », « l'ancien site web n'est plus à jour »), l'état final du
6001B a été porté d'un seul tenant, adapté au module, puis flashé et
vérifié sur le matériel. Détail des tests : PLAN.md, « Journal Phase 1 ».

**Repris tel quel du 6001B** : serveur HTTP (TCP_NODELAY, 11 routes,
`max_uri_handlers` 12), Three.js/OrbitControls embarqués en gzip (octets
et décompression vérifiés), `RoomConfig` avec les deux rotations et les
limites 5 m / 3 m, indicateurs de santé dans `/hlk_targets.json`, route
`/radar_restart`, file de commandes AT+ à accusé réel, watchdog, page web
complète (HLK/Plots/Configure, tactile, zoom, rotation, pause/rémanence/
export/inspection, bandeau 3 colonnes, données figées).

**Adapté au 6001A** :
- mode d'exploitation `AT+DEBUG=2` (seul mode avec nuage de points ;
  trames sans checksum, vérifié sur 349 trames réelles), parseur
  acceptant les deux cadrages, avec les règles validées d'abord en Python
  (`testing/radar_protocol_tlv.py`) ;
- nuage de points : enregistrements de 25 octets (taille confirmée),
  format X/Y/Z/D du 6001B appliqué comme hypothèse ;
- un seul tableau de cibles (ID + position + vitesse), l'appariement
  entre les deux flux du 6001B est supprimé ;
- **réglages = commandes réellement acceptées par le firmware radar
  livré** (`NOP_2.11-20260525-minesemi`, MinewSemi) : `AT+DPKTHF`,
  `AT+DPKTHN`, `AT+RANGE` (rayon), `AT+HEIGHT`, `AT+HRANGE`,
  `AT+HEATIME`, zone `AT+XNegaD`/`XPosiD`/`YNegaD`/`YPosiD` (avec « D »).
  `AT+DPKTH`, `AT+HEIGHTD`, `AT+Moving`/`Static`/`Exit` du manuel Hi-Link
  répondent `AT+ERR` ;
- « Etat radar » et conformité par champ alimentés par `AT+READ` (réponse
  réelle capturée et décodée ; ne répond que radar arrêté, d'où `AT+STOP`
  en tête de la séquence de configuration) ;
- zone désactivée = bornes au maximum (±500) : le radar appliquait ±300
  par défaut ;
- `wifi_ssid()` déprécié (supprimé dans ESPHome 2026.9.0) remplacé par
  `wifi_ssid_to()`.

**Défauts latents du 6001B corrigés ici au passage** — reportés aussi sur
le 6001B le 2026-09-25 à la demande de l'utilisateur (Phase 15 de
`HLK-LD6001B/ESP32S3_Plus/PLAN.md`, dépôt `HLK-6001B` commit `a3fb1b9`),
vérifiés sur l'unité `68:ee:8f:4d:19:88` :
1. « Enregistrer les réglages » ne s'active pas quand on modifie seulement
   un champ de la Zone de détection ou la case « Activer », parce que
   `#radar-zones` n'est plus dans `#radar-form` depuis la réorganisation
   du 2026-09-22.
2. `/radar_restart` appelle `send_reinit_sequence_()` depuis la tâche
   httpd (écriture UART et file de commandes hors de la tâche
   principale). Ici, le passage se fait par un drapeau lu dans `loop()`.
3. Détection des réponses `AT+OK`/`AT+ERR` : effacer le tampon depuis son
   début jusqu'à la fin de la ligne détruit une trame complète non encore
   décodée qui précéderait la réponse. Ici, seule la ligne de réponse est
   effacée.
4. `wifi_ssid()` déprécié (le build 6001B cassera avec ESPHome 2026.9.0).

**Vérifié sur le matériel** : boot propre (aucun brownout, sans les
mitigations 6001B), 12 commandes de configuration acquittées `AT+OK` sur
12, six réglages et bornes de zone relus conformes par `AT+READ`, ~9,4
trames/s, 0 trame rejetée, page servie en 0,5 s, syntaxe JS des scripts
servis valide, compilation sans avertissement (RAM 31,1 %, Flash 14,5 %).

**Validé par l'utilisateur le 2026-09-25** : interface web et
fonctionnement (« le reste est validé »), Home Assistant OK. L'invite de
découverte « HLK-LD6001B plafond (XIAO ESP32-S3 Plus)
(hlk-ld6001b-xiao-plus) » qui apparaît dans Home Assistant correspond au
6001B de production (192.168.1.17, MAC `68:ee:8f:4d:19:88`), toujours
sous tension sur le réseau — le 6001A s'annonce bien sous
`hlk-ld6001a-xiao` (vérifié en mDNS et par son API). Installation au
plafond reportée par l'utilisateur.

### Suite (dans l'ordre)

1. ~~Validation visuelle par l'utilisateur~~ — faite (2026-09-25).
2. ~~Home Assistant~~ — fait (2026-09-25).
3. **Nuage de points — vérité terrain** : une personne immobile à des
   positions mesurées puis en marche (protocole GO/FAIT du 6001B,
   bouton Exporter de la page HLK ou pont `plus-passthrough` avec
   `point_cloud_capture.py`). Attendu si le format du 6001B s'applique :
   la majorité des points à moins de 30 cm de la cible suivie (critère du
   6001B). Sinon, rétro-ingénierie à reprendre sur le format du point.
4. **Installation au plafond** (reportée par l'utilisateur) et réglage de
   `AT+HEIGHT` sur la hauteur réelle. `AT+DPKTHN`/`AT+HRANGE` : sens de
   variation à établir par essai si un réglage s'avère nécessaire.
5. **Stabilité sur plusieurs heures** (compteur de récupérations, reset
   reason) avant usage sans surveillance.
6. ~~Image de référence~~ — faite (2026-09-26), `Clone_ESP32S3/`.


Ce document complète PLAN.md (qui couvre le portage protocolaire initial,
figé au 2026-09-09) avec un état des lieux daté du **2026-09-22** : tout
ce qui a été fait sur le projet HLK-LD6001B **après** le 2026-09-09 et qui
n'a **jamais été répercuté** sur ce projet 6001A. Radar 6001A attendu
sous peu (~2026-09-23) ; les prochaines unités qui arriveront seront
toutes des 6001A, montées sur XIAO ESP32-S3 **Plus** (même carte que la
production 6001B actuelle) — l'ESP32S3N16R8 reste réservé comme outil de
capture pass-through si du reverse-engineering est nécessaire sur ce
module, exactement comme il l'a été pour le 6001B.

## Constat : cinq vagues de travail sur 6001B, aucune répercutée ici

Le composant `hlk_ld6001a` actuel est figé à l'état de la **Phase 0**
(2026-09-09, voir PLAN.md). Depuis, cinq vagues de travail ont eu lieu
sur `HLK-LD6001B/ESP32S3_Plus/` sans équivalent ici :

1. **2026-09-16** — portage de l'UI vers un style "TI" (onglets
   Configure/Plots remplaçant l'ancien Pièce/Radar/Console AT),
   consolidation du dossier `ESP32S3_Plus/`, investigation de
   l'instabilité du XIAO ESP32-S3 Plus (flash Puya P25Q128HA).
2. **2026-09-21 (session précédente)** — décodage et affichage du nuage
   de points brut (`TLV1`/`POINTLEN`), avec deux vrais bugs firmware
   trouvés et corrigés en route.
3. **2026-09-21/22 (session suivante)** — rotations d'affichage
   persistées, refonte de la page Configure, bandeau supérieur en grille
   3 colonnes (voir `PLAN.md` Phase 12 du 6001B).
4. **2026-09-22** — industrialisation complète de l'UI : support tactile
   (Pointer Events, pincer-zoomer), boutons zoom/ajuster/recentrer sur
   toutes les vues, infobulles sourcées du manuel constructeur,
   conformité par champ, indicateurs de santé (uptime, mémoire, WiFi,
   débit trames), pause/rémanence/export/inspection d'un point — voir
   `HLK-LD6001B/ESP32S3_Plus/PLAN.md` Phase 13 et
   `PLAN-UI-INDUSTRIALISATION.md` pour le détail complet, non répété ici.
5. **2026-09-22 (cette session, suite)** — réorganisation de la page
   Configure en colonnes reporting (gauche) / configuration (droite) pure
   (permutation Advanced Commands ↔ Scene Selection), ligne 3 colonnes
   Scan/Monitoring/Heartbeat, remplacement des boutons Démarrer/Arrêter
   (constatés sans effet durable — `check_watchdog_()` relance toujours le
   radar sous 90s) par un bouton unique "Redémarrer le radar"
   (`POST /radar_restart`, cycle complet `AT+RESET`+réglages+`AT+DEBUG=2`+
   `AT+START`, vérifié en bout en bout sur le matériel réel), compaction
   supplémentaire pour tenir sans ascenseur, journal Advanced Commands
   porté à 27 lignes visibles — voir `HLK-LD6001B/ESP32S3_Plus/PLAN.md`
   Phase 14 pour le détail complet.

## Ce qui se porte directement, sans travail supplémentaire

Le Phase 0 du 6001A a été conçu avec les bonnes leçons du 6001B déjà
intégrées — plusieurs points sont donc **déjà en avance** sur ce que le
6001B avait à la même étape de son propre développement :

- **Framing TLV identique** : `HEAD(8)/LENGTH(4)/FRAME(4)/TLV1(4)/
  POINTLEN(4)/<points>/TLV2(4)/TRACKLEN(4)/<personnes>/CHECK(1)` — même
  structure octet pour octet que le `AT+DEBUG=2` du 6001B, offset 24 =
  début des octets de points dans les deux cas. Confirmé par l'exemple
  chiffré du manuel Hi-Link 6001A lui-même (PLAN.md, Journal Phase 0).
- **`POINTLEN` déjà décodé dynamiquement**, jamais supposé 0 malgré ce
  qu'affirme le manuel (`hlk_ld6001a.cpp::process_debug3_frame_()`,
  `loop()` lignes ~1736-1760) — le 6001B n'a acquis ce réflexe qu'après
  coup, sur le terrain. Le 6001A part directement avec la bonne pratique.
- **Checksum du flux détaillé déjà validé** (trame rejetée si le XOR ne
  correspond pas) — avantage du 6001A sur le 6001B, dont le checksum du
  flux `DEBUG=2` n'a jamais été confirmé donc jamais vérifié.
- **Architecture watchdog, file de commandes AT+ avec accusé de
  réception réel, serveur HTTP embarqué, préférences persistées** —
  déjà portés à l'identique.
- **Méthodologie de reverse-engineering** utilisée pour décoder le
  nuage de points du 6001B (décodeur Python de référence validé AVANT
  le C++, protocole de capture GO/FAIT corrélé au sol — pièce vide,
  personne statique, marche, tests de réflectivité — outil
  `Pass-through-tests/pc_test/point_cloud_capture.py`, passthrough
  transparent ESP32S3N16R8 sans commande AT propre) — directement
  réutilisable telle quelle pour le 6001A, seul le framing d'offset
  change (trivial, déjà connu — voir plus bas).
- **Règle "PowerShell uniquement pour esphome/esptool"** — s'applique
  déjà universellement (DEPLOYMENT.md le documente déjà).
- **`makeHlk2DView()`** (factory zoom/pan/**rotation 90°** générique, avec
  `fillTextUpright()` pour garder les graduations lisibles quand l'image
  est tournée, et `unrotatePx()`/`unrotateVec()` pour que la souris reste
  cohérente) et les **deux correctifs d'affichage iPad** — `width:100%;
  height:100%; display:block;` sur les `<canvas>`, et le bandeau
  supérieur en **grille 3 colonnes** (`auto minmax(0,1fr) auto`) plutôt
  qu'un centrage `position:absolute` qui chevauchait ses voisins. Code
  JS/CSS pur, indépendant du protocole radar, copiable tel quel une fois
  la page portée (voir section suivante).
- **Rotation d'affichage persistée** (deux champs `RoomConfig`
  indépendants, un par vue, enregistrés à chaque clic sans étape de
  validation) — le mécanisme complet (C++ + HTTP + JS) se transpose
  directement, `RoomConfig` existant déjà à l'identique côté 6001A.
  Directement pertinent ici : ces radars seront posés au plafond, donc la
  même question d'orientation se posera dès la première installation.

## Ce qui doit être refait ou adapté, pas simplement copié

### 1. Layout binaire d'un point du nuage — LE point le plus important

Les 25 octets/point du 6001B (X/Y/Z float32, 1 octet tag, D/E/F
float32) sont une **découverte empirique propre au firmware du 6001B**,
absente de son manuel — rien ne garantit qu'ils s'appliquent au 6001A.
Aucun des deux manuels 6001A ne documente non plus le contenu d'un
point individuel : l'exemple chiffré du manuel Hi-Link a `POINTLEN=0`,
donc aucun octet de payload de point n'a jamais été vu, même dans la
documentation.

**Attente précise et falsifiable pour la Phase 1** : dès qu'une trame
`AT+DEBUG=3` réelle arrive avec `POINTLEN>0` (le manuel affirme "toujours
0" — le 6001B affirmait la même chose avant que ce soit démenti sur le
terrain, donc à vérifier, jamais supposé) :

- Si `POINTLEN` est divisible par 25 → tester d'abord l'hypothèse "même
  format que le 6001B" (MS72SF1/MS72SF11 est probablement une évolution
  de la même famille de puce radar) en comparant les X/Y/Z décodés à des
  positions connues au sol, même protocole GO/FAIT que le 6001B.
  Concordance → format confirmé identique, portage direct de
  `process_debug2_frame_()` → `process_debug3_frame_()`. Écart avec le
  terrain → format proche mais pas identique, RE à refaire sur les
  champs restants (comme D/E/F sur le 6001B, jamais complètement
  élucidés).
- Si `POINTLEN` n'est pas divisible par 25, ou si aucune trame avec
  `POINTLEN>0` n'apparaît jamais après plusieurs minutes d'usage normal
  (radar actif, plusieurs personnes) → format différent ou nuage de
  points réellement toujours vide sur ce module. Dans le premier cas,
  reprendre la RE depuis zéro (même méthodologie GO/FAIT, nouvelle
  byte-map) ; dans le second, ranger cette fonctionnalité comme non
  applicable au 6001A et le documenter comme tel dans PROTOCOL.md.

### 2. Portage de l'UI TI-style + industrialisation (Configure/Plots/HLK) — pas fait dans ce document

Le composant 6001A est resté sur l'ancien `VIEWER_HTML` à 3 onglets
(Pièce / Radar / Console AT) du Phase 0 — il n'a reçu ni le remaniement
du 2026-09-16 (style TI, Configure/Plots), ni la page HLK du 2026-09-21,
ni la refonte du 2026-09-21/22 (rotations, page Configure deux colonnes,
bandeau 3 colonnes), ni l'industrialisation du 2026-09-22 (voir
`HLK-LD6001B/ESP32S3_Plus/PLAN.md` Phases 12-13 et
`PLAN-UI-INDUSTRIALISATION.md`) :

- sélecteur HLK/Plots/Configure avec HLK par défaut, état radar dans le
  bandeau supérieur (grille 3 colonnes, jamais de chevauchement quelle
  que soit la largeur d'écran) plutôt qu'en onglet ;
- page Configure en deux colonnes de même hauteur (Setup Details enrichi
  / Scene Selection / Zone de détection à gauche, Real-Time Tuning en
  grille 2 colonnes + Advanced Commands à droite), infobulles sourcées du
  manuel constructeur sur chaque réglage, commande AT+ et conformité
  affichées sous chaque champ, "Enregistrer" désactivé tant que rien n'a
  changé ;
- toutes les vues 2D/3D avec support tactile complet (Pointer Events, un
  doigt glisse, deux doigts pincent pour zoomer — indispensable, pas
  cosmétique : sans ça ces vues sont totalement figées sur iPad) plus
  boutons zoom/ajuster/rotation/recentrer et échelle affichée ;
- indicateurs de santé (uptime, mémoire libre, WiFi, récupérations
  watchdog, débit de trames) transportés dans `/hlk_targets.json`
  existant (**jamais une nouvelle route sondée en boucle** — l'épuisement
  de sockets déjà vécu deux fois sur le 6001B l'écarte d'office, voir
  MAINTENANCE.md du 6001B) et bandeau "données figées" au-delà de 3
  sondages échoués ;
- page HLK : pause, rémanence (N dernières trames en fondu), export JSON,
  inspection d'un point au tap/clic (X/Y/Z/D uniquement — même décision à
  reprendre ici si/quand le nuage de points du 6001A est décodé, voir
  point 1 ci-dessus).

C'est un travail de portage HTML/CSS/JS substantiel à part entière,
volontairement **non fait ici** (ce document est une analyse d'écart +
plan, pas une implémentation UI complète) — voir "Plan d'action" plus bas
pour où il se situe dans l'ordre des priorités.

**À porter dans l'état final du 6001B, pas dans un état intermédiaire** :
inutile de refaire les nombreuses itérations qu'ont demandées la page
Configure puis l'industrialisation côté 6001B — copier directement la
version validée (commit `3967334` et suivants sur
`github.com/thieryus007-cloud/HLK-6001B`, golden-image
`plus_68ee8f4d1988_full_flash_16MB_2026-09-22_post-industrialisation.bin`
comme référence de l'état fonctionnel correspondant).

### 3. JSON de sortie `/hlk_targets.json`

Le 6001A n'a qu'**un seul** tableau `targets` (pas de `debug2Targets`
séparé, puisqu'il n'y a qu'une seule source de position — voir
PROTOCOL.md, "Conséquence architecturale majeure"). Le bug JSON corrigé
le 21 septembre sur le 6001B (`debug2Targets` mal fermé avant
`"points"`) **n'a pas d'équivalent ici** : il n'existe que parce que le
6001B a deux tableaux à fermer. En revanche, il faudra bien ajouter un
tableau `"points":[...]` de la même façon une fois le format de point
confirmé (point 1 ci-dessus).

## Correctif appliqué aujourd'hui, sans attendre le matériel

**Risque identifié** : `hlk-ld6001a-xiao.yaml` déclare un **bloc `uart:`
unique** sur GPIO43/44 — exactement la configuration qui, sur le projet
6001B le 2026-09-21, a fait qu'ESPHome assignait `UART_NUM_0` au bus
radar (compteur statique `next_uart_num` démarrant à 0, aucun bloc
`uart:` supplémentaire pour "consommer" ce numéro avant lui), lequel
coïncide avec la fonction native UART0 de démarrage de GPIO43/44 sur
l'ESP32-S3 — résultat mesuré sur le 6001B : **0/8 commandes `AT+`
acquittées**, quel que soit le firmware testé. Ce mécanisme est un fait
du SoC ESP32-S3 (même puce, mêmes broches D6/D7 que le 6001B), pas
quelque chose de spécifique à la puce flash Puya du board Plus — le
risque s'applique donc tel quel à ce YAML.

**Corrigé préventivement** dans `esphome/hlk-ld6001a-xiao.yaml` : ajout
d'un bloc `uart:` factice en premier (GPIO17/18, non câblées, sert
uniquement à consommer `UART_NUM_0`) pour forcer `radar_uart` sur
`UART_NUM_1` — copie exacte du correctif validé sur le 6001B. Compile
sans erreur (`python -m esphome compile`, PowerShell) sans matériel
connecté, comme le reste de la Phase 0 — vérifié aujourd'hui.

## Mitigation brownout WiFi du 6001B Plus — décision finale : ne PAS porter préventivement

> **Mise à jour 2026-09-26** : condition remplie — brownouts réels
> observés sur le 6001A (premier démarrage après OTA). Les trois
> protections sont désormais en place sur le 6001A (`AT+STOP` dans
> `setup()`, `enable_on_boot: false` + 8 s, `output_power: 8.5db`), la
> troisième s'étant avérée nécessaire sur cette carte. Détail :
> PLAN.md (journal 2026-09-26), MAINTENANCE.md. Le texte ci-dessous
> reste l'historique de la décision de 2026-09-21.

**Mise à jour du 2026-09-21, après-midi** : la question "faut-il porter
la mitigation brownout WiFi du 6001B Plus vers le 6001A ?" a été testée
et tranchée le jour même sur le 6001B lui-même, pas seulement discutée.

**Test de causalité fait sur le XIAO Plus 6001B réel** (`68:ee:8f:4d:19:88`,
voir `HLK-LD6001B/ESP32S3_Plus/PLAN.md` Phase 11) : `output_power: 8.5db`
retiré, `AT+STOP` (setup()) + `wifi: enable_on_boot: false`/délai 8s
laissés inchangés. **3 cycles d'alimentation vraiment froids consécutifs
(USB-C débranché >1 minute), tous propres** — WiFi associé en 1.7-2.2s,
radar répondant et streamant normalement, aucune signature de brownout.
`output_power` s'avère **non nécessaire** sur cette unité, à cet
emplacement — retiré définitivement du 6001B.

**Correction d'une mémoire erronée découverte en chemin** : une note
précédente affirmait que le correctif retenu sur le 6001B Plus était un
changement de fréquence flash (80MHz→40MHz). Faux — cette piste avait
déjà été testée et explicitement rejetée le 16 septembre ; mémoire
corrigée.

**Décision explicite de l'utilisateur pour le 6001A** : ne porter
**aucune** des trois mitigations (ni `output_power`, ni `AT+STOP`
anticipé, ni `enable_on_boot:false`+délai) tant qu'aucun brownout réel
n'est observé sur le matériel 6001A. Raison donnée : des conclusions
passées sur ce projet ont été prises trop vite sans creuser la cause
réelle (voir la correction ci-dessus, justement) — mieux vaut laisser le
YAML 6001A tel quel ("attendre WiFi confirmé puis `AT+RESET`", déjà en
place) et réagir à un problème réellement constaté sur le 6001A plutôt
que de pré-appliquer une solution à un problème pas encore observé sur
ce module. Différent du correctif UART ci-dessus (bug confirmé,
mécanique, sans contrepartie) : ici, ni le problème ni le bon dosage du
correctif ne sont confirmés pour ce nouveau matériel.

**Si un brownout apparaît sur le 6001A+Plus en Phase 1** : la séquence à
essayer en premier est `AT+STOP`+`enable_on_boot:false`/délai (suffisant
seul sur le 6001B) — `output_power` réduit en dernier recours seulement,
avec le même test de causalité isolé (retrait/remise sur cycles à froid
répétés) avant de l'adopter, pas juste parce que le commentaire source
ESPHome le suggère.

## Plan d'action, dans l'ordre, à l'arrivée du matériel

1. Bring-up de base déjà prévu par PLAN.md Phase 1 (débit UART, chaque
   commande `AT+` incertaine testée individuellement, capture d'une
   vraie trame `DEBUG=3`, comparaison au décodeur Python de référence).
2. Vérifier que le correctif UART appliqué aujourd'hui fonctionne
   réellement sur le matériel (pas seulement en théorie) — même
   protocole de vérification que le 6001B (compte de `AT+OK` reçus).
3. Si le module tourne sur un XIAO ESP32-S3 Plus dès le départ, ne rien
   porter préventivement (décision explicite, voir section précédente)
   — seulement si un brownout réel est observé, appliquer d'abord
   `AT+STOP`+`enable_on_boot:false`/délai, tester `output_power` en
   dernier recours avec son propre test de causalité isolé.
4. Dès qu'une trame `DEBUG=3` réelle avec `POINTLEN>0` est capturée (ou
   confirmée absente après un usage prolongé) : campagne de RE du nuage
   de points, méthodologie GO/FAIT + pass-through ESP32S3N16R8, comme
   décrit plus haut.
5. Porter le parsing C++ du nuage de points (`process_debug3_frame_()`,
   nouveaux champs `latest_point_*_`, tableau JSON `points`) une fois le
   format confirmé.
6. Porter l'UI TI-style + l'industrialisation complète (Configure/Plots/
   HLK, voir point 2 ci-dessus) dans son état final du 6001B, en
   réutilisant `makeHlk2DView()` (zoom/pan/rotation/tactile/inspection),
   la barre d'outils `.view-toolbar`/`.view-tool-btn` et le correctif CSS
   iPad tels quels.
7. Golden-image + documentation, même processus que le 6001B
   (`Clone_ESP32S3/README-*.md`).

## Fichiers concernés

- Ce document — gap analysis et plan, mis à jour au fil de la Phase 1.
- `PLAN.md` — reste le journal protocolaire complet ; à continuer d'y
  ajouter les entrées "Journal Phase 1" au fur et à mesure des tests
  matériels réels.
- `PROTOCOL.md` — inchangé pour l'instant ; à mettre à jour dès que le
  format des points (ou son absence confirmée) est connu.
- `esphome/hlk-ld6001a-xiao.yaml` — correctif UART appliqué aujourd'hui
  (voir ci-dessus).
