"""Liste ce que les appareils ESPHome annoncent en mDNS (_esphomelib._tcp) --
exactement ce que Home Assistant utilise pour ses invites de decouverte --
puis interroge l'API de chaque IP donnee (device_info). Lecture seule.
Usage: python esphome_discovery.py [ip ...]"""
import asyncio
import sys
import time

from zeroconf import ServiceBrowser, ServiceListener, Zeroconf


class Listener(ServiceListener):
    def __init__(self):
        self.found = {}

    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name, timeout=3000)
        if info:
            props = {k.decode(errors="replace"): (v.decode(errors="replace") if v else "")
                     for k, v in info.properties.items()}
            self.found[name] = (info.parsed_addresses(), props)

    def update_service(self, zc, type_, name):
        self.add_service(zc, type_, name)

    def remove_service(self, zc, type_, name):
        pass


def browse(seconds=8):
    zc = Zeroconf()
    listener = Listener()
    ServiceBrowser(zc, "_esphomelib._tcp.local.", listener)
    time.sleep(seconds)
    zc.close()
    return listener.found


async def device_info(ip):
    from aioesphomeapi import APIClient
    cli = APIClient(ip, 6053, None)
    try:
        await cli.connect(login=True)
        di = await cli.device_info()
        return {"name": di.name, "friendly_name": di.friendly_name, "mac": di.mac_address,
                "compilation_time": di.compilation_time, "esphome_version": di.esphome_version}
    except Exception as e:  # noqa: BLE001
        return {"error": f"{type(e).__name__}: {e}"}
    finally:
        await cli.disconnect()


if __name__ == "__main__":
    print("=== mDNS _esphomelib._tcp ===")
    for name, (addrs, props) in sorted(browse().items()):
        keep = {k: props.get(k) for k in ("friendly_name", "mac", "version", "board", "project_name") if k in props}
        print(f"{name}  {addrs}  {keep}")
    for ip in sys.argv[1:]:
        print(f"=== API {ip} ===", asyncio.run(device_info(ip)))
