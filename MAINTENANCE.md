# Maintenance

Reprend telles quelles les règles opérationnelles déjà validées sur le
HLK-LD6001B (même carte XIAO ESP32-S3, même architecture de composant) —
voir `HLK-LD6001B/XIAO-ESP32S3/MAINTENANCE.md` pour le détail de chaque
incident d'origine. Ce document ne liste que ce qui est identique par
construction (même code de base) ou spécifique au 6001A.

## Règle opérationnelle absolue (identique au 6001B)

**Jamais deux maîtres UART actifs simultanément sur les broches TX/RX du
radar.** Voir `HLK-LD6001B/XIAO-ESP32S3/MAINTENANCE.md`, même risque,
même cause, même remède (débrancher l'un des deux avant tout test direct
hors ESPHome).

## Comportement de récupération automatique

Identique au 6001B : `WATCHDOG_TIMEOUT_MS` (90s) sans trame valide
(normale OU détaillée), `WATCHDOG_COOLDOWN_MS` (30s) entre deux
déclenchements, séquence complète `AT+RESET` → réglages → `AT+DEBUG=3` →
`AT+START` (voir PROTOCOL.md). Sensiblement plus de marge que nécessaire
même à la cadence ≤30ms annoncée par le datasheet MS72SF1 pour ce
chipset (voir PLAN.md, tableau des différences) — 90s reste une marge
énorme face à un flux normalement continu, aucune raison de la resserrer
pour l'instant.

## Etat radar dégradé par rapport au 6001B (limitation connue, pas un bug)

Le bandeau "Etat radar" du 6001B compare les réglages commandés à ceux
rapportés par la trame heartbeat en continu. **Ce composant 6001A n'a pas
cette comparaison automatique** : le format de la trame heartbeat n'est
pas confirmé (voir PROTOCOL.md), donc le bandeau affiche la réponse brute
et non analysée du dernier `AT+READ` réussi, sans comparaison
champ-par-champ. Une réponse `AT+READ` vieille de plus de ~60s (deux
cycles de sondage à 15s manqués) doit être traitée comme "radar
probablement injoignable", pas comme un vrai mismatch de réglage — voir
Phase 1 de PLAN.md pour la levée de cette limitation une fois le format
réel de `AT+READ` (ou, alternativement, celui d'une vraie trame
heartbeat) capturé sur matériel réel.

## Persistance des réglages en cas de coupure d'alimentation

Identique au 6001B : ce système ne dépend pas d'une éventuelle
persistance flash côté radar (non documentée pour ce modèle non plus).
L'ESP32 sauvegarde ses propres réglages en NVS et rejoue la séquence
complète à chaque démarrage.

## Brownout au démarrage WiFi

Même risque que le 6001B (même carte, même séquence de démarrage
radar+WiFi qui peut cumuler deux pics de courant) — voir
`HLK-LD6001B/XIAO-ESP32S3/MAINTENANCE.md` pour le diagnostic complet et
le correctif (attente WiFi confirmée avant `AT+RESET`), déjà repris tel
quel dans ce portage (voir PROTOCOL.md, "Séquence de démarrage").

## Débit UART : symptôme d'un mauvais réglage

Voir PROTOCOL.md, "Débit UART" — le manuel se contredit sur la valeur par
défaut (115200 vs 921600). Un mauvais débit ressemble à un module mort
(aucune trame, aucun `AT+OK`) : avant de suspecter le câblage ou le
matériel, réessayer avec l'autre débit dans le YAML.

## Dépannage — identique au 6001B sauf mention contraire

Toutes les entrées de dépannage générique du 6001B s'appliquent tel quel
à cette base de code partagée (voir
`HLK-LD6001B/XIAO-ESP32S3/MAINTENANCE.md`) :
- Page web inaccessible / épuisement du pool de sockets LWIP.
- Vérifier qu'une commande AT+ atteint réellement le fil
  (`uart: debug: direction: BOTH`).
- `esphome compile`/`upload` "réussi" sans firmware produit (symptôme
  Git-Bash/MSYS — voir DEPLOYMENT.md).
- Flash OTA "successful" sans changement de comportement (rollback ou
  contenu web statique périmé).
- Connexion résiduelle après un `curl`/navigateur interrompu.
- Console AT `/at_command` répond "busy" (409).
- Commande Console AT en timeout systématique : vérifier qu'elle existe
  réellement dans la table AT+ (PROTOCOL.md) avant de suspecter un bug de
  la file de commandes — tester avec `AT+READ`, qui répond toujours
  rapidement si le mécanisme lui-même fonctionne (contrairement à
  `AT+SEEKING` utilisé comme exemple sur le 6001B : cette commande
  **n'existe pas** pour le 6001A, voir PLAN.md "Piège identifié").
- Une seule cible remonte dans HA malgré plusieurs personnes détectées :
  vérifier `internal:` dans le YAML, comme sur le 6001B.
