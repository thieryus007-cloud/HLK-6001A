# Plan de portage — HLK-LD6001A (MS72SF1) sur XIAO ESP32-S3

Statut : **plan initial, en attente de validation**. Rien n'est encore
implémenté. Matériel radar attendu sous ~2 semaines — ce plan est
structuré pour distinguer ce qui peut être fait/vérifié dès maintenant
(documentation, code, décodeur de référence) de ce qui doit attendre le
matériel réel.

## Sources consultées

1. **`HLK-LD6001A-60G Human Trajectory Radar Sensor Module Manual V1.1`**
   (Hi-Link, 2024-05-11) — manuel du produit commercial réellement acheté.
   Traité comme **source la plus autoritaire** pour ce qui concerne le
   produit fini (même logique que pour le 6001B : le manuel de la marque
   qui vend le module physique prime sur le datasheet générique de la
   puce).
2. **`MS72SF1_Datasheet_K` V1.0.0** (MinewSemi, 2024-06-06) — datasheet du
   chipset générique (même puce radar, référencée "MS72SF1" ou "MS72SF11"
   selon la source) sous une autre marque. Utile pour des détails absents
   du manuel Hi-Link, mais **ses valeurs numériques divergent par endroits
   du manuel Hi-Link** (voir tableau ci-dessous) — traité comme source
   secondaire, jamais préférée au manuel Hi-Link en cas de conflit.
3. **`github.com/Devristo/esphome-hlk-ld6001`** (`Links.txt`) — composant
   ESPHome tiers, déjà existant, pour ce même module (dossier
   `esphome/components/ld6001a/`). **Pas notre modèle** (l'utilisateur a
   été explicite : le travail HLK-LD6001B est le modèle) — mais consulté
   comme **troisième témoin indépendant, empiriquement testé sur du vrai
   6001A**, pour trancher les divergences entre les deux PDF. Code +ests
   unitaires lus intégralement (`frame_parser.h`, `ld6001a.h`/`.cpp`,
   `command_queue.h`, `test/ld6001a/test_frame_parser/frame_parser.cpp`).

Recoupement à trois sources : quand les trois s'accordent, fait traité
comme solide. Quand elles divergent, la valeur du code testé de Devristo
est retenue par défaut (préférée à une valeur seulement documentée), mais
marquée **"à vérifier sur le matériel réel"** — jamais présentée comme
acquise avant un test empirique.

## Ce qui est identique au HLK-LD6001B (repris tel quel du modèle 6001B)

- **Brochage** : 3V3, NRST, TX, RX, GND — 5 broches, mêmes fonctions.
- **Alimentation** : pic ~530mA (démarrage RF), ~110mA moyen (cycle
  100ms), alimentation ≥1A requise. Identique aux marges déjà validées
  sur le 6001B — le même risque de brownout au démarrage WiFi
  (PLAN.md Phase 16 du projet 6001B) s'applique par construction.
- **Montage** : plafond, antenne vers le bas, hauteur ~2.3-3.0m.
- **UART** : 8 bits, 1 stop, pas de parité, pas de contrôle de flux.
- **Format de trame TLV détaillé** (voir section protocole) : header fixe
  `01 02 03 04 05 06 07 08`, structure HEAD/LENGTH/FRAME/TLV1/POINTLEN/TLV2/TRACKLEN/personnes
  quasi identique octet pour octet au format `AT+DEBUG=2` du 6001B.
- **Architecture logicielle** : composant externe ESPHome (`.h`/`.cpp`
  pair), file de commandes AT+ avec accusé de réception réel
  (`enqueue_at_command_`/timeout), serveur HTTP embarqué (`esp_http_server`)
  pour la vue 3D + réglages + Console AT, page web `VIEWER_HTML`
  Three.js réutilisée quasi telle quelle (mêmes vues Dessus/Face/Profil,
  même mapping d'axes paramétrable par mount, mêmes correctifs de
  robustesse du 2026-09-09 : disposal Three.js, échappement JSON, cap du
  journal console, correspondance par position, repli si CDN indisponible,
  CSS responsive).
- **Séquence de démarrage prudente** : attente WiFi confirmée avant tout
  `AT+RESET` (évite le brownout, voir Phase 16 du 6001B) — **Devristo ne
  fait PAS ça** (son `setup()` envoie `AT+RESET` immédiatement, sans
  attendre quoi que ce soit) ; on garde la version 6001B, plus robuste,
  pas celle de Devristo.
- **Watchdog d'auto-récupération** (`WATCHDOG_TIMEOUT_MS`/
  `WATCHDOG_COOLDOWN_MS`, re-déclenche la séquence complète si plus aucune
  trame ne vient) — **absent du composant de Devristo**, repris du 6001B.
