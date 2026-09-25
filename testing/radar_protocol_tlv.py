"""Reference decoder for the HLK-LD6001A TLV stream, BOTH framings -- the
single reference for hlk_ld6001a.cpp's loop()/process_tlv_frame_() (same
decision rules, ported 1:1). See ../PROTOCOL.md for the field reference.

Two framings share the same 01..08 header and the same TLV body:

- AT+DEBUG=2 ("debug mode, used by the host computer", manual Hi-Link §6),
  observed on the real module 2026-09-25 (349/349 frames of
  fixtures/debug2_rx_2026-09-25.bin): LENGTH is the EXACT total frame size
  and there is NO trailing checksum byte -- the next frame's header starts
  right at offset LENGTH. The point cloud is present (POINTLEN = 0, 25, 50,
  75, 100 observed -- always a multiple of 25).
- AT+DEBUG=3 ("detailed protocol", manual §7.2): per the manual's own worked
  example (see radar_protocol_debug3.py / test_protocol_debug3.py), LENGTH
  does NOT count one trailing CHECK byte = XOR(FRAME field + person records).
  The manual says POINTLEN is "always 0" in this mode.

Frame body (little-endian):
  HEAD 8 | LENGTH 4 | FRAME 4 | TLV1=1 4 | POINTLEN 4 | <points> | TLV2=2 4 |
  TRACKLEN 4 | <persons, 32 bytes each: Q, ID, X, Y, Z, Vx, Vy, Vz>

Structural validation (both framings): TLV1 == 1, TLV2 == 2 and
LENGTH == 24 + POINTLEN + 8 + TRACKLEN exactly (true for every real frame
captured) -- this replaces the missing checksum in DEBUG=2.

Point record (25 bytes) -- HYPOTHESIS carried over from the HLK-LD6001B
(reverse-engineered there against ground truth, see
HLK-LD6001B/POINT_CLOUD_INVESTIGATION.md), NOT yet validated against ground
truth on the 6001A: X,Y,Z float32 @0/4/8, signed tag int8 @12, D,E,F float32
@13/17/21. On the 6001A capture: X/Y/Z plausible (0-4.4 m), D 2.0-13.3
(same range as the vendor colour legend), but E only ever 255.0 or 7.0
(was always < 2 on the 6001B) -- E/F semantics clearly differ, unused.
"""
import struct
from dataclasses import dataclass, field

HEAD = b"\x01\x02\x03\x04\x05\x06\x07\x08"
MAX_TLV_FRAME_LEN = 4096
POINT_RECORD_LEN = 25
PERSON_RECORD_LEN = 32

FRAMING_UNKNOWN = "unknown"
FRAMING_NO_CHECK = "no_check"      # AT+DEBUG=2 (observed)
FRAMING_WITH_CHECK = "with_check"  # AT+DEBUG=3 (manual example)


@dataclass
class TlvPoint:
    x: float
    y: float
    z: float
    tag: int
    d: float
    e: float
    f: float


@dataclass
class TlvPerson:
    q: int
    id: int
    x: float
    y: float
    z: float
    vx: float
    vy: float
    vz: float


@dataclass
class TlvFrame:
    frame_no: int
    point_len: int
    track_len: int
    checksum_validated: bool
    points: list = field(default_factory=list)
    people: list = field(default_factory=list)


def structure(buf: bytes, length: int):
    """Returns (point_len, track_len, people_start) if the body at buf[0:length]
    is structurally valid, else None. Caller guarantees len(buf) >= length >= 32."""
    tlv1, point_len = struct.unpack_from("<II", buf, 16)
    if tlv1 != 1 or point_len > length - 32:
        return None
    tlv2_off = 24 + point_len
    tlv2, track_len = struct.unpack_from("<II", buf, tlv2_off)
    people_start = tlv2_off + 8
    if tlv2 != 2 or track_len != length - people_start:
        return None
    return point_len, track_len, people_start


def checksum(buf: bytes, people_start: int, track_len: int) -> int:
    c = 0
    for b in buf[12:16]:
        c ^= b
    for b in buf[people_start:people_start + track_len]:
        c ^= b
    return c


