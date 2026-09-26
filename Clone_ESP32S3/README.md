# Clone_ESP32S3 — image de référence du XIAO ESP32-S3 Plus (radar HLK-LD6001A)

Image complète de la flash de l'unité en service, pour restaurer la carte
à l'identique sans recompiler. Les fichiers `.bin` restent locaux (voir
`.gitignore`) : l'application qu'ils contiennent embarque les identifiants
WiFi compilés depuis `esphome/secrets.yaml`.

## Carte

- MAC `28:84:85:8a:be:00`, nom réseau `hlk-ld6001a-xiao`
  (http://192.168.1.90/ au moment de la lecture — adresse DHCP, à
  revérifier plutôt que supposer stable).
- ESP32-S3 (QFN56) rév. v0.2, PSRAM 8 Mo (AP_3v3), flash 16 Mo Quad Puya
  (fabricant `85`, device `2018`).
- Port USB au moment de la lecture : COM59 (l'attribution des ports change
  d'une session à l'autre ; identifier la carte par sa MAC, jamais par son
  port).

L'image `plus_28848a58abe00_full_flash_16MB_2026-09-21.bin` rangée dans
`HLK-LD6001B/ESP32S3_Plus/Clone_ESP32S3/` concerne cette même carte mais
contient l'**ancien firmware HLK-LD6001B** : ne pas l'utiliser pour le
6001A.

## Image

> **Plus conforme au firmware en service** : la carte exécute depuis le
> 2026-09-26 20:57 le build du 2026-09-26 20:55:59 (anti-rebond de
> `People Count`, protections brownout, sans `uart debug` — PLAN.md). Une
> nouvelle lecture est à faire dès que la carte est rebranchée en USB.
> L'image ci-dessous reste valable pour revenir à l'état du 2026-09-25.

**`plus_2884858abe00_hlk-ld6001a_full_flash_16MB_2026-09-26.bin`** —
16 777 216 octets (`0x000000`–`0x1000000`), SHA-256
`be416b6c04fdf0063ad470524ba2e44647c2eacb1810919bb43835321de43fda`.

Contenu vérifié contre le build qui tourne sur la carte :

| Zone | Contenu |
|---|---|
| `otadata` (0x9000) | deux entrées valides, `seq=2` la plus récente → application active = `app1` |
| `app1` (0x7D0000) | **identique octet pour octet** au `firmware.ota.bin` du build `hlk-ld6001a-xiao` du 2026-09-25 18:05:02 (ESPHome 2026.8.0) — la date de compilation annoncée par la carte via l'API |
| `app0` (0x10000) | build antérieur du même jour (premier flash USB), inactif |
| `nvs` (0xF90000) | préférences en service : réglages radar `sensFar 4`, `sensNear 4`, rayon 500 cm, hauteur 300 cm, hauteur de balayage 300 cm, heartbeat 60 s, zone désactivée ; `room_config` 5,00 × 5,00 × 2,50 m, montage plafond |

Les réglages radar et la configuration de la pièce sont modifiables depuis
la page web : l'image reflète leur valeur au 2026-09-26. Après tout
changement durable, en refaire une.

## Restaurer

Vérifier l'identité de la carte avant toute écriture (MAC et flash
attendues ci-dessus) :

```powershell
python -m esptool --port <COM> flash-id
python -m esptool --port <COM> write-flash 0x0 plus_2884858abe00_hlk-ld6001a_full_flash_16MB_2026-09-26.bin
```

Écrit aussi la NVS : la carte redémarre avec ses réglages et sa
configuration de pièce. Sur une **autre** unité, l'image donnerait le même
nom réseau (`hlk-ld6001a-xiao`) que celle-ci — deux cartes de même nom se
confondent sur le réseau et dans Home Assistant. Pour une unité
supplémentaire, suivre DEPLOYMENT.md (« Déploiement d'unités
supplémentaires »).

## Lire une nouvelle image

```powershell
python -m esptool --port <COM> flash-id
python -m esptool --port <COM> read-flash 0x0 0x1000000 <fichier>.bin
```

Lecture ~130 s ; la carte redémarre à la fin (reset RTS) et revient sur le
WiFi en quelques secondes. Si esptool 5.3.1 s'arrête avec `A fatal error
occurred: Packet content transfer stopped` (bug SLIP,
[espressif/esptool#1184](https://github.com/espressif/esptool/issues/1184),
dépend du contenu de la flash) : `python -m pip install esptool==5.4.0`,
relire, puis revenir à `esptool==5.3.1`, version exigée par ESPHome
2026.8.0. Outils USB/série : PowerShell uniquement, jamais Git-Bash.
