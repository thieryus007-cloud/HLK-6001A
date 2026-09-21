# HLK-LD6001A (MS72SF1) — déploiement plafond sur XIAO ESP32-S3

Firmware ESPHome pour le radar mmWave Hi-Link HLK-LD6001A (détection de
trajectoire multi-cible, montage plafond), piloté par un Seeed XIAO
ESP32-S3. Portage direct du projet HLK-LD6001B (même carte, même
architecture de composant) — voir PLAN.md pour le détail complet des
différences protocolaires entre les deux modules et les décisions de
conception qui en découlent.

**Statut (2026-09-21)** : Phase 0 (documentation + code, sans matériel)
terminée et compile sans erreur. Matériel radar attendu sous peu — voir
PLAN.md pour ce qui reste à valider en Phase 1 une fois le module reçu,
et PORTAGE-6001B-VERS-6001A.md pour le travail fait sur le projet
HLK-LD6001B depuis le 2026-09-09 et pas encore répercuté ici.

## Documents de ce dossier

- **[PLAN.md](PLAN.md)** — plan de portage complet : sources consultées,
  différences protocolaires 6001A vs 6001B, décisions de conception,
  journal des vérifications faites directement dans les manuels PDF.
- **[PORTAGE-6001B-VERS-6001A.md](PORTAGE-6001B-VERS-6001A.md)** — état
  des lieux daté (2026-09-21) : ce qui a changé sur le 6001B depuis la
  Phase 0 (UI TI-style, nuage de points, correctifs firmware) et qui
  reste à porter ici, plan d'action pour la Phase 1.
- **[PROTOCOL.md](PROTOCOL.md)** — référence technique : câblage, le
  protocole détaillé `AT+DEBUG=3` (seule source de position pour ce
  modèle), table des commandes `AT+`, séquence de démarrage, API HTTP.
- **[DEPLOYMENT.md](DEPLOYMENT.md)** — prérequis, build, flash,
  installation physique, vérification post-déploiement.
- **[MAINTENANCE.md](MAINTENANCE.md)** — règles opérationnelles,
  récupération automatique, limitations connues, dépannage.

Le firmware (YAML + composant C++) vit dans `esphome/`, sous ce même
dossier — `hlk-ld6001a-xiao.yaml` et `components/hlk_ld6001a/`.

## Sous-dossiers

- **`esphome/`** — firmware complet et autosuffisant : composant
  `components/hlk_ld6001a/`, le YAML de production
  (`hlk-ld6001a-xiao.yaml`), `secrets.yaml.example` comme gabarit
  (`secrets.yaml` réel jamais commité).
- **`testing/`** — décodeur Python de référence
  (`radar_protocol_debug3.py`) et son test unitaire
  (`test_protocol_debug3.py`), validés contre l'exemple chiffré exact du
  manuel Hi-Link **avant** toute ligne de C++ — même méthodologie que
  `HLK-LD6001B/pc_test/radar_protocol_debug2.py`.

## Référence matérielle (racine de ce dossier)

- `HLK-LD6001A-60G Human Tracking Sensor Module Manual V1.1.pdf` —
  manuel officiel Hi-Link (référence la plus autoritaire pour ce
  produit).
- `MS72SF1_Datasheet_K.pdf` — datasheet MinewSemi, même puce radar sous
  une autre marque ; référence secondaire, ses valeurs numériques
  divergent par endroits du manuel Hi-Link (voir PLAN.md).
- `Links.txt` — pointeur vers `github.com/Devristo/esphome-hlk-ld6001`,
  composant tiers indépendant utilisé comme troisième témoin pour
  trancher les divergences entre les deux PDF (pas notre modèle
  d'architecture — voir PLAN.md).
