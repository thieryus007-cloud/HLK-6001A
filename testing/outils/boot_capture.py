"""Reset an ESP32-S3 (USB-Serial/JTAG) and capture its boot log over USB.

usage: boot_capture.py <COMx> <expected_mac> <trials> <seconds_per_trial> <out_prefix>

Per trial: hard reset through RTS (same sequence as esptool's HardReset for
USB devices), then reopen the port as soon as it re-enumerates and log every
line with the time since the reset. A port drop in the middle of a capture
means the chip reset again (e.g. brownout) -- it is marked and the port is
reopened, so the next boot is captured too. DTR/RTS are forced low before
each open so that opening the port does not itself reset the chip.
Run from PowerShell (USB/serial tool rule of this machine).
"""
import sys
import time

import serial

port, mac, trials, secs, prefix = sys.argv[1], sys.argv[2].lower(), int(sys.argv[3]), float(sys.argv[4]), sys.argv[5]


def open_port(deadline):
    while time.time() < deadline:
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 0.2
            s.dtr = False
            s.rts = False
            s.open()
            return s
        except (serial.SerialException, OSError):
            time.sleep(0.05)
    return None


for trial in range(1, trials + 1):
    out = f"{prefix}_{trial}.log"
    with open(out, "w", encoding="utf-8", errors="replace") as f:
        s = open_port(time.time() + 10)
        if s is None:
            print(f"essai {trial}: port introuvable")
            continue
        # Hard reset (EN low 200 ms) -- esptool HardReset, USB variant. Each
        # RTS change is followed by a dummy DTR write, as esptool's _setRTS
        # does: Windows' usbser.sys only sends SET_CONTROL_LINE_STATE (and so
        # the new RTS level) when DTR is written too.
        s.rts = True
        s.dtr = s.dtr
        time.sleep(0.2)
        s.rts = False
        s.dtr = s.dtr
        t0 = time.time()
        try:
            s.close()
        except Exception:  # noqa: BLE001
            pass
        f.write(f"{0.0:7.2f} === RESET (RTS)\n")
        drops = 0
        buf = b""
        s = None
        while time.time() - t0 < secs:
            if s is None:
                s = open_port(t0 + secs)
                if s is None:
                    break
                f.write(f"{time.time() - t0:7.2f} === PORT OPEN\n")
            try:
                chunk = s.read(4096)
            except (serial.SerialException, OSError):
                drops += 1
                f.write(f"{time.time() - t0:7.2f} === PORT DROPPED (chip reset?)\n")
                try:
                    s.close()
                except Exception:  # noqa: BLE001
                    pass
                s = None
                continue
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    f.write(f"{time.time() - t0:7.2f} {line.decode('utf-8', 'replace').rstrip()}\n")
                f.flush()
        if s is not None:
            try:
                s.close()
            except Exception:  # noqa: BLE001
                pass
    text = open(out, encoding="utf-8", errors="replace").read()
    bod = ("Brownout" in text) or ("BROWN" in text.upper() and "rst:" in text)
    print(f"essai {trial}: coupures port={drops} brownout_vu={bod} -> {out}")
    time.sleep(3)
