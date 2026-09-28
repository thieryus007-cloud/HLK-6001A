# Règles du projet HLK-LD6001A (MS72SF1) — à lire en entier avant tout travail

Complète `C:\ncs\CLAUDE.md` (règles générales, toujours applicables) et
`C:\ncs\projects\HLK-LD6001B\CLAUDE.md` (projet d'origine, mêmes cartes
XIAO). Journal complet : `PLAN.md` ; enquête brownout en cours :
`TRANSITION-BROWNOUT-6001A.md`.

## Cartes — faits établis par l'utilisateur, à ne pas remettre en question

| Carte | XIAO ESP32-S3 Plus | Nom réseau | IP (DHCP) | Alimentation |
|---|---|---|---|---|
| **6001A-01** | `28:84:85:8a:be:00` | `hlk-ld6001a-xiao` | 192.168.1.90 | **hub USB auto-alimenté** (avec le 6001B) |
| **6001A-02** | `7c:4f:ad:1e:98:fc` | `hlk-ld6001a-xiao-02` | 192.168.1.201 | **port USB du PC, seule** |
| 6001B (autre projet) | `68:ee:8f:4d:19:88` | `hlk-ld6001b-xiao-plus` | 192.168.1.17 | hub USB auto-alimenté |

Radars des deux cartes 6001A : HLK-LD6001A, firmware radar
`NOP_2.11-20260525-minesemi`. Les ports COM changent d'une session à
l'autre : identifier une carte **par sa MAC** (numéro de série USB ou
`esptool flash-id`), jamais par son port.

## Règles impératives

1. **Carte 6001A-01 : NE PAS Y TOUCHER sans accord explicite de
   l'utilisateur** (consigne du 2026-09-28) — ni écriture, ni esptool, ni
   ouverture du port série (sa fermeture redémarre la carte), ni
   redémarrage, ni commande radar, ni changement de réglage, ni
   surveillance continue. Seules des lectures d'état ponctuelles (HTTP,
   API) quand elles sont nécessaires, en le disant.
2. **Ne jamais mélanger 6001A et 6001B.** Une carte 6001A ne reçoit que le
   firmware de ce projet (`esphome/hlk-ld6001a-xiao*.yaml`), une carte
   6001B que celui du projet HLK-LD6001B. Avant tout flash : MAC vérifiée
   par `esptool flash-id` ET fichier YAML du bon projet, confirmés par
   écrit avant d'écrire.
3. **Ne jamais supposer une topologie d'alimentation ou de câblage** non
   dite ; en cas de doute, demander. (Erreur du 2026-09-28 : les trois
   cartes supposées sur le même hub alors que l'utilisateur avait dit que
   la 02 était sur un autre port du PC.)
4. Outils USB/série (esptool, esphome, pyserial) : **PowerShell
   uniquement**.
5. Après une OTA : relire la date de compilation annoncée par la carte
   **après 90 s d'uptime** — un brownout pendant la première minute fait
   revenir le bootloader à l'image précédente sans erreur visible.
6. Radar muet après des redémarrages répétés de l'ESP32 (toutes les
   commandes sans réponse, 0 trame) : seule une coupure d'alimentation
   > 1 min le débloque — la demander à l'utilisateur, en désignant la
   carte par son nom et sa MAC.
7. **Pas de chiffrement** (API, OTA) — consigne du projet d'origine.
8. Git : vérifier `git rev-parse --show-toplevel` (= ce dossier ; le
   dossier parent `C:\ncs\projects` est un autre dépôt). Jamais de
   `secrets.yaml` ni d'image `.bin` dans un commit. `Links.txt` est un
   fichier de l'utilisateur : ne pas le committer sans demande.
9. Documents d'état (README, PROTOCOL, DEPLOYMENT, MAINTENANCE) : état
   actuel uniquement ; l'historique va dans `PLAN.md`.

## Ajouter une carte 6001A

Fichier par carte `esphome/hlk-ld6001a-xiao-NN.yaml` qui inclut
`hlk-ld6001a-xiao.yaml` sans le modifier (voir `hlk-ld6001a-xiao-02.yaml`)
et ne change que `name`, `friendly_name` et `wifi: ap: ssid`. Vérifier
l'identité des configurations résolues (`esphome config` des deux fichiers,
seuls ces noms doivent différer). Procédure complète : `DEPLOYMENT.md`.