- **Décodage défensif des champs de longueur variable** (`POINTLEN`,
  `TRACKLEN` bornés par soustraction avant tout calcul d'offset, jamais
  d'hypothèse "toujours 0" prise pour acquise) — **le composant de
  Devristo suppose `POINTLEN=0` de façon rigide** (offset fixe à 32,
  aucune vérification) ; on applique la leçon du 6001B (où cette même
  hypothèse s'est révélée fausse sur le terrain) et on décode
  `POINTLEN` dynamiquement dès le premier jour, même si les deux
  manuels 6001A affirment "toujours 0".
- Limite de pas de resynchronisation par appel à `loop()` (bruit UART
  prolongé ne doit jamais monopoliser la tâche principale).

## Différences protocolaires confirmées (6001A vs 6001B)

| Aspect | 6001B | 6001A | Confiance |
|---|---|---|---|
| Débit UART par défaut | 115200 | **115200** (datasheet MS72SF1 §8.1 + outil hôte du manuel Hi-Link §7.3) — **mais** la table `AT+BAUD` du même manuel Hi-Link affirme "921600" par défaut, contradiction interne au document | Moyenne — à vérifier au premier branchement (essayer 115200 d'abord) |
| Mode protocole normal (`AT+DEBUG=0`) | Trame `55 AA` TYPE=0x01, X/Y/Z/respiration/coeur par personne | Trame `55 AA` TYPE=0x04, **uniquement un compte de personnes, aucune coordonnée** | Haute (confirmé par manuel + test unitaire Devristo, octet pour octet) |
| Mode protocole détaillé | `AT+DEBUG=2` (additif, le flux normal reste la source principale de position) | `AT+DEBUG=3` (**devient la SEULE source de position/vitesse/ID** puisque le mode normal n'en fournit pas) | Haute — conséquence directe du point précédent |
| Checksum du flux détaillé | Non validé (présence incertaine sur le matériel réel, cf. MAINTENANCE.md 6001B) | **Confirmé et bien défini** : 1 octet XOR final, calculé sur FRAME (4 octets) + tous les enregistrements personne — PAS sur LENGTH/TLV/POINTLEN/TRACKLEN. Confirmé par le manuel Hi-Link (exemple chiffré) ET par `validate_frame()`+tests unitaires de Devristo | Haute — à valider si implémentée dès le départ |
| Sensibilité | `AT+SENS=XX` (1-19, def. 2) | `AT+DPKTH=X` (1-9, def. 4) — **le datasheet MS72SF1 documente à tort `AT+SENS=1-19` pour cette puce**, contredit par le manuel Hi-Link ET par le code testé de Devristo (`assert(1<=x<=9)`) | Haute (2 sources sur 3 s'accordent contre la 3e) |
| Portée radiale | `AT+RANGE` 10-1000cm, def. 600 | `AT+RANGE` — **3 valeurs différentes selon la source** : manuel Hi-Link 10-500 def.450, datasheet MinewSemi 100-2000 def.300, code testé Devristo `assert(100<=x<=500)` | Basse — utiliser la plage la plus restrictive testée (100-500) en attendant confirmation matérielle |
| Hauteur d'installation | Non applicable (zones seulement) | `AT+HEIGHTD=XXX` (50-500cm, def. 300) — confirmé par les 2 manuels ET Devristo. Un `AT+HEIGHT` distinct (250-320cm) existe dans le datasheet MinewSemi seul, absent du manuel Hi-Link et jamais utilisé par Devristo → probablement non pertinent pour ce produit, à ignorer sauf preuve contraire | Haute pour `HEIGHTD`, basse pour `HEIGHT` |
| Zones d'exclusion | 6 zones rectangulaires indépendantes (`AT+WINxRANGE`, encodage signe+dixièmes) | **Une seule zone rectangulaire** de détection (pas d'exclusion multiple) : `AT+XNega`/`AT+XPosi`/`AT+YNega`/`AT+YPosi` (entiers signés en cm, **sans le suffixe "D"** contrairement à ce qu'affirment les deux manuels — confirmé par le code testé de Devristo) + `AT+RANGE` (rayon circulaire) | Haute pour l'absence du "D" (code testé prime sur documentation) |
| Disparition de cible | Non applicable | `AT+Moving=XXX`, `AT+Static=XXX`, `AT+Exit=XXX` (unité 100ms) — nouveau, aucun équivalent 6001B | Haute (2 manuels + Devristo s'accordent) |
| Lecture de config | Aucune commande dédiée (déductions via heartbeat) | `AT+READ` — **commande réelle** qui renvoie un bloc quasi-JSON (mal formé, nécessite un nettoyage de chaîne côté firmware ancien) avec tous les paramètres courants | Haute pour l'existence, moyenne pour le format exact (variable selon version de firmware d'après le code de Devristo) |
| Trame heartbeat (TYPE 0x02) | Oui, sert à vérifier que les réglages commandés ont pris effet | **Non documentée pour le 6001A** dans les deux manuels lus | Moyenne — absence de preuve, pas preuve d'absence ; à confirmer par capture réelle |
| Nombre de cibles suivies | Jusqu'à 10 (specs), limité à 6 dans l'UI/YAML par choix pratique | Jusqu'à 10 (specs, identique) | N/A — décision à prendre, voir "Points ouverts" |
| Cadence de traitement | ~100ms (`AT+TIME` def. 100) | ≤30ms (spec datasheet) — **potentiellement 3x plus rapide** | Haute (specs) |

**Piège identifié et à éviter explicitement** : le correctif du 2026-09-09
sur le 6001B a remplacé le placeholder de la Console AT `READ` par
`SEEKING` parce que `READ` n'existe pas sur le 6001B. **Sur le 6001A,
c'est l'inverse : `AT+READ` est une vraie commande, `AT+SEEKING` n'existe
dans aucune des sources consultées.** Ne pas copier le choix "SEEKING"
fait pour le 6001B sur ce nouveau projet — utiliser `READ` comme exemple
dans la Console AT du portage 6001A.

## Conséquence architecturale majeure

Le 6001B traite le protocole normal (`AT+DEBUG=0`) comme source
principale de position et le protocole détaillé (`AT+DEBUG=2`) comme
additif (ID + vitesse en plus). **Sur le 6001A, cette hiérarchie
s'inverse** : le protocole normal ne donnant qu'un compte de personnes,
`AT+DEBUG=3` devient la source principale et unique de position/vitesse/ID
dès le premier jour. Le composant 6001A doit donc démarrer directement en
`AT+DEBUG=3` (pas en 0 comme le 6001B), et la page web n'aura qu'un seul
jeu de données cible (pas de distinction "protocole normal" vs "Debug2"
comme sur le 6001B — plus besoin de la correspondance par position ajoutée
le 2026-09-09, puisqu'il n'y aura qu'un seul flux).

## Fonctionnalités du composant de Devristo à considérer comme améliorations (hors modèle 6001B)

Non présentes dans le 6001B actuel, mais plausibles/utiles pour le
portage 6001A, à valider avec vous avant implémentation :
1. **Checksum du flux détaillé réellement validé** (voir tableau
   ci-dessus) — recommandé par défaut, contrairement au 6001B où
   l'incertitude justifiait de s'en passer.
2. **`AT+READ` périodique** pour vérifier que les réglages commandés ont
   pris effet — remplacerait le rôle que joue le heartbeat sur le 6001B
   (probablement absent ici), pour le bandeau "Etat radar" de l'onglet
   Radar.
3. **Capteur de distance par cible** (`sqrt(x²+y²+z²)` depuis le radar) —
   trivial à ajouter, absent du 6001B.
4. **Déclencheurs d'entrée/sortie de cible** par ID persistant
   (`on_target_enter`/`on_target_left`) — exploite un champ déjà présent
   dans le protocole mais inutilisé côté automatisations sur le 6001B.
5. **Comptage d'occupation par zone logicielle** (rectangle arbitraire,
   calculé côté ESP32 à partir des positions déjà décodées — indépendant
   des commandes matérielles `AT+XNega`/etc.) — idée à garder simple,
   optionnelle.
6. **Limitation explicite du débit de publication des capteurs**
   (throttle, ex. 1000ms) — Devristo l'a ajoutée spécifiquement à cause du
   cycle de traitement plus rapide (≤30ms) que celui du 6001B (~100ms) ;
   probablement nécessaire ici pour ne pas saturer l'enregistreur Home
   Assistant. Le 6001B n'en a pas besoin à son propre rythme mais le
   6001A, plus rapide, pourrait.

## Points ouverts nécessitant votre décision

1. **Nombre de cibles exposées à Home Assistant** : 6 (cohérence visuelle
   avec le 6001B, tableau 3×2 déjà existant dans `VIEWER_HTML`) ou 10
   (capacité réelle du matériel, comme le fait Devristo) ? reponse: 6
2. **Nom du composant/projet** : je propose `hlk_ld6001a` /
   `hlk-ld6001a-xiao.yaml`, par cohérence directe avec `hlk_ld6001b` /
   `hlk-ld6001b-xiao.yaml` — à confirmer.reponse: hlk-ld6001a-xiao.yaml
3. **Zones logicielles de comptage** (point 5 ci-dessus) : à inclure dès
   le portage initial, ou à reporter à une itération future ? garder simple
4. **Débit de publication des capteurs** (point 6) : valeur de throttle
   à retenir (1000ms comme Devristo, ou autre) ? 1000ms a revoir pendant les tests.

## Structure de fichiers à créer

Miroir de `HLK-LD6001B/` et `HLK-LD6001B/XIAO-ESP32S3/` :

```
MS72SF1-6001A/
├── PLAN.md                         (ce fichier — journal complet au fur et a mesure)
├── PROTOCOL.md                     (reference technique : cablage, protocole, table AT+, API HTTP)
├── DEPLOYMENT.md                   (prerequis, build, flash, installation, verification)
├── MAINTENANCE.md                  (regles operationnelles, watchdog, depannage)
├── README.md                       (pointeur, sous-dossiers)
├── Links.txt                       (deja present)
├── HLK-LD6001A-60G Human Tracking Sensor Module Manual V1.1.pdf   (deja present)
├── MS72SF1_Datasheet_K.pdf         (deja present)
├── esphome/
│   ├── secrets.yaml.example
│   ├── hlk-ld6001a-xiao.yaml
│   └── components/
│       └── hlk_ld6001a/
│           ├── __init__.py
│           ├── hlk_ld6001a.h
│           ├── hlk_ld6001a.cpp
│           ├── sensor.py
│           └── binary_sensor.py
└── testing/
    └── radar_protocol_debug3.py    (decodeur Python de reference, valide AVANT le C++,
                                      meme methodologie que pc_test/radar_protocol_debug2.py
                                      sur le projet 6001B)
```

## Phases de travail

### Phase 0 — Dès maintenant, sans matériel (cette validation + les 2 semaines suivantes)

Tout ce qui est vérifiable par lecture/compilation/tests unitaires, sans
UART réel :

1. Ce plan, validé par vous.
2. `PROTOCOL.md`/`MAINTENANCE.md`/`DEPLOYMENT.md`/`README.md` initiaux,
   même structure que le 6001B, contenu adapté aux différences ci-dessus.
3. Décodeur Python de référence (`testing/radar_protocol_debug3.py`) pour
   le format `AT+DEBUG=3`, avec tests unitaires basés sur les exemples
   chiffrés des deux manuels ET sur les fixtures de test de Devristo
   (`test_it_should_accept_binary_type2`) — validation du format AVANT
   d'écrire une seule ligne de C++, même méthodologie que le
   `pc_test/radar_protocol_debug2.py` du 6001B.
4. Composant ESPHome (`.h`/`.cpp`/`.py`) : port direct de la structure
   6001B, adapté aux commandes/plages/architecture ci-dessus. Compilable
   (`esphome compile`) sans matériel connecté — seul le flash/test réel
   attend le matériel.
5. Page web : réutilisation quasi complète de `VIEWER_HTML`, adaptée pour
   un seul flux de cibles (pas de distinction normal/détaillé), nouveaux
   contrôles de réglages (DPKTH, HEIGHTD, XNega/XPosi/YNega/YPosi au lieu
   des 6 zones), Console AT avec `READ` comme exemple.
6. YAML de production, `secrets.yaml.example`.

### Phase 1 — À l'arrivée du matériel (~2 semaines)

1. **Vérification du débit UART** en premier (115200 attendu, cf.
   incertitude documentée ci-dessus) — avant tout le reste, puisque rien
   ne fonctionne sans ça.
2. Validation empirique de chaque commande AT+ incertaine (particulièrement
   `AT+RANGE`, `AT+DPKTH`, `AT+XNega`/`AT+XPosi`/`AT+YNega`/`AT+YPosi` sans
   "D") par vrais `AT+OK`/`AT+ERR` via la Console AT.
3. Capture de trames `AT+DEBUG=3` réelles, comparaison avec le décodeur
   Python de référence — confirmation ou infirmation de "POINTLEN toujours
   0" et de l'algorithme de checksum, exactement comme le 6001B l'a fait
   pour son propre format détaillé (600+/196+ trames, methode déjà
   éprouvée).
4. Vérification de l'existence ou non d'une trame heartbeat (TYPE 0x02) —
   conditionne si le bandeau "Etat radar" utilise le heartbeat (comme le
   6001B) ou un sondage périodique `AT+READ` (plan B déjà anticipé
   ci-dessus).
5. Déploiement complet suivant `DEPLOYMENT.md`, vérification
   post-déploiement (calquée sur celle du 6001B, adaptée au nombre de
   cibles retenu).

## Journal Phase 0 (2026-09-09)

### Vérifications faites en lisant directement le texte des deux PDF (pas seulement le résumé ci-dessus)

Extraction `pdftotext -layout` des deux manuels, recoupée avec les tableaux
déjà résumés plus haut. Deux points **changent ou précisent** ce qui était
écrit plus haut :

1. **Exemple chiffré complet retrouvé et vérifié bit à bit** (manuel
   Hi-Link, §7.2.2) — utilisé comme test unitaire de
   `testing/radar_protocol_debug3.py` (comme `pc_test/test_protocol.py`
   pour le 6001B) :
   ```
   01 02 03 04 05 06 07 08 40 00 00 00 A3 01 00 00 01 00 00 00 00 00 00 00
   02 00 00 00 20 00 00 00 00 00 00 00 00 00 00 00 21 28 96 BF CB 85 20 40
   9A AB A3 3E 8A BD C1 3D 50 98 99 BD 40 52 C3 3A CC
   ```
   65 octets au total. Décodé et vérifié par calcul (pas seulement recopié) :
   HEAD(8) LENGTH=64(4) FRAME=419(4) TLV1=1(4) POINTLEN=0(4) TLV2=2(4)
   TRACKLEN=32(4) puis 1 personne (Q=0, ID=0, X=-1.173, Y=2.508, Z=0.320,
   Vx=0.095, Vy=-0.075, Vz=0.001 — le manuel arrondit/tronque ces 6 derniers
   à 2 décimales, ce qui explique les "0.31"/"0.094"/"-0.074"/"0.001" écrits
   en toutes lettres dans le manuel), puis **1 octet de checksum = 0xCC**.
   **Point non documenté explicitement mais confirmé par ce calcul :
   `LENGTH` (64) ne compte PAS l'octet de checksum final — la trame réelle
   sur le fil fait `LENGTH + 1` octets.** Vérifié : XOR de
   `FRAME(4 octets) + tous les octets des enregistrements personne
   (TRACKLEN octets)` = 0xCC, confirmant exactement la description du
   manuel ("A3 01 00 00 et les infos personne ... sont vérifiés par XOR")
   et la ligne du tableau protocolaire de ce PLAN.md ("PAS sur
   LENGTH/TLV/POINTLEN/TRACKLEN").
2. **`AT+HEATIME` existe bel et bien pour le 6001A**, documenté noir sur
   blanc dans le tableau de configuration du manuel Hi-Link (§6, "Configure
   the heartbeat interval of the protocol output (unit: s, range: 10-999,
   default value: 60)") — **contredit la ligne "Trame heartbeat (TYPE 0x02)
   : Non documentée pour le 6001A dans les deux manuels lus" du tableau plus
   haut**, qui datait d'une lecture antérieure moins approfondie. Nuance
   importante : le réglage `AT+HEATIME` existe, mais **aucun des deux
   manuels ne documente le format de la trame heartbeat elle-même** (pas de
   tableau de champs comme pour le 6001B, pas d'exemple chiffré) — donc
   l'existence du réglage ne suffit pas à en déduire le layout binaire.
   **Attente précise et falsifiable pour la Phase 1** : après le boot et
   l'envoi de `AT+HEATIME=60` (ou la valeur par défaut), si une trame
   `55 AA` avec `TYPE=0x02` apparaît sur le fil dans les ~60s qui suivent,
   la trame heartbeat existe réellement et sa structure sera à
   rétro-ingénierer depuis une capture réelle (probablement proche du
   format 6001B, à adapter aux champs DPKTH/HEIGHTD/zones du 6001A). Si
   rien n'apparaît après plusieurs minutes malgré `AT+HEATIME` accepté
   (`AT+OK`), le réglage existe mais n'a aucun effet observable sur ce
   firmware — confirmerait qu'`AT+READ` reste la seule voie pour le
   bandeau "Etat radar" (Plan B déjà anticipé plus haut).
3. **`AT+RANGE` précisé à la source primaire** : le manuel Hi-Link lui-même
   (pas seulement le résumé qu'on en avait fait) dit sans ambiguïté
   interne "range: 10-500, default value: 450" pour `AT+RANGE` — fait
   remonter la confiance de "Basse" à "Haute" pour la borne min/max
   (10-500), la valeur de Devristo (100-500) restant une contrainte plus
   stricte que son propre composant applique côté client, pas une limite
   matérielle documentée. Décision Phase 0 : bornes serveur 10-500,
   défaut 450 (manuel Hi-Link, source la plus autoritaire par la règle de
   ce projet).
4. **Confirmation directe (pas déduite) que `AT+XNegaD`/`AT+XPosiD`/
   `AT+YNegaD`/`AT+YPosiD` (AVEC le "D") sont bien ce qu'écrit le manuel
   Hi-Link** : `AT+XPosiD=XXX` (20-500, def 450), `AT+XNegaD=-XXX`
   (-500..-20, def -450), `AT+YPosiD=XXX` (20-500, def 450),
   `AT+YNegaD=-XXX` (-500..-20, def -450). Le tableau plus haut retenait
   déjà la version SANS "D" (code testé Devristo) comme la plus fiable —
   décision maintenue (code empiriquement testé prime sur documentation),
   mais je note explicitement que je n'ai, à ce stade, aucune preuve
   directe (ni PDF, ni capture) de la forme sans "D" — seule la lecture du
   code source de Devristo (faite dans une session antérieure, pas
   re-vérifiée ici faute d'accès au dépôt en local) l'atteste. **À
   confirmer en Phase 1** : essayer d'abord `AT+XNega=` (sans D) via la
   Console AT ; si `AT+ERR`, retomber sur `AT+XNegaD=`.
5. **Confirmé texte manuel** : `AT+RESTORE` (restauration réglages usine)
   et `AT+READ` (lecture réglages) existent bien tous les deux pour le
   6001A (tableau §6) — cohérent avec le PLAN.md existant.
6. **Alimentation/brochage identiques au 6001B confirmés à la source** :
   3V3/NRST/TX/RX/GND, pic 530mA, veille ~80mA, moyenne ~110mA à 100ms —
   texte du manuel Hi-Link §4.4, chiffres identiques à ceux déjà retenus
   plus haut par analogie avec le 6001B.

### Décisions de conception prises pour ce premier portage (Phase 0)

- **Checksum validé et la trame rejetée (resync 1 octet) si le XOR ne
  correspond pas** — contrairement au 6001B où le checksum du flux détaillé
  n'était pas confirmé et donc pas vérifié. Ici il est confirmé §2 ci-dessus
  et documenté sans ambiguïté, donc validé par défaut dès le premier jour
  (améliration n°1 de la liste plus haut, actée).
- **Zone unique via 4 commandes signées `AT+XNega=`/`AT+XPosi=`/
  `AT+YNega=`/`AT+YPosi=` (sans "D", à vérifier Phase 1)**, remplace le
  wizard "coin 1 / coin 2" du 6001B par 4 champs numériques directs (X-,
  X+, Y-, Y+) — plus fidèle à la façon dont le firmware radar définit
  réellement la zone (un rectangle ancré sur l'origine du radar, pas un
  rectangle libre) que ne l'aurait été un portage direct du wizard de
  capture par position. Si activée, envoie les 4 commandes ; si désactivée,
  ne les envoie simplement pas (pas de valeur sentinelle "désactivation"
  documentée pour ce format, contrairement au `999999999999` du 6001B).
- **Pas de décodage de trame heartbeat (TYPE 0x02)** pour cette première
  version, faute de layout confirmé (voir point 2 ci-dessus) — le bandeau
  "Etat radar" repose sur un sondage périodique `AT+READ` (toutes les 15s,
  valeur arbitraire choisie pour ce premier jet, à ajuster), dont la
  réponse texte brute est affichée telle quelle côté web (pas de parsing
  champ par champ tant que le format réel de la réponse n'a pas été
  capturé sur le vrai matériel — voir Phase 1).
- **Aucun capteur respiration/coeur/geste** : ce protocole (DEBUG=3) ne les
  fournit pas du tout (contrairement au 6001B où ils existent, même
  toujours à 0) — supprimés plutôt que publiés à une valeur bidon.
- **Une seule source de cibles** (DEBUG=3 : ID, X, Y, Z, Vx, Vy, Vz) — pas
  de correspondance à établir entre deux flux comme sur le 6001B (voir
  "Conséquence architecturale majeure" plus haut) : `matchDebug2Targets()`
  et son équivalent JS n'ont pas de raison d'être ici.
- **Publication vers HA limitée à 1000ms** (point 4 des "points ouverts",
  décision utilisateur) — un simple filtre "pas plus d'une publication par
  capteur toutes les 1000ms", indépendant du flux `/hlk_targets.json` de la
  vue 3D qui reste rafraîchi à chaque trame valide (même principe "vue
  brute jamais filtrée" que le 6001B).
- **Reportés à une itération future, non implémentés ici** (cohérent avec
  "garder simple", réponse au point 3) : zones logicielles de comptage par
  rectangle arbitraire, capteur de distance par cible, déclencheurs
  entrée/sortie par ID persistant. Triviaux à ajouter plus tard sur la
  base des champs déjà décodés.

## Journal Phase 0bis (2026-09-21)

État des lieux complet du travail fait sur le projet HLK-LD6001B depuis
cette Phase 0 (UI TI-style du 16 sept, nuage de points + 2 bugs firmware
corrigés le 21 sept) et de ce qui en est porté ou non ici — voir
**[PORTAGE-6001B-VERS-6001A.md](PORTAGE-6001B-VERS-6001A.md)** pour le
détail complet, non dupliqué ici.

Seul changement de code appliqué aujourd'hui, sans attendre le matériel
(compile vérifié) : `esphome/hlk-ld6001a-xiao.yaml` — bloc `uart:`
factice ajouté en premier pour forcer `radar_uart` sur `UART_NUM_1`,
correctif préventif porté à l'identique du bug UART_NUM_0/GPIO43-44
découvert sur le 6001B le 21 septembre (0/8 `AT+OK` avant correctif, 7/8
après, sur les mêmes broches). Reste à vérifier réellement en Phase 1
que ce correctif tient sur le matériel 6001A une fois reçu — même
protocole de vérification que le 6001B.

## Journal Phase 1 (2026-09-25) — matériel reçu

### Matériel en place

XIAO ESP32-S3 **Plus** `28:84:85:8a:be:00` (COM59, flash Puya 16 Mo — la
même unité que celle des bootloops brownout 6001B du 2026-09-15, et dont
une image complète existe : `HLK-LD6001B/ESP32S3_Plus/Clone_ESP32S3/
plus_28848a58abe00_full_flash_16MB_2026-09-21.bin`) câblée au radar
HLK-LD6001A par l'utilisateur. À l'arrivée, elle portait encore un
firmware 6001B (UI TI du 2026-09-21, antérieure au nuage de points),
joignable en 192.168.1.90.

### Observations faites AVANT tout changement (lecture seule)

Le firmware 6001B encore présent envoie sa propre séquence au radar
(`AT+STOP`, `AT+RESET`, réglages 6001B, `AT+DEBUG=2`, `AT+START`) — ce qui
a permis d'observer le 6001A en `AT+DEBUG=2` sans rien flasher :

1. **Débit : 115200 confirmé.** Le firmware 6001B (115200) décode des
   trames TLV réelles du 6001A (`/hlk_targets.json` : 2 cibles, ID
   persistants 3 et 6, X/Y/Z et vitesses qui évoluent). La mention
   « 921600 par défaut » de la table `AT+BAUD` du manuel ne correspond pas
   au module livré.
2. **Capture de 35 s des octets bruts** (logs `uart_debug` via l'API
   WiFi, aucune commande envoyée) → 12 251 octets, conservés tels quels
   dans `testing/fixtures/debug2_rx_2026-09-25.bin`. Sur **349/349
   trames** : `LENGTH` = taille exacte de la trame, **aucun octet de
   checksum final** en `DEBUG=2` (l'en-tête suivant commence pile à
   l'offset `LENGTH`), relation `LENGTH = 24 + POINTLEN + 8 + TRACKLEN`
   exacte, `TLV1 = 1`, `TLV2 = 2`, compteur `FRAME` continu (+1 à chaque
   trame, aucune perte), ~10 trames/s.
3. **Nuage de points présent en `DEBUG=2`** : `POINTLEN` ∈ {0, 25, 50,
   75, 100}, toujours multiple de 25 → même taille d'enregistrement que le
   6001B. Décodé avec le layout 6001B : X/Y/Z plausibles (0–4,4 m), D
   entre 2,0 et 13,3 (plage de la légende couleur constructeur), octet
   tag ∈ {-5,-1,0,+1}, mais **E ne vaut que 255,0 ou 7,0** (toujours < 2
   sur le 6001B) → sémantique de E/F différente, non utilisés. Contenu
   X/Y/Z **pas encore validé contre une vérité terrain** (aucune personne
   suivie pendant la fenêtre de capture : `TRACKLEN = 0` sur les 349
   trames).
4. Cohérent avec le datasheet MS72SF1 §9 (« R&D Mode displays the point
   cloud, Demo Mode does not ») et le manuel Hi-Link (`DEBUG=2` = mode du
   logiciel PC, `DEBUG=3` = mode « démonstration », `POINTLEN` « toujours
   0 »).
5. **`AT+READ` → `AT+ERR`** quand le radar est en streaming (`DEBUG=2`,
   `START`) : réponse brute `41 54 2B 45 52 52 0D 0A` (« AT+ERR\r\n »),
   rien d'autre.

### Vérification du code de Devristo (sources téléchargées ce jour)

- Il envoie bien `AT+XNega=`/`AT+XPosi=`/`AT+YNega=`/`AT+YPosi=` (sans
  « D »), `AT+DPKTH`, `AT+HEIGHTD`, `AT+Moving`/`AT+Static`/`AT+Exit`
  (unité 100 ms), UART 115200.
- **Mais son parseur accepte n'importe quelle ligne `AT+...\r\n` comme
  accusé, `AT+ERR` compris**, et n'écoute pas `Save Para Fail` (fonction
  écrite mais absente de la liste des matchers) : son code ne prouve donc
  PAS que la forme sans « D » est acceptée. Question toujours ouverte, à
  trancher sur le matériel (voir plus bas).
- Réponse `AT+READ` attendue selon lui : bloc pseudo-JSON commençant par
  `{`, clés `PeopleCntSoftVerison`, `RangeRes`, `VelRes`, `TIME`, `PROG`,
  `Range`, `Sen`, `Heart_Time`, `Debug`, `detectionHeight`,
  `XboundaryN/P`, `YboundaryN/P`, `Moving target`, `Static target`,
  `Target exit` (valeurs en secondes, suffixe « s »), fixups propres à
  certaines versions de firmware (`NOP_1.07-01`).
- **Conséquence pour notre composant Phase 0** : son sondage `AT+READ`
  toutes les 15 s ne pouvait jamais afficher les paramètres (seule une
  ligne `AT+OK`/`AT+ERR` était capturée, jamais le bloc `{...}`) et
  occupait la file 5 s à chaque fois — supprimé dans le portage.

### Décisions de ce jour

- **Mode d'exploitation : `AT+DEBUG=2`** (au lieu de 3 retenu en
  Phase 0) — seul mode qui fournit le nuage de points dont la page HLK a
  besoin, avec les mêmes enregistrements personne. Le parseur accepte les
  deux cadrages (avec/sans octet de checksum, détection automatique,
  logique validée d'abord en Python : `testing/radar_protocol_tlv.py` +
  `test_protocol_tlv.py`, 349 trames réelles + flux `DEBUG=3` synthétique
  + corruption + texte intercalé + bascules de mode). Revenir à
  `DEBUG=3` = une constante (`RADAR_OPERATING_DEBUG_MODE`).
- **Portage complet de l'UI finale du 6001B** (demande utilisateur :
  « l'ancien site web n'est plus à jour ») — voir PORTAGE-6001B-VERS-
  6001A.md pour la liste des adaptations.

### Déploiement du nouveau firmware (demande utilisateur : effacer l'ancien soft)

Identité vérifiée avant écriture (`esptool flash-id` : MAC
`28:84:85:8a:be:00`, flash `85`/`2018` 16 Mo), `esptool erase-flash`
(7,3 s), flash USB (`Hash of data verified`). Premier boot propre
(`Boot seems successful`), aucun signe de brownout — alors que cette
unité précise avait fait des bootloops avec le 6001B le 2026-09-15 et
qu'aucune mitigation n'a été portée (décision du 2026-09-21 respectée).
Correctif UART (`UART_NUM_1`) validé : chaque commande envoyée a reçu une
réponse explicite du radar.

### Découverte : le firmware radar n'accepte pas le jeu de commandes du manuel Hi-Link

`AT+READ` (radar arrêté) renvoie :
`{ "SoftVerison":"NOP_2.11-20260525-minesemi", "RangeRes":0.055664,
"VelRes":0.111289, "TIME":100, "PROG":2, "BautRate":115200,
"Heart_Time":60, "DPKN":5, "DPKF":4, "PointTHV":2, "PointTH":10,
"FreeTime":20, "FreeTimeNoise":100, "FreeNumNoise":5, "Hrange":200,
"Height":270, "Range":450, "XdetectionN":-300, "XdetectionP":300,
"YdetectionN":-300, "YdetectionP":300 }` — firmware MinewSemi daté du
25/05/2026, clés très différentes de celles vues par Devristo
(`NOP_1.07`). Premier cycle de configuration (commandes du manuel) :
`AT+STOP` OK, `AT+RANGE=450` → `AT+OK=450`, `AT+HEATIME=60` →
`AT+OK=60`, mais **`AT+DPKTH`, `AT+HEIGHTD`, `AT+Moving`, `AT+Static`,
`AT+Exit` → `AT+ERR`**.

Sondage des noms candidats (radar arrêté, en renvoyant à chaque fois la
valeur déjà en place, `AT+READ` de contrôle identique) puis **test
changement → relecture → restauration** pour relier chaque commande à sa
clé (état final vérifié identique à l'état initial) :

| Commande acceptée | Clé `AT+READ` | Observations |
|---|---|---|
| `AT+DPKTHF` | `DPKF` | 5 puis 4 relus ; équivalent du `AT+DPKTH` du manuel |
| `AT+DPKTHN` | `DPKN` | 6 puis 5 relus ; absente des deux manuels |
| `AT+RANGE` | `Range` | 440 puis 450 relus |
| `AT+HEIGHT` | `Height` | 250, 320, 321 acceptés ; 249 appliqué mais sans accusé (timeout) |
| `AT+HRANGE` | `Hrange` | 210, 280 (> Height) acceptés ; absente des manuels |
| `AT+HEATIME` | `Heart_Time` | 61 puis 60 relus |
| `AT+XNegaD` / `XPosiD` / `YNegaD` / `YPosiD` | `XdetectionN/P`, `YdetectionN/P` | ±500 accepté ; **la forme sans « D » répond `AT+ERR`** → question ouverte depuis la Phase 0 tranchée, le manuel avait raison et le code de Devristo tort sur ce firmware |

Refusées (`AT+ERR`) dans toutes les graphies essayées : `AT+DPKTH`,
`AT+HEIGHTD` (même ≤ `Height`), `AT+Moving`/`Static`/`Exit`,
`AT+DPKF`/`DPKN`, `AT+FREETIME`/`FreeTime`, `AT+FREETIMENOISE`,
`AT+FREENUMNOISE`, `AT+POINTTH`/`PointTH`/`POINTTHV`, `AT+XNega` (sans
D), `AT+XdetectionN`.

**Zone** : le radar applique toujours des bornes (±300 cm trouvés à la
livraison, valeur du datasheet MinewSemi, pas ±450 du manuel Hi-Link).
Décision : zone « désactivée » dans l'UI = bornes envoyées au maximum
(±500 cm), pour que seul le cercle `AT+RANGE` limite réellement la
détection — ne rien envoyer laisserait une restriction cachée à ±3 m.

Firmware adapté en conséquence (réglages : DPKTHF, DPKTHN, RANGE, HEIGHT,
HRANGE, HEATIME, zone avec « D »), recompilé sans avertissement, mis à
jour par OTA : **les six réglages et les bornes de zone relus par
`AT+READ` sont conformes aux valeurs commandées** ; ~9,4 trames/s,
cadrage sans checksum détecté, 0 trame rejetée, 0 récupération watchdog.

### Validation utilisateur (2026-09-25)

Interface web et fonctionnement validés par l'utilisateur ; Home Assistant
OK. Une invite de découverte « HLK-LD6001B plafond (XIAO ESP32-S3 Plus)
(hlk-ld6001b-xiao-plus) » est apparue dans Home Assistant : vérification
en mDNS (`_esphomelib._tcp`) et par l'API de chaque carte — le 6001A
(192.168.1.90, `28:84:85:8a:be:00`) annonce `hlk-ld6001a-xiao` /
« HLK-LD6001A plafond (XIAO ESP32-S3 Plus) » ; l'invite correspond au
6001B de production (192.168.1.17, `68:ee:8f:4d:19:88`), sous tension
sur le réseau. Installation au plafond reportée.

### Incidents de test (artefacts, pas des défauts du firmware)

- L'ESP32 a redémarré une fois (raison « USB peripheral ») juste après
  l'arrêt de la capture de logs série : la fermeture du port USB a
  réinitialisé la carte, comme déjà vécu sur le 6001B. Captures
  suivantes faites via l'API WiFi (aucun effet sur l'USB).
- Une récupération watchdog (compteur à 1) : les sondages AT ont laissé
  le radar arrêté plus de 90 s — le mécanisme a relancé le radar comme
  prévu. Compteur revenu à 0 après la mise à jour OTA.

## Journal 2026-09-26 — images de référence

Demande utilisateur : « les images doivent être conformes à ce qui est en
place ». Lectures seules (`esptool read-flash`), identité vérifiée par
`flash-id` avant chacune.

- **6001A** (`28:84:85:8a:be:00`, COM59) :
  `Clone_ESP32S3/plus_2884858abe00_hlk-ld6001a_full_flash_16MB_2026-09-26.bin`,
  lue en 130,1 s avec esptool 5.3.1 (pas de bug SLIP cette fois).
  Application active `app1` (otadata `seq=2`) identique octet pour octet
  au build du 2026-09-25 18:05:02, date annoncée par la carte via l'API.
  `app0` garde le build du premier flash USB. NVS : réglages
  `sensFar 4 / sensNear 4`, rayon 500, hauteur 300, balayage 300 (valeurs
  changées depuis la page entre 15 h 20 et la lecture, relues sur le radar
  au redémarrage). Carte de retour en ligne (`uptimeS` 12, 0 trame
  rejetée).
- **6001B** (`68:ee:8f:4d:19:88`, COM64) : sixième sauvegarde, firmware
  Phase 15, documentée dans
  `HLK-LD6001B/ESP32S3_Plus/Clone_ESP32S3/README-plus.md` (dépôt
  `HLK-6001B`, commit `fab3ba6`), avec une note : l'image du 2026-09-21 de
  la carte `28:84:85:8a:be:00` contient l'ancien firmware 6001B.

### Comparaison de détection 6001A / 6001B (même instant, 15 h 20)

Même `room_config` sur les deux (5 × 5 × 2,5 m, plafond). 6001B : 2 cibles ;
6001A : 6 cibles, avec `DPKF 1 / DPKN 1` (le bas de l'échelle, c.-à-d. le
plus sensible selon le manuel), rayon 500, hauteur 300, balayage 300.

- Le 6001B tourne entièrement sur ses valeurs d'usine (`AT+SENS=2`,
  `AT+RANGE=600`, `AT+TIME=100`, `AT+MONTIME=1`, `AT+HEATIME=60`, aucune
  porte/rideau) ; sa hauteur de montage interne n'est ni réglable ni
  lisible (`AT+HEIGHT`/`AT+READ` → `AT+ERR`, journal HLK-LD6001B du
  2026-09-06). Aucune correspondance documentée entre `AT+SENS` (1-19) et
  `AT+DPKTHF`/`DPKTHN` (1-9).
- Réglages proposés pour le 6001A : valeurs d'usine pour la sensibilité
  (`DPKTHF 4`, `DPKTHN 5`) et la hauteur de balayage (200), géométrie
  réelle pour le reste (`AT+HEIGHT` = hauteur de pose, rayon ≥ 354 cm
  pour couvrir une pièce 5 × 5 depuis son centre, zone désactivée).
  Attendu : le nombre de cibles du 6001A rejoint celui du 6001B dans la
  même scène ; sinon ajuster `DPKTHF` d'un cran à la fois, puis
  `HRANGE`/`HEIGHT` un par un.
- Différences de code qu'aucun réglage ne supprime : le 6001B publie son
  nombre de cibles à partir des trames `55 AA` avec un anti-rebond de
  60 s sur les hausses (`TARGET_COUNT_INCREASE_DEBOUNCE_MS`) ; le 6001A
  publie la liste TLV sans anti-rebond. Positions HA au pas de 10 cm sur
  le 6001B, au centimètre sur le 6001A. Portage de l'anti-rebond proposé
  à l'utilisateur, après validation des réglages (une variable à la
  fois).

## Risques identifiés

- **Débit UART ambigu** (115200 vs 921600 selon la source) — impact
  faible (facile à tester dans les deux sens dès le premier branchement)
  mais à ne pas négliger : un mauvais débit ressemble à un module mort.
- **Plage `AT+RANGE` incertaine à 3 valeurs différentes** — impact faible
  (validation côté serveur déjà prévue, comme sur le 6001B, donc une
  valeur hors plage réelle est simplement rejetée par le radar, pas
  dangereux) mais peut limiter la portée utile tant que non clarifié.
- **Absence de heartbeat 6001A non confirmée** — si confirmée, le
  mécanisme "Etat radar : correspond / NE CORRESPOND PAS" du 6001B doit
  être repensé (sondage actif `AT+READ` au lieu de lecture passive) —
  déjà anticipé dans ce plan, pas un blocage.
- **Cadence de données plus rapide (≤30ms)** — risque de reproduire le
  problème d'épuisement de sockets déjà rencontré deux fois sur le 6001B
  (Phases 13-14) si le débit de publication/sondage n'est pas maîtrisé
  dès le départ plutôt que corrigé après coup.
