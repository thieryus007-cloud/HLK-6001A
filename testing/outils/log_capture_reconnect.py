"""API log capture that survives reboots: reconnects after every drop.

usage: log_capture_reconnect.py <host> <seconds> <out.log>
Each line: seconds since capture start, then the device log line. Session
boundaries are marked, so the last lines before a reset are visible.
"""
import asyncio
import sys
import time

from aioesphomeapi import APIClient, LogLevel

host, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
t0 = time.time()


async def main():
    with open(out, "w", encoding="utf-8", errors="replace") as f:
        def w(text):
            f.write(f"{time.time() - t0:8.2f} {text}\n")
            f.flush()

        while time.time() - t0 < seconds:
            stopped = asyncio.Event()
            cli = APIClient(host, 6053, None)

            async def on_stop(expected_disconnect: bool) -> None:
                stopped.set()

            try:
                await cli.connect(login=True, on_stop=on_stop)
            except Exception as e:  # noqa: BLE001 -- device rebooting: retry
                await asyncio.sleep(1)
                continue
            try:
                di = await cli.device_info()
                w(f"=== CONNECTED build={di.compilation_time}")
            except Exception:  # noqa: BLE001
                w("=== CONNECTED (device_info failed)")
            cli.subscribe_logs(lambda m: w(m.message.decode("utf-8", errors="replace")),
                               log_level=LogLevel.LOG_LEVEL_VERBOSE)
            remaining = seconds - (time.time() - t0)
            try:
                await asyncio.wait_for(stopped.wait(), timeout=max(0.0, remaining))
                w("=== DISCONNECTED")
            except asyncio.TimeoutError:
                pass
            try:
                await cli.disconnect()
            except Exception:  # noqa: BLE001
                pass


asyncio.run(main())
