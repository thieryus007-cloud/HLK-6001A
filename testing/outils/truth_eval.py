"""Score both radars against ground truth (1 person) on the dual recordings."""
import json, math, sys

def debounce(series, hold=60.0):
    pub = pend = None; since = 0.0; out = []
    for t, c in series:
        if pub is None: pub = pend = c; since = t
        if c > pub:
            if c != pend: pend = c; since = t
            if t - since >= hold: pub = c
        else:
            pub = pend = c; since = t
        out.append(pub)
    return out

def count(targets, room):
    n = 0
    for d in targets:
        if math.sqrt(d["x"]**2 + d["y"]**2 + d["z"]**2) < 0.30: continue
        if room and (abs(d["x"]) > 2.0 or abs(d["y"]) > 2.0): continue
        n += 1
    return n

for f in sys.argv[1:]:
    recs = [json.loads(l) for l in open(f)]
    n = len(recs)
    print(f"== {f} ({n} s, truth = 1 person)")
    for room in (False, True):
        sB = [(r["t"], count(r["B"]["targets"], room)) for r in recs]   # 55AA list = B's HA source
        sA = [(r["t"], count(r["A"]["targets"], room)) for r in recs]
        vals = {"B raw": [c for _, c in sB], "B HA (debounce, firmware actuel)": debounce(sB),
                "A HA (brut, firmware actuel)": [c for _, c in sA], "A avec debounce": debounce(sA)}
        tag = "pièce ±2 m" if room else "sans filtre"
        for k, v in vals.items():
            ok = sum(1 for c in v if c == 1)
            print(f"  [{tag}] {k:34s}: =1 {100*ok/n:5.1f}%   >1 {100*sum(c>1 for c in v)/n:5.1f}%   0 {100*sum(c==0 for c in v)/n:5.1f}%")
        ag = sum(a == b for a, b in zip(debounce(sB), [c for _, c in sA]))
        print(f"  [{tag}] accord HA B(debounce) / A(brut): {100*ag/n:.1f}%")
