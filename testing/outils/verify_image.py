"""Compare a full-flash dump against the ESPHome build that is supposed to be in it.

usage: verify_image.py <dump.bin> <firmware.factory.bin> <firmware.ota.bin>
"""
import hashlib
import struct
import sys
import zlib

dump_path, factory_path, ota_path = sys.argv[1:4]
dump = open(dump_path, "rb").read()
factory = open(factory_path, "rb").read()
ota = open(ota_path, "rb").read()

print(f"dump size      : {len(dump)} ({'OK 16MB' if len(dump) == 16 * 1024 * 1024 else 'UNEXPECTED'})")
print(f"dump sha256    : {hashlib.sha256(dump).hexdigest()}")

# Partition table at 0x8000: 32-byte entries, magic 0x50AA
parts = {}
for i in range(0, 0xC00, 32):
    e = dump[0x8000 + i:0x8000 + i + 32]
    if e[:2] != b"\xaa\x50":
        break
    ptype, subtype, off, size = e[2], e[3], *struct.unpack_from("<II", e, 4)
    name = e[12:28].split(b"\0")[0].decode()
    parts[name] = (ptype, subtype, off, size)
    print(f"  partition {name:10s} type={ptype} sub={subtype:#04x} off={off:#08x} size={size:#08x}")

# otadata: two 32-byte esp_ota_select_entry_t (seq u32, label[20], state u32, crc u32)
ota_off = parts["otadata"][2]
active = "app0 (factory flash, otadata empty)"
best = None
for k in range(2):
    sec = dump[ota_off + k * 0x1000: ota_off + k * 0x1000 + 32]
    seq = struct.unpack_from("<I", sec, 0)[0]
    crc = struct.unpack_from("<I", sec, 28)[0]
    valid = seq != 0xFFFFFFFF and crc == zlib.crc32(sec[0:4], 0xFFFFFFFF)
    print(f"  otadata[{k}] seq={seq:#x} crc={crc:#x} valid={valid}")
    if valid and (best is None or seq > best):
        best = seq
if best is not None:
    app_count = sum(1 for p in parts.values() if p[0] == 0 and 0x10 <= p[1] <= 0x1F)
    idx = (best - 1) % app_count
    active = f"ota_{idx} (seq {best})"
print(f"active app     : {active}")

app_parts = [n for n, p in parts.items() if p[0] == 0]
for n in app_parts:
    off = parts[n][2]
    same = dump[off:off + len(ota)] == ota
    print(f"  {n}: firmware.ota.bin ({len(ota)} B) identical at {off:#x} -> {same}")

# Region-by-region compare of the factory image range
diff_sectors = [s for s in range(0, len(factory), 0x1000) if dump[s:s + len(factory[s:s + 0x1000])] != factory[s:s + 0x1000]]
print(f"factory range  : 0x0..{len(factory):#x}, differing 4K sectors: {[hex(s) for s in diff_sectors] or 'none'}")
