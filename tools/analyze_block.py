#!/usr/bin/env python3
"""Print a compact tag summary for a MinLZ block."""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path


def read_uvarint(buf: bytes, pos: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        if pos >= len(buf):
            raise ValueError("truncated varint")
        c = buf[pos]
        pos += 1
        value |= (c & 0x7F) << shift
        if c < 0x80:
            return value, pos
        shift += 7


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("block", nargs="?")
    ap.add_argument("--make-small", metavar=("OUT", "SIZE"), nargs=2, help="write minlz_perf small latency corpus")
    ap.add_argument("--make-logjson", metavar=("OUT", "SIZE"), nargs=2, help="write minlz_perf log/json corpus")
    args = ap.parse_args()

    if args.make_small:
        out, size_s = args.make_small
        size = int(size_s, 0)
        pat = b"MinLZ-small-latency-pattern:"
        data = bytes(((pat[i % len(pat)] + (i >> 5)) & 0xFF) for i in range(size))
        Path(out).write_bytes(data)
        print(f"wrote {out} bytes={size}")
        return 0

    if args.make_logjson:
        out, size_s = args.make_logjson
        size = int(size_s, 0)
        pos = 0
        line = 0
        buf = bytearray()
        while pos < size:
            s = (
                '{"ts":"2026-05-07T12:%02u:%02uZ","level":"info",'
                '"svc":"minlz","msg":"block processed",'
                '"block":%u,"bytes":%u,"crc":"%08x"}\n'
                % (line % 60, (line // 60) % 60, line, 4096 + (line & 8191), (line * 2654435761) & 0xFFFFFFFF)
            )
            chunk = s.encode("ascii")[: size - pos]
            buf.extend(chunk)
            pos += len(chunk)
            line += 1
        Path(out).write_bytes(bytes(buf))
        print(f"wrote {out} bytes={size}")
        return 0

    if not args.block:
        ap.error("block path is required unless a --make-* option is used")

    data = Path(args.block).read_bytes()
    if not data or data[0] != 0:
        raise SystemExit("not a MinLZ block")
    if len(data) == 1:
        print("empty block")
        return 0

    declared, pos = read_uvarint(data, 1)
    payload = data[pos:]
    counts = {0: 0, 1: 0, 2: 0, 3: 0}
    literals: list[int] = []
    copies: list[tuple[str, int, int]] = []
    repeats = 0
    fused = 0
    copy_kinds: Counter[str] = Counter()
    copy_lengths: Counter[int] = Counter()
    copy_offsets: Counter[int] = Counter()
    literal_length_counts: Counter[int] = Counter()
    i = 0
    decoded = 0
    offset = 1

    while i < len(payload):
        tag = payload[i] & 3
        counts[tag] += 1
        if tag == 0:
            v = payload[i]
            x = v >> 3
            i += 1
            if x < 29:
                length = x + 1
            elif x == 29:
                length = 30 + payload[i]
                i += 1
            elif x == 30:
                length = 30 + int.from_bytes(payload[i : i + 2], "little")
                i += 2
            else:
                length = 30 + int.from_bytes(payload[i : i + 3], "little")
                i += 3
            if v & 4:
                repeats += 1
                copies.append(("repeat", length, offset))
                copy_kinds["repeat"] += 1
                copy_lengths[length] += 1
                copy_offsets[offset] += 1
                decoded += length
            else:
                literals.append(length)
                literal_length_counts[length] += 1
                i += length
                decoded += length
        elif tag == 1:
            v = int.from_bytes(payload[i : i + 2], "little")
            len_code = (payload[i] >> 2) & 15
            offset = (v >> 6) + 1
            i += 2
            if len_code == 15:
                length = 18 + payload[i]
                i += 1
            else:
                length = 4 + len_code
            copies.append(("copy1", length, offset))
            copy_kinds["copy1"] += 1
            copy_lengths[length] += 1
            copy_offsets[offset] += 1
            decoded += length
        elif tag == 2:
            len_code = payload[i] >> 2
            offset = int.from_bytes(payload[i + 1 : i + 3], "little") + 64
            i += 3
            if len_code <= 60:
                length = 4 + len_code
            elif len_code == 61:
                length = 64 + payload[i]
                i += 1
            elif len_code == 62:
                length = 64 + int.from_bytes(payload[i : i + 2], "little")
                i += 2
            else:
                length = 64 + int.from_bytes(payload[i : i + 3], "little")
                i += 3
            copies.append(("copy2", length, offset))
            copy_kinds["copy2"] += 1
            copy_lengths[length] += 1
            copy_offsets[offset] += 1
            decoded += length
        else:
            val = int.from_bytes(payload[i : i + 4], "little")
            lit_len = (val >> 3) & 3
            if val & 4:
                len_code = (val >> 5) & 63
                offset = (val >> 11) + 65536
                i += 4
                if len_code < 61:
                    length = 4 + len_code
                elif len_code == 61:
                    length = 64 + payload[i]
                    i += 1
                elif len_code == 62:
                    length = 64 + int.from_bytes(payload[i : i + 2], "little")
                    i += 2
                else:
                    length = 64 + int.from_bytes(payload[i : i + 3], "little")
                    i += 3
                kind = "copy3"
            else:
                length = 4 + ((val >> 5) & 7)
                offset = ((val >> 8) & 0xFFFF) + 64
                i += 3
                lit_len += 1
                fused += 1
                kind = "fused2"
            if lit_len:
                literals.append(lit_len)
                literal_length_counts[lit_len] += 1
                i += lit_len
                decoded += lit_len
            copies.append((kind, length, offset))
            copy_kinds[kind] += 1
            copy_lengths[length] += 1
            copy_offsets[offset] += 1
            decoded += length

    print(f"bytes={len(data)} header={pos} payload={len(payload)} declared={declared} decoded={decoded}")
    print(f"tags={counts} literals={len(literals)} literal_bytes={sum(literals)} repeats={repeats} fused={fused}")
    print(f"literal_lengths={literals[:32]}")
    print(f"copies={copies[:64]}")
    print(f"copy_kinds={copy_kinds.most_common()}")
    print(f"top_copy_lengths={copy_lengths.most_common(24)}")
    print(f"top_copy_offsets={copy_offsets.most_common(24)}")
    print(f"top_literal_lengths={literal_length_counts.most_common(16)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
