"""Journal passif des redemarrages des cartes radar (lecture seule).

usage: python surveillance_resets.py <duree_h> <sortie.csv> [ip ...]
  (par defaut : 192.168.1.90 6001A-01, 192.168.1.201 6001A-02,
   192.168.1.17 6001B)

Toutes les 60 s, lit l'uptime de chaque carte par HTTP
(/hlk_targets.json, la meme route que la page web sonde a 1 Hz) et ecrit
une ligne CSV. Un uptime qui diminue = redemarrage : sa date est calculee
(maintenant - uptime), puis la raison du redemarrage est lue une seule
fois par l'API ESPHome (capteur "Reset Reason", lecture seule, comme Home
Assistant). N'envoie aucune commande, ne touche ni a l'USB ni au port serie.

Regle du projet (CLAUDE.md) : ne lancer sur la carte 6001A-01
(28:84:85:8a:be:00) qu'avec l'accord explicite de l'utilisateur.
"""
import asyncio
import csv
import json
import sys
import time
import urllib.request

DEFAULT = {"192.168.1.90": "6001A-01", "192.168.1.201": "6001A-02", "192.168.1.17": "6001B"}


def http_uptime(ip):
    try:
        with urllib.request.urlopen(f"http://{ip}/hlk_targets.json", timeout=5) as r:
            d = json.load(r)
        return d.get("uptimeS"), d.get("frameCount"), d.get("recoveryCount")
    except Exception as e:  # noqa: BLE001
        return None, None, type(e).__name__


async def reset_reason(ip):
    from aioesphomeapi import APIClient
    cli = APIClient(ip, 6053, None)
    try:
        await cli.connect(login=True)
        entities, _ = await cli.list_entities_services()
        key = next((e.key for e in entities if e.name == "Reset Reason"), None)
        states = {}
        cli.subscribe_states(lambda s: states.__setitem__(s.key, s))
        await asyncio.sleep(3)
        st = states.get(key)
        return getattr(st, "state", None)
    except Exception as e:  # noqa: BLE001
        return f"lecture impossible ({type(e).__name__})"
    finally:
        try:
            await cli.disconnect()
        except Exception:  # noqa: BLE001
            pass


def main():
    hours, out = float(sys.argv[1]), sys.argv[2]
    ips = sys.argv[3:] or list(DEFAULT)
    last = {ip: None for ip in ips}
    end = time.time() + hours * 3600
    with open(out, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["heure", "carte", "ip", "uptime_s", "trames", "recuperations", "evenement"])
        while time.time() < end:
            now = time.time()
            for ip in ips:
                up, frames, rec = http_uptime(ip)
                event = ""
                if up is None:
                    event = f"injoignable ({rec})"
                elif last[ip] is not None and up < last[ip]:
                    when = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now - up))
                    event = f"REDEMARRAGE vers {when} : {asyncio.run(reset_reason(ip))}"
                if up is not None:
                    last[ip] = up
                w.writerow([time.strftime("%Y-%m-%d %H:%M:%S"), DEFAULT.get(ip, ip), ip, up, frames,
                            rec if up is not None else "", event])
                f.flush()
            time.sleep(max(0.0, 60 - (time.time() - now)))


if __name__ == "__main__":
    main()
