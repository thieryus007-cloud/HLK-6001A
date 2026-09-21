# Déploiement

## Prérequis

- ESPHome installé.
- **PowerShell obligatoire pour `esphome compile`/`upload`/`clean` sur
  Windows — jamais Git-Bash/MSYS.** Voir
  `HLK-LD6001B/XIAO-ESP32S3/DEPLOYMENT.md` pour le détail complet du
  symptôme (compilation "réussie" sans firmware produit) — même cause
  (`idf.py` détecte MSYS et avorte silencieusement), même base ESPHome.
- `esphome/secrets.yaml` doit définir `wifi_ssid` et `wifi_password`
  (voir `secrets.yaml.example`) — jamais en clair dans un fichier commité.

## Build et flash

Depuis `C:\ncs\projects\MS72SF1-6001A\esphome\`, en PowerShell :

```powershell
python -m esphome compile hlk-ld6001a-xiao.yaml
python -m esphome upload hlk-ld6001a-xiao.yaml --device <IP-du-device>
```

Le premier flash nécessite un câble USB-C. Les flashs suivants se font
par OTA via `--device <IP>`.

```powershell
python -m esphome logs hlk-ld6001a-xiao.yaml --device <IP-du-device>
```

## Installation physique

- Radar monté au plafond, antenne vers le bas, hauteur 2.5–3.0 m (manuel
  Hi-Link §5.2 — légèrement différent du 6001B qui recommandait
  2.3–2.8 m, à respecter pour ce modèle spécifiquement).
- Câblage : voir PROTOCOL.md — identique au 6001B (mêmes broches).
- **Ne jamais câbler un second maître UART en parallèle sur les broches
  TX/RX du radar pendant que l'ESP32 y est connecté** — voir
  MAINTENANCE.md.

## Déploiement d'unités supplémentaires

Même principe que le 6001B (voir
`HLK-LD6001B/XIAO-ESP32S3/DEPLOYMENT.md` pour le détail) :

1. **Partagé, ne pas dupliquer** : `esphome/secrets.yaml`, le composant
   `esphome/components/hlk_ld6001a/` tel quel.
2. **À changer par unité** : `substitutions: name`/`friendly_name` en
   tête de `hlk-ld6001a-xiao.yaml`. Copier le fichier YAML sous un
   nouveau nom par emplacement plutôt que de modifier en place.
3. **Câblage** : identique pour chaque unité (PROTOCOL.md).
4. Vérification post-déploiement complète pour chaque nouvelle unité,
   aucun raccourci "identique à la précédente" — en particulier le
   **débit UART** (voir PROTOCOL.md, incertitude documentée 115200 vs
   921600) doit être revérifié unité par unité tant qu'aucune n'a encore
   confirmé laquelle des deux valeurs est la bonne.

## Vérification post-déploiement

1. `esphome logs` : boot propre, WiFi établi, pas de `E BOD`/boot loop
   (voir MAINTENANCE.md du 6001B pour le diagnostic si ça arrive).
2. **Débit UART en premier** (voir PROTOCOL.md) : si aucune trame
   `01 02 03 04 05 06 07 08` n'apparaît dans les logs UART (`direction:
   BOTH` temporairement) dans les secondes qui suivent `AT+START`,
   basculer `baud_rate` du YAML à 921600 avant de suspecter autre chose.
3. Ouvrir `http://<IP>/` : la vue 3D doit afficher le radar et réagir à
   un mouvement dans la pièce.
4. Onglet "Radar" : `Etat radar` doit afficher une réponse `AT+READ`
   dans les ~15-30s (voir MAINTENANCE.md — pas de comparaison
   automatique commandé/réel sur cette première version, contrairement
   au 6001B).
5. Onglet "Piece" : configurer la géométrie réelle de la pièce.
6. Home Assistant : `Presence`, `People Count`, et `Target 1` à
   `Target 6` (X/Y/Z, Vx/Vy/Vz, ID — pas de Respiration/Fréquence
   cardiaque, ce protocole ne les fournit pas) doivent apparaître et se
   mettre à jour, avec un throttle d'au plus une publication par capteur
   par seconde (voir PLAN.md, "Journal Phase 0" — valeur à réévaluer
   pendant les tests réels).
