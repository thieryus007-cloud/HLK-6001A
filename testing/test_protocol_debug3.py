"""Validate radar_protocol_debug3.py against the exact worked example from
the HLK-LD6001A manual (Hi-Link V1.1, section 7.2.2) before trusting it on
real hardware -- same methodology as HLK-LD6001B/pc_test/test_protocol.py.

The manual's own prose rounds/truncates Y/Z/Vx/Vy/Vz to 2-3 decimals
(e.g. "0.31" for a true 0.3197) -- verified by hand (see PLAN.md, Journal
Phase 0) that the raw hex bytes decode to the *exact* float values used
here, not the manual's rounded prose. Also independently confirmed by
hand: LENGTH (64) excludes the trailing CHECK byte (frame is 65 bytes on
the wire), and CHECK = XOR(FRAME bytes + all person-record bytes) = 0xCC.
"""
from radar_protocol_debug3 import parse_debug3_frame, Debug3FrameError, Debug3Reassembler

# Hi-Link manual V1.1, section 7.2.2 -- one person, frame #419.
DEBUG3_EXAMPLE = bytes.fromhex(
    "01 02 03 04 05 06 07 08 40 00 00 00 A3 01 00 00 01 00 00 00 00 00 00 00 "
    "02 00 00 00 20 00 00 00 00 00 00 00 00 00 00 00 21 28 96 BF CB 85 20 40 "
    "9A AB A3 3E 8A BD C1 3D 50 98 99 BD 40 52 C3 3A CC".replace(" ", "")
)


def check(name, got, expected):
    status = "OK" if got == expected else "MISMATCH"
    print(f"  {name} = {got!r}  [{status}]")
    if status != "OK":
        raise SystemExit(f"{name}: expected {expected!r}, got {got!r}")


frame, consumed = parse_debug3_frame(DEBUG3_EXAMPLE)
print(f"[debug3] {frame}, consumed={consumed}")
check("consumed", consumed, 65)
check("frame_no", frame.frame_no, 419)
check("tlv1", frame.tlv1, 1)
check("point_len", frame.point_len, 0)
check("tlv2", frame.tlv2, 2)
check("track_len", frame.track_len, 32)
check("num_people", len(frame.people), 1)

p = frame.people[0]
check("q", p.q, 0)
check("id", p.id, 0)


def close(a, b, eps=1e-3):
    return abs(a - b) < eps


assert close(p.x, -1.173), p.x
assert close(p.y, 2.508), p.y
assert close(p.z, 0.320), p.z
assert close(p.vx, 0.095), p.vx
assert close(p.vy, -0.075), p.vy
assert close(p.vz, 0.0015), p.vz
print(f"  person0 = x={p.x:.3f} y={p.y:.3f} z={p.z:.3f} "
      f"vx={p.vx:.3f} vy={p.vy:.3f} vz={p.vz:.4f} "
      f"(manual prose, rounded: x=-1.17 y=2.50 z=0.31 vx=0.094 vy=-0.074 vz=0.001)")

# Checksum must actually be validated, not just present -- flip one byte
# and confirm rejection (the format's checksum was described as "confirmed
# and well-defined" in PLAN.md, so it MUST reject a corrupted frame).
corrupted = bytearray(DEBUG3_EXAMPLE)
corrupted[40] ^= 0xFF  # flip a byte inside person 0's X field
try:
    parse_debug3_frame(bytes(corrupted))
    raise SystemExit("expected Debug3FrameError on corrupted frame, got none")
except Debug3FrameError as e:
    print(f"  corrupted frame correctly rejected: {e}")

# Reassembler: feed the example one byte at a time (worst case for a real
# UART read loop) plus leading noise, confirm it still finds exactly one
# frame with the same field values.
reasm = Debug3Reassembler()
results = []
noisy = b"\x00\x01\xff" + DEBUG3_EXAMPLE + b"\x02\x03"
for i in range(0, len(noisy), 3):
    results.extend(reasm.feed(noisy[i:i + 3]))
check("reassembler_frame_count", len(results), 1)
check("reassembler_frame_no", results[0].frame_no, 419)

print("\nALL CHECKS PASSED")
