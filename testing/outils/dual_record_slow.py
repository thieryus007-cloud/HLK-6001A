"""Poll both radars' /hlk_targets.json at the same instant, every N seconds.

usage: dual_record_slow.py <seconds> <out.jsonl> <interval_s>
Light enough to run for an hour next to the web page's own 1 Hz polling.
"""
import json
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor

HOSTS = {"B": "http://192.168.1.17/hlk_targets.json", "A": "http://192.168.1.90/hlk_targets.json"}


def fetch(url):
    try:
        with urllib.request.urlopen(url, timeout=4) as r:
            d = json.load(r)
        d.pop("points", None)
        return d
    except Exception as e:  # noqa: BLE001
        return {"error": f"{type(e).__name__}: {e}"}


seconds, out, interval = float(sys.argv[1]), sys.argv[2], float(sys.argv[3])
with ThreadPoolExecutor(2) as pool, open(out, "w") as f:
    t0 = time.time()
    while time.time() - t0 < seconds:
        tick = time.time()
        futs = {k: pool.submit(fetch, u) for k, u in HOSTS.items()}
        rec = {"t": round(tick - t0, 1), "wall": time.strftime("%H:%M:%S"), **{k: fu.result() for k, fu in futs.items()}}
        f.write(json.dumps(rec) + "\n")
        f.flush()
        time.sleep(max(0.0, interval - (time.time() - tick)))
