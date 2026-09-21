"""Decoder for the HLK-LD6001A's AT+DEBUG=3 ("demonstration mode") protocol
-- the ONLY source of target position/velocity/ID for this module (unlike
the HLK-LD6001B, whose AT+DEBUG=0 default protocol already carries X/Y/Z
and AT+DEBUG=2 is additive). See ../PLAN.md, "Conséquence architecturale
majeure", and XIAO-ESP32S3-equivalent PROTOCOL.md (this project) for the
full field reference.

Frame layout (all integers/floats little-endian), per the Hi-Link manual
V1.1 section 7.2.1, cross-checked against the worked example in 7.2.2:
  HEAD        8 bytes   fixed 01 02 03 04 05 06 07 08
  LENGTH      4 bytes   uint32, length of HEAD..end of person records --
                        does NOT include the trailing CHECK byte (verified
                        against the manual's own worked example below: a
                        65-byte frame carries LENGTH=64).
  FRAME       4 bytes   uint32, frame counter
  TLV1        4 bytes   uint32, expected 1 (point-cloud marker)
  POINTLEN    4 bytes   uint32, length in bytes of point-cloud data that
                        follows (skip this many bytes). The manual claims
                        this is "always 0" -- decoded dynamically anyway,
                        never assumed 0, per the HLK-LD6001B project's own
                        hard-won lesson (PLAN.md, "Ce qui est identique").
  <point-cloud data, POINTLEN bytes, not decoded here>
  TLV2        4 bytes   uint32, expected 2 (person/track marker)
  TRACKLEN    4 bytes   uint32, num_persons = TRACKLEN / 32
  <TRACKLEN bytes of person records, 32 bytes each>:
      Q       4 bytes   uint32, reserved
      ID      4 bytes   uint32, persistent target ID
      X       4 bytes   float32, metres (left/right)
      Y       4 bytes   float32, metres (front/back)
      Z       4 bytes   float32, metres (height)
      Vx      4 bytes   float32, m/s
      Vy      4 bytes   float32, m/s
      Vz      4 bytes   float32, m/s
  CHECK       1 byte    XOR of FRAME (4 bytes) + every byte of the person
                        records above (TRACKLEN bytes) -- NOT over
                        LENGTH/TLV1/POINTLEN/TLV2/TRACKLEN, and NOT over
                        any point-cloud bytes. Confirmed by hand against
                        the manual's worked example (see
                        test_protocol_debug3.py): 0xCC.
"""
import struct
from dataclasses import dataclass, field

HEAD = b"\x01\x02\x03\x04\x05\x06\x07\x08"


@dataclass
class Debug3Person:
    q: int
    id: int
    x: float
    y: float
    z: float
    vx: float
    vy: float
    vz: float


@dataclass
class Debug3Frame:
    frame_no: int
    tlv1: int
    point_len: int
    tlv2: int
    track_len: int
    people: list = field(default_factory=list)


class Debug3FrameError(ValueError):
    pass


def parse_debug3_frame(buf: bytes, start: int = 0):
    """Try to parse one Debug3Frame starting at buf[start:]. Returns
    (frame, consumed_bytes) on success -- consumed_bytes is LENGTH + 1
    (the trailing CHECK byte is not counted by LENGTH itself, see the
    module docstring). Raises Debug3FrameError if the header doesn't
    match, the buffer is too short, or the checksum doesn't match."""
    if buf[start:start + 8] != HEAD:
        raise Debug3FrameError("bad HEAD")
    if len(buf) - start < 32:
        raise Debug3FrameError("too short for even the fixed header")
    length = struct.unpack_from("<I", buf, start + 8)[0]
    if length < 32 or start + length + 1 > len(buf):
        raise Debug3FrameError(f"LENGTH field {length} implausible or exceeds buffer")
    frame_no = struct.unpack_from("<I", buf, start + 12)[0]
    tlv1 = struct.unpack_from("<I", buf, start + 16)[0]
    point_len = struct.unpack_from("<I", buf, start + 20)[0]
    tlv2_offset = start + 24 + point_len
    if tlv2_offset + 8 > start + length:
        raise Debug3FrameError("POINTLEN pushes TLV2 marker past frame end")
    tlv2 = struct.unpack_from("<I", buf, tlv2_offset)[0]
    track_len = struct.unpack_from("<I", buf, tlv2_offset + 4)[0]
    people_start = tlv2_offset + 8
    if people_start + track_len > start + length:
        raise Debug3FrameError("TRACKLEN exceeds frame end")

    check_offset = start + length  # 1 byte, just past the LENGTH-counted region
    expected_check = buf[check_offset]
    computed_check = 0
    for b in buf[start + 12:start + 16]:  # FRAME field
        computed_check ^= b
    for b in buf[people_start:people_start + track_len]:  # person records only
        computed_check ^= b
    if computed_check != expected_check:
        raise Debug3FrameError(
            f"checksum mismatch: computed 0x{computed_check:02X}, frame says 0x{expected_check:02X}")

    people = []
    for i in range(track_len // 32):
        off = people_start + i * 32
        q, pid = struct.unpack_from("<II", buf, off)
        x, y, z, vx, vy, vz = struct.unpack_from("<ffffff", buf, off + 8)
        people.append(Debug3Person(q, pid, x, y, z, vx, vy, vz))
    return Debug3Frame(frame_no, tlv1, point_len, tlv2, track_len, people), length + 1


class Debug3Reassembler:
    """Feed raw bytes in; get back parsed Debug3Frame objects as they
    complete. Mirrors HLK-LD6001B's pc_test/radar_protocol_debug2.py
    Debug2Reassembler interface, adapted for the checksum this format
    actually has (see parse_debug3_frame)."""

    def __init__(self):
        self._buf = bytearray()

    def feed(self, chunk: bytes):
        self._buf.extend(chunk)
        results = []
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
            try:
                frame, consumed = parse_debug3_frame(bytes(self._buf))
            except Debug3FrameError as e:
                msg = str(e)
                if ("too short" in msg or "implausible" in msg) and len(self._buf) < 200:
                    break  # wait for more bytes before giving up
                del self._buf[:1]  # resync one byte at a time (bad header/checksum)
                continue
            results.append(frame)
            del self._buf[:consumed]
        return results