def decide(buf: bytes, length: int, xor: int, sticky: str):
    """Framing decision for a structurally valid frame at buf[0]. Returns one of
    ("accept", checksum_validated, consumed, new_sticky), ("wait",), ("reject",).
    Mirrors HlkLd6001aComponent::decide_tlv_framing_() in hlk_ld6001a.cpp."""
    n = len(buf)
    head_at_len = n >= length + 8 and buf[length:length + 8] == HEAD
    head_at_len1 = n >= length + 9 and buf[length + 1:length + 9] == HEAD
    have_check = n >= length + 1
    check_ok = have_check and buf[length] == xor
    if n >= length + 9:
        if head_at_len1:
            # exactly one byte sits between this frame and the next header:
            # it can only be the DEBUG=3 check byte -- must match.
            return ("accept", True, length + 1, FRAMING_WITH_CHECK) if check_ok else ("reject",)
        if head_at_len:
            return ("accept", False, length, FRAMING_NO_CHECK)
        # followed by something else (AT reply text, idle garbage)
        if sticky == FRAMING_WITH_CHECK:
            return ("accept", True, length + 1, sticky) if check_ok else ("reject",)
        return ("accept", False, length, sticky)
    if sticky == FRAMING_NO_CHECK:
        return ("accept", False, length, sticky)
    if sticky == FRAMING_WITH_CHECK:
        if not have_check:
            return ("wait",)
        if check_ok:
            return ("accept", True, length + 1, sticky)
        return ("wait",)  # maybe a DEBUG=3 -> DEBUG=2 switch: wait for a full decision
    return ("wait",)


def decode_body(buf: bytes, point_len: int, track_len: int, people_start: int):
    points = []
    for i in range(point_len // POINT_RECORD_LEN):
        off = 24 + i * POINT_RECORD_LEN
        x, y, z = struct.unpack_from("<fff", buf, off)
        tag = struct.unpack_from("<b", buf, off + 12)[0]
        d, e, f = struct.unpack_from("<fff", buf, off + 13)
        points.append(TlvPoint(x, y, z, tag, d, e, f))
    people = []
    for i in range(track_len // PERSON_RECORD_LEN):
        off = people_start + i * PERSON_RECORD_LEN
        q, pid = struct.unpack_from("<II", buf, off)
        x, y, z, vx, vy, vz = struct.unpack_from("<ffffff", buf, off + 8)
        people.append(TlvPerson(q, pid, x, y, z, vx, vy, vz))
    return points, people


class TlvReassembler:
    """Feed raw bytes; get TlvFrame objects back. Same resync behaviour as the
    C++ loop(): search the header, validate structure, decide framing, consume."""

    def __init__(self):
        self._buf = bytearray()
        self.sticky = FRAMING_UNKNOWN
        self.rejected = 0
        self.structure_errors = 0

    def feed(self, chunk: bytes):
        self._buf.extend(chunk)
        out = []
        while True:
            idx = self._buf.find(HEAD)
            if idx < 0:
                if len(self._buf) > 7:
                    del self._buf[:-7]
                break
            if idx > 0:
                del self._buf[:idx]
            if len(self._buf) < 32:
                break
            length = struct.unpack_from("<I", self._buf, 8)[0]
            if length < 32 or length > MAX_TLV_FRAME_LEN:
                self.structure_errors += 1
                del self._buf[:1]
                continue
            if len(self._buf) < length:
                break
            st = structure(bytes(self._buf[:length]), length)
            if st is None:
                self.structure_errors += 1
                del self._buf[:1]
                continue
            point_len, track_len, people_start = st
            xor = checksum(self._buf, people_start, track_len)
            verdict = decide(bytes(self._buf), length, xor, self.sticky)
            if verdict[0] == "wait":
                break
            if verdict[0] == "reject":
                self.rejected += 1
                del self._buf[:1]
                continue
            _, validated, consumed, self.sticky = verdict
            body = bytes(self._buf[:length])
            frame_no = struct.unpack_from("<I", body, 12)[0]
            points, people = decode_body(body, point_len, track_len, people_start)
            out.append(TlvFrame(frame_no, point_len, track_len, validated, points, people))
            del self._buf[:consumed]
        return out
