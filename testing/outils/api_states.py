"""Lit une fois les etats des entites ESPHome via l'API native (lecture seule).
Usage: python api_states.py <host> [filtre_nom ...]"""
import asyncio
import sys
import time

from aioesphomeapi import APIClient


async def main(host, filters):
    cli = APIClient(host, 6053, None)
    await cli.connect(login=True)
    entities, _ = await cli.list_entities_services()
    names = {e.key: e.name for e in entities}
    states = {}
    unsub = cli.subscribe_states(lambda s: states.__setitem__(s.key, s))
    await asyncio.sleep(3)
    if callable(unsub):
        unsub()
    await cli.disconnect()
    print(f"now={time.time():.0f}")
    for key, st in states.items():
        name = names.get(key, str(key))
        if filters and not any(f.lower() in name.lower() for f in filters):
            continue
        val = getattr(st, "state", None)
        print(f"{name}: {val}")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], sys.argv[2:]))
