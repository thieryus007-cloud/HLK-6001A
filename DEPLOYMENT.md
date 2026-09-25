# Déploiement

## Prérequis

- ESPHome 2026.8.0 (`python -m esphome`), esptool 5.3.1 (version épinglée
  par ESPHome).
- **PowerShell obligatoire pour toute commande `esphome`/`esptool`** —
  jamais Git-Bash/MSYS : la compilation y « réussit » sans produire de
  firmware, et les accès USB/série y échouent.
- `esphome/secrets.yaml` avec `wifi_ssid` et `wifi_password` (gabarit :
  `secrets.yaml.example`, jamais commité).

## Matériel

Seeed **XIAO ESP32-S3 Plus** (board ESPHome `seeed_xiao_esp32_s3_plus`,
flash 16 Mo). Unité en service : MAC `28:84:85:8a:be:00`. Câblage radar :
voir PROTOCOL.md.

## Build et flash

Depuis `C:\ncs\projects\MS72SF1-6001A\esphome\`, en PowerShell :

```powershell
python -m esphome compile hlk-ld6001a-xiao.yaml
```

Après toute modification de la page web embarquée (`VIEWER_HTML`) ou un
changement de carte : `python -m esphome clean hlk-ld6001a-xiao.yaml`
avant de compiler, puis vérifier qu'une chaîne récemment ajoutée est bien
présente dans `.esphome/build/hlk-ld6001a-xiao/build/firmware.ota.bin`.

**Vérifier l'identité de la carte avant toute écriture** :

- par USB : `python -m esptool --port <COM> flash-id` → MAC attendue et
  flash `85`/`2018` (Puya 16 Mo) ;
- par OTA : la table ARP doit associer l'IP visée à la MAC attendue
  (`Get-NetNeighbor -IPAddress <IP>`).

Premier flash (carte vierge ou autre firmware) par USB, en effaçant
d'abord la flash — cela supprime aussi toute ancienne partition OTA vers
laquelle un retour automatique serait possible :

```powershell
python -m esptool --port <COM> erase-flash
python -m esphome upload hlk-ld6001a-xiao.yaml --device <COM>
```

Mises à jour suivantes par OTA :

```powershell
python -m esphome upload hlk-ld6001a-xiao.yaml --device <IP>
```

Logs : préférer l'API WiFi (`python -m esphome logs hlk-ld6001a-xiao.yaml
--device <IP>`). Ouvrir ou fermer le port série USB peut réinitialiser la
carte (raison de reset « USB peripheral »). Ne jamais laisser plusieurs
sessions de logs ouvertes en parallèle, car chacune garde une connexion
API.

## Installation physique

- Radar au plafond, antenne vers le bas, hauteur 2,5–3,0 m (manuel
  Hi-Link §5.2). Régler « Hauteur d'installation » (page Configure,
  `AT+HEIGHT`, 2,50–3,20 m) sur la hauteur réelle.
- Câblage : PROTOCOL.md. Jamais de second maître UART en parallèle sur
  TX/RX du radar.

## Déploiement d'unités supplémentaires

1. Partagés : `esphome/secrets.yaml`, `esphome/components/hlk_ld6001a/`.
2. Par unité : copier `hlk-ld6001a-xiao.yaml` sous un nouveau nom et
   changer `substitutions: name`/`friendly_name`, car deux unités de même
   nom se confondraient sur le réseau et dans Home Assistant.
3. Vérification post-déploiement complète pour chaque unité, sans
   raccourci. Le jeu de commandes a été établi sur le firmware radar
   `NOP_2.11-20260525-minesemi` : vérifier la version rapportée par
   `AT+READ` (Setup Details → « Firmware radar »). Avec une autre version,
   contrôler que les six réglages sont conformes (lignes vertes) avant de
   s'y fier.

## Vérification post-déploiement

1. Logs de boot : pas de `E BOD`, pas de redémarrages en boucle ;
   `Boot seems successful` au bout de ~60 s.
2. Séquence de configuration dans les logs : 12 lignes `AT response
   received: AT+OK...` puis `AT+READ reply`, `sending AT+DEBUG=2`,
   `sending AT+START`.
3. `http://<IP>/`, page Configure : les six lignes sous les réglages en
   vert (✓) et la zone conforme ; Setup Details → Protocole « TLV
   AT+DEBUG=2 (sans checksum) », cadence ~9-10 trames/s, 0 trame
   rejetée.
4. Page HLK : les compteurs Trames/POINTLEN évoluent ; une personne qui se
   déplace apparaît comme cible numérotée (ID) et produit des points.
5. Page Configure → Scene Selection : géométrie réelle de la pièce.
6. Home Assistant : appareil `hlk-ld6001a-xiao` adopté ; entités
   `Presence`, `People Count`, `Target 1..6` (X/Y/Z, Vx/Vy/Vz, ID),
   `Radar Recovery Count`, `Reset Reason`, `Uptime`, `Heap Free`,
   `Loop Time`. Les cibles sont publiées au plus une fois par seconde ;
   `Presence` est immédiate.
