# Outils de mesure et de diagnostic

Scripts Python utilisés pendant la mise au point (2026-09-25 → 28), copiés
ici pour rester disponibles d'une conversation à l'autre. Les adresses IP
par défaut sont celles du réseau actuel (6001A-01 `192.168.1.90`,
6001A-02 `192.168.1.201`, 6001B `192.168.1.17`) — DHCP, à revérifier.
Dépendances : `aioesphomeapi`, `zeroconf`, `pyserial` (déjà installées).

**Règle** : sur la carte 6001A-01 (`28:84:85:8a:be:00`), rien sans l'accord
explicite de l'utilisateur — voir `../../CLAUDE.md`.

| Script | Effet sur la carte | Rôle |
|---|---|---|
| `surveillance_resets.py` | lecture seule (HTTP 1/min + API sur redémarrage) | journal CSV des redémarrages et de leur raison, plusieurs cartes |
| `api_states.py <ip> [filtre…]` | lecture seule (API, comme Home Assistant) | lit une fois des entités (`Reset Reason`, `Uptime`…) |
| `esphome_discovery.py [ip…]` | lecture seule (mDNS + API `device_info`) | nom, MAC, **date de compilation du firmware en service** |
| `log_capture_reconnect.py <ip> <s> <fichier>` | lecture seule (logs API), se reconnecte après chaque redémarrage | voir les dernières lignes avant une coupure |
| `dual_record_slow.py <s> <fichier> <intervalle>` | lecture seule (HTTP) | relevé périodique des cibles de deux radars (IP dans le script) |
| `truth_eval.py <fichiers…>` | aucun (analyse hors ligne) | score « 1 personne » d'un enregistrement double |
| `verify_image.py <dump> <factory.bin> <ota.bin>` | aucun (analyse hors ligne) | vérifie qu'une image de flash contient le build en service |
| `boot_capture.py <COM> <mac> <essais> <s> <préfixe>` | **REDÉMARRE la carte** (RTS) puis capture le démarrage par USB | voir `E BOD` et la séquence de démarrage — jamais sur la 6001A-01 sans accord |

Rappels :

- Outils USB/série (esptool, esphome, `boot_capture.py`) : **PowerShell
  uniquement**, jamais Git-Bash.
- **Ouvrir puis fermer le port série USB redémarre la carte** (raison
  « USB peripheral ») : une écoute série n'est jamais « passive » à sa
  fermeture.
- Sous Windows, un changement de RTS n'est transmis que suivi d'une
  écriture de DTR (pilote `usbser.sys`) — `boot_capture.py` le fait.
