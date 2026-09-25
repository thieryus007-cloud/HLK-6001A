"""Tests for radar_protocol_tlv.py -- run with: python test_protocol_tlv.py

1. Real AT+DEBUG=2 bytes captured from the module (fixtures/debug2_rx_2026-09-25.bin).
2. Synthetic AT+DEBUG=3 stream built from the Hi-Link manual's worked example
   (§7.2.2), checksum recomputed per frame.
3. Corrupted DEBUG=3 frame rejected, neighbours kept.
4. Frames interleaved with AT reply text.
5. Mode switches DEBUG=2 -> DEBUG=3 -> DEBUG=2 in one stream.
"""
import os
import struct
import sys

from radar_protocol_tlv import (FRAMING_NO_CHECK, FRAMING_WITH_CHECK, HEAD,
                                TlvReassembler, checksum, structure)

HERE = os.path.dirname(os.path.abspath(__file__))
failures = []


def check(name, cond, detail=""):
    print(f"  {name}: {'OK' if cond else 'FAIL'} {detail}")
    if not cond:
        failures.append(name)


# Manual §7.2.2 worked example (65 bytes: LENGTH=64 + CHECK 0xCC).
MANUAL = bytes.fromhex(
    "0102030405060708400000 00a3010000010000000000000002000000200000000000000000000000"
    "212896bfcb8520409aaba33e8abdc13d509899bd4052c33acc".replace(" ", ""))


def debug3_frame(frame_no: int) -> bytes:
    body = bytearray(MANUAL[:64])
    struct.pack_into("<I", body, 12, frame_no)
    st = structure(bytes(body), 64)
    return bytes(body) + bytes([checksum(body, st[2], st[1])])


def feed_in_chunks(r, data, sizes=(1, 7, 64, 3, 200, 13)):
    frames, pos, i = [], 0, 0
    while pos < len(data):
        n = sizes[i % len(sizes)]
        frames += r.feed(data[pos:pos + n])
        pos += n
        i += 1
    return frames


print("1. real DEBUG=2 capture")
raw = open(os.path.join(HERE, "fixtures", "debug2_rx_2026-09-25.bin"), "rb").read()
r = TlvReassembler()
frames = feed_in_chunks(r, raw)
check("frame_count", len(frames) == 349, f"({len(frames)})")
check("frame_no_contiguous", all(b.frame_no - a.frame_no == 1 for a, b in zip(frames, frames[1:])))
check("no_checksum_in_debug2", not any(f.checksum_validated for f in frames))
check("sticky_no_check", r.sticky == FRAMING_NO_CHECK, f"({r.sticky})")
check("no_rejects", r.rejected == 0 and r.structure_errors == 0, f"(rejected={r.rejected} struct={r.structure_errors})")
check("pointlen_multiple_of_25", all(f.point_len % 25 == 0 for f in frames))
check("pointlen_values", sorted({f.point_len for f in frames}) == [0, 25, 50, 75, 100])
first_pt = next(f.points[0] for f in frames if f.points)
check("first_point_decoded", abs(first_pt.x - 2.181) < 1e-3 and abs(first_pt.y + 1.520) < 1e-3
      and abs(first_pt.z - 3.290) < 1e-3 and abs(first_pt.d - 2.033) < 1e-3,
      f"(x={first_pt.x:.3f} y={first_pt.y:.3f} z={first_pt.z:.3f} d={first_pt.d:.3f})")

print("2. synthetic DEBUG=3 stream (manual example, checksum recomputed)")
check("manual_frame_self_consistent", debug3_frame(419) == MANUAL)
r = TlvReassembler()
frames = feed_in_chunks(r, debug3_frame(419) + debug3_frame(420) + debug3_frame(421))
check("frame_count", len(frames) == 3, f"({len(frames)})")
check("all_checksums_validated", all(f.checksum_validated for f in frames))
check("sticky_with_check", r.sticky == FRAMING_WITH_CHECK, f"({r.sticky})")
p = frames[0].people[0]
check("person_decoded", len(frames[0].people) == 1 and p.id == 0 and abs(p.x + 1.173) < 1e-3
      and abs(p.y - 2.508) < 1e-3, f"(x={p.x:.3f} y={p.y:.3f})")

print("3. corrupted DEBUG=3 frame")
bad = bytearray(debug3_frame(420))
bad[40] ^= 0x10  # inside the person record: structure intact, checksum broken
r = TlvReassembler()
frames = feed_in_chunks(r, debug3_frame(419) + bytes(bad) + debug3_frame(421))
check("corrupted_dropped", [f.frame_no for f in frames] == [419, 421], f"({[f.frame_no for f in frames]})")
check("reject_counted", r.rejected >= 1, f"({r.rejected})")

print("4. frames interleaved with AT reply text")
r = TlvReassembler()
frames = feed_in_chunks(r, debug3_frame(419) + b"AT+OK\r\n" + debug3_frame(420) + debug3_frame(421))
check("frame_count", [f.frame_no for f in frames] == [419, 420, 421], f"({[f.frame_no for f in frames]})")

print("5. mode switches DEBUG=2 -> DEBUG=3 -> DEBUG=2")
d2 = bytearray()
pos = 0
for _ in range(10):  # first 10 real DEBUG=2 frames
    length = struct.unpack_from("<I", raw, pos + 8)[0]
    d2 += raw[pos:pos + length]
    pos += length
stream = bytes(d2) + debug3_frame(1000) + debug3_frame(1001) + bytes(d2)
r = TlvReassembler()
frames = feed_in_chunks(r, stream)
check("frame_count", len(frames) == 22, f"({len(frames)})")
check("debug3_part_validated", [f.checksum_validated for f in frames[10:12]] == [True, True])
check("sticky_back_to_no_check", r.sticky == FRAMING_NO_CHECK, f"({r.sticky})")
check("no_rejects", r.rejected == 0, f"({r.rejected})")

print()
if failures:
    print("FAILED:", ", ".join(failures))
    sys.exit(1)
print("ALL CHECKS PASSED")
