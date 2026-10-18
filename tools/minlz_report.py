#!/usr/bin/env python3
# Copyright 2026 MinLZ C contributors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
"""Generate C-vs-Go MinLZ performance reports as CSV, Markdown, and HTML."""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import html
import io
import os
import shutil
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


LEVEL_LABELS = {
    "-1": "-xfast",
    "0": "-0",
    "1": "-1",
    "2": "-2",
    "3": "-3",
}

LEVEL_ARGS = {label: raw for raw, label in LEVEL_LABELS.items()}

OP_PAIRS = {
    "block-encode": "go-block-encode",
    "block-decode": "go-block-decode",
    "stream-encode": "go-stream-encode",
    "stream-decode": "go-stream-decode",
    "index-build": "go-index-build",
    "index-load": "go-index-load",
    "index-find": "go-index-find",
    "read-at-hot": "go-read-at-hot",
}

GO_HELPER_CACHE: dict[tuple[str, str, str], str] = {}

FOCUSED_CASES = [
    ("small-8k", "0"),
    ("small-8k", "-1"),
    ("twain-repeat-8m", "0"),
    ("twain-repeat-8m", "-1"),
    ("twain-repeat-8m", "1"),
    ("twain-repeat-8m", "2"),
    ("logjson-1m", "-1"),
    ("logjson-1m", "2"),
]

L2_ENCODE_CASES = [
    ("twain-expand-10k", "2"),
    ("twain-expand-1m", "2"),
    ("twain-repeat-8m", "2"),
    ("logjson-1m", "2"),
    ("random-1m", "2"),
    ("small-8k", "2"),
]

STRUCTURED_L2_CASES = [
    ("logjson-1m", "2"),
    ("logjson-1m", "-1"),
    ("logjson-1m", "3"),
    ("twain-expand-1m", "2"),
    ("random-1m", "2"),
]

L2_REPEAT_CASES = [
    ("twain-repeat-8m", "2"),
    ("twain-repeat-8m", "-1"),
    ("twain-repeat-8m", "1"),
    ("twain-repeat-8m", "3"),
]

LAST_L2_BLOCKER_CASES = [
    ("logjson-1m", "2"),
    ("logjson-1m", "3"),
    ("logjson-1m", "-1"),
    ("twain-expand-1m", "2"),
    ("twain-repeat-8m", "2"),
    ("random-1m", "2"),
]

FOCUSED_ROWS = {
    ("block-decode", "small-8k", "-0"),
    ("block-decode", "small-8k", "-xfast"),
    ("block-decode", "twain-repeat-8m", "-0"),
    ("block-decode", "twain-repeat-8m", "-xfast"),
    ("block-decode", "twain-repeat-8m", "-1"),
    ("block-encode", "twain-repeat-8m", "-2"),
    ("read-at-hot", "twain-repeat-8m", "-xfast"),
    ("read-at-hot", "twain-repeat-8m", "-1"),
    ("read-at-hot", "twain-repeat-8m", "-3"),
    ("block-encode", "logjson-1m", "-xfast"),
    ("block-encode", "logjson-1m", "-2"),
    ("block-decode", "logjson-1m", "-xfast"),
    ("block-decode", "logjson-1m", "-2"),
}

L2_ENCODE_ROWS = {
    ("block-encode", "twain-expand-10k", "-2"),
    ("block-encode", "twain-expand-1m", "-2"),
    ("block-encode", "twain-repeat-8m", "-2"),
    ("block-encode", "logjson-1m", "-2"),
    ("block-encode", "random-1m", "-2"),
    ("block-encode", "small-8k", "-2"),
    ("block-decode", "twain-expand-1m", "-2"),
    ("block-decode", "logjson-1m", "-2"),
}

# These encoder rows have produced false wins in single-pass full reports on
# shared Linux hosts. Confirm them even when the first speedup exceeds 1.03.
ALWAYS_CONFIRM_ROWS = {
    ("block-encode", "twain-expand-10k", "-0"),
    ("block-encode", "twain-expand-10k", "-1"),
    ("block-encode", "twain-expand-1m", "-2"),
    ("block-encode", "twain-repeat-8m", "-2"),
    ("block-encode", "logjson-1m", "-2"),
}

STRUCTURED_L2_ROWS = {
    ("block-encode", "logjson-1m", "-2"),
    ("block-decode", "logjson-1m", "-2"),
    ("stream-encode", "logjson-1m", "-2"),
    ("stream-decode", "logjson-1m", "-2"),
    ("read-at-hot", "logjson-1m", "-2"),
    ("block-encode", "logjson-1m", "-xfast"),
    ("block-decode", "logjson-1m", "-xfast"),
    ("block-encode", "logjson-1m", "-3"),
    ("block-decode", "logjson-1m", "-3"),
    ("block-encode", "twain-expand-1m", "-2"),
    ("block-decode", "twain-expand-1m", "-2"),
    ("block-encode", "random-1m", "-2"),
    ("block-decode", "random-1m", "-2"),
}

L2_REPEAT_ROWS = {
    ("block-encode", "twain-repeat-8m", "-2"),
    ("block-decode", "twain-repeat-8m", "-2"),
    ("block-encode", "twain-repeat-8m", "-xfast"),
    ("block-decode", "twain-repeat-8m", "-xfast"),
    ("block-encode", "twain-repeat-8m", "-1"),
    ("block-decode", "twain-repeat-8m", "-1"),
    ("block-encode", "twain-repeat-8m", "-3"),
    ("block-decode", "twain-repeat-8m", "-3"),
}

LAST_L2_BLOCKER_ROWS = {
    ("block-encode", "logjson-1m", "-2"),
    ("block-decode", "logjson-1m", "-2"),
    ("stream-encode", "logjson-1m", "-2"),
    ("stream-decode", "logjson-1m", "-2"),
    ("read-at-hot", "logjson-1m", "-2"),
    ("block-encode", "logjson-1m", "-3"),
    ("block-decode", "logjson-1m", "-3"),
    ("block-encode", "logjson-1m", "-xfast"),
    ("block-decode", "logjson-1m", "-xfast"),
    ("block-encode", "twain-expand-1m", "-2"),
    ("block-decode", "twain-expand-1m", "-2"),
    ("block-encode", "twain-repeat-8m", "-2"),
    ("block-decode", "twain-repeat-8m", "-2"),
    ("block-encode", "random-1m", "-2"),
    ("block-decode", "random-1m", "-2"),
}


def parse_level(value: str) -> str:
    try:
        level = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("level must be a C API enum value") from exc
    if level not in (-1, 0, 1, 2, 3):
        raise argparse.ArgumentTypeError("level must be -1, 0, 1, 2, or 3")
    return str(level)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-release", help="CMake build directory")
    parser.add_argument("--go-repo", default=os.environ.get("MINLZ_GO_REPO", ""), help="Go MinLZ checkout")
    parser.add_argument("--samples", type=int, default=3, help="samples per measurement")
    parser.add_argument(
        "--rounds",
        type=int,
        default=1,
        help="repeat minlz_perf and aggregate row speeds by median-of-medians",
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=0,
        help="loops per measurement; 0 uses minlz_perf auto-scaling",
    )
    parser.add_argument("--corpus", default="", help="optional corpus substring filter")
    parser.add_argument(
        "--ops", choices=("all", "block", "stream", "index"), default=None,
        help="limit measured operation family",
    )
    parser.add_argument(
        "--level", type=parse_level, default=None,
        help="C API enum: -1=xfast, 0=raw, 1=fastest, 2=balanced, 3=smallest",
    )
    parser.add_argument("--out-dir", default="docs/perf-reports", help="report output directory")
    parser.add_argument(
        "--gate-profile",
        choices=(
            "full",
            "latency",
            "focused",
            "l2-encode",
            "l2-encode-blockers",
            "structured-l2",
            "l2-repeat-blocker",
            "last-l2-blocker",
        ),
        default="full",
        help="rows checked by --fail-on-go-win; latency checks small-* corpora",
    )
    parser.add_argument(
        "--fail-on-go-win",
        action="store_true",
        help="exit non-zero when selected rows have C size > Go or C speed < Go",
    )
    parser.add_argument("--no-candidate-gate", action="store_true", help="omit candidate labels on stderr")
    parser.add_argument("--keep-going", action="store_true", help="write raw output even when minlz_perf fails")
    parser.add_argument(
        "--taskset",
        default="",
        help="optional Linux CPU set for minlz_perf; child Go helper inherits the same affinity",
    )
    parser.add_argument(
        "--alternate-go-order",
        action="store_true",
        help="run Go first on every second focused round to remove thermal order bias",
    )
    parser.add_argument(
        "--official-gate",
        action="store_true",
        help="confirm noisy speed rows with focused median-of-medians before failing",
    )
    parser.add_argument(
        "--confirm-threshold",
        type=float,
        default=1.03,
        help="official gate rows below this speedup are confirmed with focused rounds",
    )
    parser.add_argument(
        "--confirm-samples",
        type=int,
        default=7,
        help="samples per focused confirmation run",
    )
    parser.add_argument(
        "--confirm-rounds",
        type=int,
        default=3,
        help="focused confirmation rounds for official gate",
    )
    parser.add_argument(
        "--confirm-iterations",
        type=int,
        default=0,
        help=(
            "iterations per focused confirmation run; 0 uses minlz_perf auto scaling "
            "instead of inheriting the shorter full-matrix iteration count"
        ),
    )
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def perf_exe(root: Path, build_dir: str) -> Path:
    name = "minlz_perf.exe" if os.name == "nt" else "minlz_perf"
    candidates = [
        root / build_dir / name,
        root / build_dir / "Release" / name,
        root / build_dir / "RelWithDebInfo" / name,
    ]
    for exe in candidates:
        if exe.exists():
            return exe
    raise SystemExit(f"minlz_perf not found under: {root / build_dir}")


def build_go_helper(root: Path, go_repo: str, env: dict[str, str], build_dir: str) -> str:
    compat = root / "compat" / "minlz-go-compat" / "main.go"
    env["MINLZ_GO_COMPAT"] = str(compat)
    if not go_repo:
        return ""
    go_amd64 = env.get("GOAMD64", "")
    cache_key = (str(Path(go_repo).resolve()), build_dir, go_amd64)
    cached = GO_HELPER_CACHE.get(cache_key)
    if cached and Path(cached).exists():
        env["MINLZ_GO_COMPAT_BIN"] = cached
        return cached
    suffix = f"-{go_amd64}" if go_amd64 else ""
    out = root / build_dir / (
        f"minlz-go-compat{suffix}.exe" if os.name == "nt" else f"minlz-go-compat{suffix}"
    )
    module_lines = [line.split()[1].strip('"')
                    for line in (Path(go_repo) / "go.mod").read_text().splitlines()
                    if line.startswith("module ") or line.startswith("module\t")]
    if len(module_lines) != 1:
        raise SystemExit(f"Cannot read module path from {go_repo}/go.mod")
    selected = out.parent / "compat" / "main.go"
    selected.parent.mkdir(parents=True, exist_ok=True)
    selected.write_text(compat.read_text().replace("github.com/minio/minlz", module_lines[0]))
    env["MINLZ_GO_COMPAT"] = str(selected)
    cmd = ["go", "build", "-o", str(out), str(selected)]
    subprocess.run(cmd, cwd=go_repo, env=env, check=True)
    env["MINLZ_GO_COMPAT_BIN"] = str(out)
    GO_HELPER_CACHE[cache_key] = str(out)
    return str(out)


def _set_godebug(env: dict[str, str], entries: list[str]) -> None:
    replacements = {entry.split("=", 1)[0]: entry for entry in entries}
    existing = [
        part for part in env.get("GODEBUG", "").split(",")
        if part and part.split("=", 1)[0] not in replacements
    ]
    env["GODEBUG"] = ",".join(existing + entries)


def _match_go_cpu_to_forced_backend(env: dict[str, str]) -> None:
    forced = env.get("MINLZ_FORCE_CPU", "").strip().lower()
    if forced in {"portable", "sse42", "asm"}:
        # A GOAMD64=v3/v4 binary requires AVX; those features cannot be disabled
        # with GODEBUG. Build a v2 oracle for scalar/SSE4.2 comparisons.
        env["GOAMD64"] = "v2"
        _set_godebug(env, [
            "cpu.avx=off", "cpu.avx2=off", "cpu.avx512f=off", "cpu.avx512cd=off",
            "cpu.avx512bw=off", "cpu.avx512dq=off", "cpu.avx512vl=off",
        ])
        if sys.platform.startswith("linux"):
            # The library backend does not control glibc's IFUNC selection.
            # Preserve unrelated tunables and CPU masks while disabling AVX
            # in libc's copy/clear routines for the same no-AVX comparison.
            key = "glibc.cpu.hwcaps="
            features: list[str] = []
            tunables: list[str] = []
            disabled = {
                "AVX", "AVX2", "AVX512F", "AVX512CD", "AVX512BW", "AVX512DQ",
                "AVX512VL", "AVX_Fast_Unaligned_Load",
            }
            for part in env.get("GLIBC_TUNABLES", "").split(":"):
                if part.startswith(key):
                    features.extend(
                        feature for feature in part[len(key):].split(",")
                        if feature and feature.lstrip("+-") not in disabled
                    )
                elif part:
                    tunables.append(part)
            features.extend("-" + feature for feature in sorted(disabled))
            env["GLIBC_TUNABLES"] = ":".join(tunables + [key + ",".join(features)])


def cpu_gate_metadata() -> dict[str, str]:
    env = os.environ.copy()
    _match_go_cpu_to_forced_backend(env)
    return {
        "backend": env.get("MINLZ_FORCE_CPU") or "default",
        "go_amd64": env.get("GOAMD64") or "default",
        "go_debug": env.get("GODEBUG") or "default",
        "libc_tunables": env.get("GLIBC_TUNABLES") or "default",
    }


def build_gate_metadata(root: Path, build_dir: str) -> dict[str, str]:
    cache = root / build_dir / "CMakeCache.txt"
    keys = {
        "MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH": "experimental_l2_search",
        "MINLZ_ENABLE_HUGEPAGE_WORKSPACE": "workspace_hugepages",
        "MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER": "readseeker_buffer_transfer",
    }
    values = dict.fromkeys(keys.values(), "unknown")
    try:
        lines = cache.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        lines = []
    for line in lines:
        name, separator, raw = line.partition(":BOOL=")
        key = keys.get(name)
        if not separator or key is None:
            continue
        raw = raw.strip().upper()
        if raw in ("ON", "TRUE", "YES", "1"):
            values[key] = "ON"
        elif raw in ("OFF", "FALSE", "NO", "0", ""):
            values[key] = "OFF"
    return values


def _perf_command(
    args: argparse.Namespace,
    root: Path,
    corpus: str | None = None,
    level: str | None = None,
    go_first: bool = False,
) -> tuple[list[str], dict[str, str]]:
    env = os.environ.copy()
    _match_go_cpu_to_forced_backend(env)
    if args.go_repo:
        env["MINLZ_GO_REPO"] = args.go_repo
    env["PATH"] = "/usr/local/go/bin:/usr/local/bin:/usr/bin:/bin:" + env.get("PATH", "")
    build_go_helper(root, args.go_repo, env, args.build_dir)

    perf = str(perf_exe(root, args.build_dir))
    cmd = [
        perf,
        "--samples",
        str(args.samples),
        "--go-oracle",
        "--go-speed",
        "--strict-go-size",
    ]
    if args.ops:
        cmd.extend(["--ops", args.ops])
    elif args.gate_profile in ("l2-encode", "l2-encode-blockers"):
        cmd.extend(["--ops", "block"])
    elif args.gate_profile == "l2-repeat-blocker":
        cmd.extend(["--ops", "block"])
    if args.taskset:
        cmd = ["taskset", "-c", args.taskset] + cmd
    if args.iterations > 0:
        cmd.extend(["--iterations", str(args.iterations)])
    if go_first:
        cmd.append("--go-first")
    if not args.no_candidate_gate:
        cmd.append("--candidate-gate")
    corpus_filter = args.corpus if corpus is None else corpus
    level_filter = args.level if level is None else level
    if corpus_filter:
        cmd.extend(["--corpus", corpus_filter])
    elif args.gate_profile == "latency":
        cmd.extend(["--corpus", "small-"])
    if level_filter:
        cmd.extend(["--level", level_filter])

    return cmd, env


def _median(values: list[float]) -> float:
    return float(statistics.median(values))


@dataclass
class Confirmation:
    operation: str
    corpus: str
    level: str
    full_speedup: float | None
    confirm_speedup: float | None
    full_size_delta: int | None
    confirm_size_delta: int | None
    status: str
    reason: str


def _aggregate_round_rows(round_rows: list[list[dict[str, str]]]) -> str:
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = {}
    fieldnames: list[str] | None = None
    for rows in round_rows:
        for row in rows:
            if fieldnames is None:
                fieldnames = list(row.keys())
            grouped.setdefault((row["kind"], row["corpus"], row["level"]), []).append(row)
    if not fieldnames:
        return ""
    out = io.StringIO()
    writer = csv.DictWriter(out, fieldnames=fieldnames)
    writer.writeheader()
    for key in sorted(grouped):
        rows = grouped[key]
        merged = dict(rows[0])
        for column in ("mbps_min", "mbps_median", "mbps_max"):
            vals = [fnum(row, column) for row in rows]
            vals = [v for v in vals if v is not None]
            if not vals:
                continue
            if column == "mbps_min":
                merged[column] = f"{min(vals):.12g}"
            elif column == "mbps_max":
                merged[column] = f"{max(vals):.12g}"
            else:
                merged[column] = f"{_median(vals):.12g}"
        writer.writerow(merged)
    return out.getvalue()


def _run_command_rounds(
    cmd: list[str],
    env: dict[str, str],
    root: Path,
    rounds: int,
    keep_going: bool,
    alternate_go_order: bool = False,
) -> tuple[str, str]:
    rounds = max(1, rounds)
    if rounds == 1:
        proc = subprocess.run(cmd, cwd=root, env=env, text=True, capture_output=True)
        if proc.returncode != 0 and not keep_going:
            sys.stderr.write(proc.stderr)
            raise SystemExit(proc.returncode)
        return proc.stdout, proc.stderr

    round_rows: list[list[dict[str, str]]] = []
    stderr_parts: list[str] = []
    for round_no in range(1, rounds + 1):
        go_first = alternate_go_order and round_no % 2 == 0
        round_cmd = cmd + ["--go-first"] if go_first else cmd
        proc = subprocess.run(round_cmd, cwd=root, env=env, text=True, capture_output=True)
        order = "go-first" if go_first else "c-first"
        stderr_parts.append(f"# round {round_no}/{rounds} order={order}\n{proc.stderr}")
        if proc.returncode != 0 and not keep_going:
            sys.stderr.write(proc.stderr)
            raise SystemExit(proc.returncode)
        round_rows.append(parse_rows(proc.stdout))
    return _aggregate_round_rows(round_rows), "\n".join(stderr_parts)


def run_perf(args: argparse.Namespace, root: Path) -> tuple[str, str, list[str]]:
    profile_cases = FOCUSED_CASES
    profile_label = "--focused-blocking-profile"
    if args.gate_profile in ("l2-encode", "l2-encode-blockers"):
        profile_cases = L2_ENCODE_CASES
        profile_label = "--l2-encode-blockers-profile"
    elif args.gate_profile == "structured-l2":
        profile_cases = STRUCTURED_L2_CASES
        profile_label = "--structured-l2-profile"
    elif args.gate_profile == "l2-repeat-blocker":
        profile_cases = L2_REPEAT_CASES
        profile_label = "--l2-repeat-blocker-profile"
    elif args.gate_profile == "last-l2-blocker":
        profile_cases = LAST_L2_BLOCKER_CASES
        profile_label = "--last-l2-blocker-profile"
    if args.gate_profile in ("focused", "l2-encode", "l2-encode-blockers", "structured-l2", "l2-repeat-blocker", "last-l2-blocker") and not args.corpus and not args.level:
        round_rows: list[list[dict[str, str]]] = []
        stderr_parts: list[str] = []
        command_label: list[str] = []
        for round_no in range(1, max(1, args.rounds) + 1):
            rows: list[dict[str, str]] = []
            for corpus, level in profile_cases:
                cmd, env = _perf_command(
                    args, root, corpus, level,
                    args.alternate_go_order and (round_no % 2 == 0),
                )
                if not command_label:
                    command_label = [cmd[0], profile_label]
                proc = subprocess.run(cmd, cwd=root, env=env, text=True, capture_output=True)
                stderr_parts.append(
                    f"# focused round {round_no}/{max(1, args.rounds)} "
                    f"corpus={corpus} level={level}\n{proc.stderr}"
                )
                if proc.returncode != 0 and not args.keep_going:
                    sys.stderr.write(proc.stderr)
                    raise SystemExit(proc.returncode)
                rows.extend(parse_rows(proc.stdout))
            round_rows.append(rows)
        return _aggregate_round_rows(round_rows), "\n".join(stderr_parts), command_label

    cmd, env = _perf_command(args, root)
    raw_csv, raw_err = _run_command_rounds(
        cmd, env, root, args.rounds, args.keep_going, args.alternate_go_order
    )
    return raw_csv, raw_err, cmd


def parse_rows(raw_csv: str) -> list[dict[str, str]]:
    lines = [line for line in raw_csv.splitlines() if line and not line.startswith("#")]
    if not lines:
        return []
    reader = csv.DictReader(io.StringIO("\n".join(lines)))
    return [row for row in reader if row.get("kind")]


def fnum(row: dict[str, str] | None, key: str) -> float | None:
    if not row:
        return None
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return None


def inum(row: dict[str, str] | None, key: str) -> int | None:
    if not row:
        return None
    try:
        return int(row[key])
    except (KeyError, TypeError, ValueError):
        return None


def build_comparison(rows: list[dict[str, str]]) -> list[dict[str, object]]:
    by_key = {(r["kind"], r["corpus"], r["level"]): r for r in rows}
    comparison: list[dict[str, object]] = []
    for row in rows:
        kind = row["kind"]
        if kind not in OP_PAIRS:
            continue
        go = by_key.get((OP_PAIRS[kind], row["corpus"], row["level"]))
        c_mbps = fnum(row, "mbps_median")
        go_mbps = fnum(go, "mbps_median")
        c_bytes = inum(row, "encoded_bytes")
        go_bytes = inum(go, "encoded_bytes")
        speedup = None
        if c_mbps is not None and go_mbps and go_mbps > 0:
            speedup = c_mbps / go_mbps
        size_delta = None
        if c_bytes is not None and go_bytes is not None:
            size_delta = c_bytes - go_bytes
        winner = "n/a"
        if speedup is not None:
            winner = "C" if speedup >= 1.0 else "Go"
        comparison.append(
            {
                "operation": kind,
                "corpus": row["corpus"],
                "level": LEVEL_LABELS.get(row["level"], row["level"]),
                "raw_bytes": inum(row, "raw_bytes"),
                "c_bytes": c_bytes,
                "go_bytes": go_bytes,
                "size_delta": size_delta,
                "c_ratio": fnum(row, "ratio"),
                "go_ratio": fnum(go, "ratio"),
                "c_mbps": c_mbps,
                "go_mbps": go_mbps,
                "speedup": speedup,
                "winner": winner,
            }
        )
    comparison.sort(key=lambda r: (str(r["operation"]), str(r["corpus"]), str(r["level"])))
    return comparison


def fmt(value: object, precision: int = 2) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return f"{value:.{precision}f}"
    return str(value)


def write_csv(path: Path, comparison: list[dict[str, object]]) -> None:
    fields = [
        "operation",
        "corpus",
        "level",
        "raw_bytes",
        "c_bytes",
        "go_bytes",
        "size_delta",
        "c_ratio",
        "go_ratio",
        "c_mbps",
        "go_mbps",
        "speedup",
        "winner",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in comparison:
            writer.writerow(row)


def summary(comparison: list[dict[str, object]]) -> dict[str, int]:
    comparable = [r for r in comparison if r["speedup"] is not None]
    size_checked = [r for r in comparison if r["size_delta"] is not None]
    return {
        "rows": len(comparison),
        "speed_rows": len(comparable),
        "c_speed_wins": sum(1 for r in comparable if float(r["speedup"]) >= 1.0),
        "go_speed_wins": sum(1 for r in comparable if float(r["speedup"]) < 1.0),
        "size_rows": len(size_checked),
        "c_size_ok": sum(1 for r in size_checked if int(r["size_delta"]) <= 0),
        "c_size_worse": sum(1 for r in size_checked if int(r["size_delta"]) > 0),
    }


def gate_coverage_failures(stats: dict[str, int], full_unfiltered: bool) -> list[str]:
    if stats["speed_rows"] == 0 or stats["size_rows"] == 0:
        return ["no comparable C/Go speed and size rows were measured"]
    if full_unfiltered and (stats["speed_rows"] != 240 or stats["size_rows"] != 240):
        return [
            f"incomplete full matrix: {stats['speed_rows']}/240 speed rows, "
            f"{stats['size_rows']}/240 size rows"
        ]
    return []


def gate_rows(comparison: list[dict[str, object]], profile: str) -> list[dict[str, object]]:
    if profile == "latency":
        return [r for r in comparison if str(r["corpus"]).startswith("small-")]
    if profile == "focused":
        return [
            r for r in comparison
            if (str(r["operation"]), str(r["corpus"]), str(r["level"])) in FOCUSED_ROWS
        ]
    if profile in ("l2-encode", "l2-encode-blockers"):
        return [
            r for r in comparison
            if (str(r["operation"]), str(r["corpus"]), str(r["level"])) in L2_ENCODE_ROWS
        ]
    if profile == "structured-l2":
        return [
            r for r in comparison
            if (str(r["operation"]), str(r["corpus"]), str(r["level"])) in STRUCTURED_L2_ROWS
        ]
    if profile == "l2-repeat-blocker":
        return [
            r for r in comparison
            if (str(r["operation"]), str(r["corpus"]), str(r["level"])) in L2_REPEAT_ROWS
        ]
    if profile == "last-l2-blocker":
        return [
            r for r in comparison
            if (str(r["operation"]), str(r["corpus"]), str(r["level"])) in LAST_L2_BLOCKER_ROWS
        ]
    return list(comparison)


def gate_failures(comparison: list[dict[str, object]], profile: str) -> list[str]:
    failures: list[str] = []
    for row in gate_rows(comparison, profile):
        speedup = row.get("speedup")
        size_delta = row.get("size_delta")
        if size_delta is not None and int(size_delta) > 0:
            failures.append(
                f"size {row['operation']} {row['corpus']} {row['level']}: "
                f"C {row['c_bytes']} > Go {row['go_bytes']}"
            )
        if speedup is not None and float(speedup) < 1.0:
            failures.append(
                f"speed {row['operation']} {row['corpus']} {row['level']}: "
                f"C {fmt(row['c_mbps'])} < Go {fmt(row['go_mbps'])} "
                f"(speedup {fmt(speedup, 3)})"
            )
    return failures


def _comparison_key(row: dict[str, object]) -> tuple[str, str, str]:
    return (str(row["operation"]), str(row["corpus"]), str(row["level"]))


def _raw_level_arg(level_label: str) -> str:
    return LEVEL_ARGS.get(level_label, level_label)


def official_confirmation_targets(
    comparison: list[dict[str, object]],
    profile: str,
    threshold: float,
) -> list[dict[str, object]]:
    targets: list[dict[str, object]] = []
    for row in gate_rows(comparison, profile):
        speedup = row.get("speedup")
        size_delta = row.get("size_delta")
        if size_delta is not None and int(size_delta) > 0:
            continue
        if speedup is not None and (
            float(speedup) < threshold or _comparison_key(row) in ALWAYS_CONFIRM_ROWS
        ):
            targets.append(row)
    targets.sort(key=lambda r: (str(r["corpus"]), str(r["level"]), str(r["operation"])))
    return targets


def run_official_confirmations(
    args: argparse.Namespace,
    root: Path,
    comparison: list[dict[str, object]],
) -> tuple[list[Confirmation], str]:
    targets = official_confirmation_targets(comparison, args.gate_profile, args.confirm_threshold)
    if not targets:
        return [], ""

    confirm_args = argparse.Namespace(**vars(args))
    confirm_args.samples = args.confirm_samples
    confirm_args.rounds = args.confirm_rounds
    confirm_args.iterations = args.confirm_iterations
    confirm_args.keep_going = args.keep_going
    confirm_args.official_gate = False

    by_case: dict[tuple[str, str], list[dict[str, object]]] = {}
    for row in targets:
        by_case.setdefault((str(row["corpus"]), str(row["level"])), []).append(row)

    confirmations: list[Confirmation] = []
    stderr_parts: list[str] = []
    for (corpus, level_label), rows in sorted(by_case.items()):
        round_rows: list[list[dict[str, str]]] = []
        round_err: list[str] = []
        for round_no in range(1, max(1, confirm_args.rounds) + 1):
            orders = (False, True) if confirm_args.alternate_go_order else (False,)
            for go_first in orders:
                cmd, env = _perf_command(
                    confirm_args,
                    root,
                    corpus,
                    _raw_level_arg(level_label),
                    go_first,
                )
                proc = subprocess.run(cmd, cwd=root, env=env, text=True, capture_output=True)
                order_label = "go-first" if go_first else "c-first"
                round_err.append(
                    f"# confirmation round {round_no}/{max(1, confirm_args.rounds)} "
                    f"order={order_label}\n{proc.stderr}"
                )
                if proc.returncode != 0 and not confirm_args.keep_going:
                    sys.stderr.write(proc.stderr)
                    raise SystemExit(proc.returncode)
                round_rows.append(parse_rows(proc.stdout))
        raw_csv = _aggregate_round_rows(round_rows)
        raw_err = "\n".join(round_err)
        stderr_parts.append(
            f"# official confirm corpus={corpus} level={level_label} "
            f"samples={confirm_args.samples} rounds={confirm_args.rounds}\n{raw_err}"
        )
        confirmed = {
            _comparison_key(row): row
            for row in build_comparison(parse_rows(raw_csv))
        }
        for row in rows:
            key = _comparison_key(row)
            c_row = confirmed.get(key)
            c_speedup = fnum(c_row, "speedup") if c_row else None
            c_size_delta = inum(c_row, "size_delta") if c_row else None
            full_speedup = row.get("speedup")
            full_size_delta = row.get("size_delta")
            status = "fail"
            reason = "missing confirmation row"
            if c_row:
                if c_size_delta is not None and c_size_delta > 0:
                    reason = "confirmed size regression"
                elif c_speedup is not None and c_speedup >= 1.0:
                    status = "pass"
                    reason = "confirmed median-of-medians"
                else:
                    reason = "confirmed Go speed win"
            confirmations.append(
                Confirmation(
                    operation=str(row["operation"]),
                    corpus=str(row["corpus"]),
                    level=str(row["level"]),
                    full_speedup=float(full_speedup) if full_speedup is not None else None,
                    confirm_speedup=c_speedup,
                    full_size_delta=int(full_size_delta) if full_size_delta is not None else None,
                    confirm_size_delta=c_size_delta,
                    status=status,
                    reason=reason,
                )
            )
    return confirmations, "\n".join(stderr_parts)


def official_gate_failures(
    comparison: list[dict[str, object]],
    profile: str,
    confirmations: list[Confirmation],
) -> list[str]:
    failures: list[str] = []
    confirmed_ok = {
        (c.operation, c.corpus, c.level)
        for c in confirmations
        if c.status == "pass"
    }
    confirmed_seen = {
        (c.operation, c.corpus, c.level)
        for c in confirmations
    }
    for row in gate_rows(comparison, profile):
        key = _comparison_key(row)
        speedup = row.get("speedup")
        size_delta = row.get("size_delta")
        if size_delta is not None and int(size_delta) > 0:
            failures.append(
                f"size {row['operation']} {row['corpus']} {row['level']}: "
                f"C {row['c_bytes']} > Go {row['go_bytes']}"
            )
            continue
        if speedup is not None and float(speedup) < 1.0 and key not in confirmed_ok and key not in confirmed_seen:
            failures.append(
                f"speed {row['operation']} {row['corpus']} {row['level']}: "
                f"C {fmt(row['c_mbps'])} < Go {fmt(row['go_mbps'])} "
                f"(speedup {fmt(speedup, 3)})"
            )
    for c in confirmations:
        if c.status != "pass":
            failures.append(
                f"confirm {c.operation} {c.corpus} {c.level}: "
                f"speedup {fmt(c.confirm_speedup, 3)} size_delta {fmt(c.confirm_size_delta, 0)} "
                f"({c.reason})"
            )
    return failures


def blocker_group(row: dict[str, object]) -> str:
    op = str(row["operation"])
    corpus = str(row["corpus"])
    level = str(row["level"])
    if op == "block-decode":
        if corpus.startswith("small-"):
            return "small-decode"
        if level == "-0" or row.get("c_bytes") == row.get("raw_bytes"):
            return "large-decode"
        return "structured-decode"
    if op == "block-encode":
        if corpus.startswith("small-"):
            return "small-encode"
        return "encoder"
    if op.startswith("index") or op == "read-at-hot":
        return "index/read-at"
    if op.startswith("stream"):
        return "stream"
    return "other"


def blocking_rows(comparison: list[dict[str, object]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for row in comparison:
        speedup = row.get("speedup")
        size_delta = row.get("size_delta")
        if (speedup is not None and float(speedup) < 1.0) or (
            size_delta is not None and int(size_delta) > 0
        ):
            out = dict(row)
            out["blocker_group"] = blocker_group(row)
            rows.append(out)
    rows.sort(key=lambda r: (str(r["blocker_group"]), float(r.get("speedup") or 0.0)))
    return rows


def write_markdown(path: Path, comparison: list[dict[str, object]], meta: dict[str, object]) -> None:
    s = summary(comparison)
    blockers = blocking_rows(comparison)
    confirmations: list[Confirmation] = meta.get("confirmations") or []
    headers = ["operation", "corpus", "level", "C MB/s", "Go MB/s", "speedup", "C bytes", "Go bytes", "delta", "winner"]
    lines = [
        "# MinLZ C vs Go Performance Report",
        "",
        f"- Generated: {meta['generated']}",
        f"- Command: `{meta['command']}`",
        f"- Samples: {meta['samples']}",
        f"- Rounds: {meta['rounds']}",
        f"- Iterations: {meta['iterations']}",
        f"- Comparable speed rows: {s['speed_rows']}; C wins: {s['c_speed_wins']}; Go wins: {s['go_speed_wins']}",
        f"- Size rows: {s['size_rows']}; C <= Go: {s['c_size_ok']}; C > Go: {s['c_size_worse']}",
        f"- Gate profile: {meta['gate_profile']}",
        "",
    ]
    if meta.get("gate_status"):
        lines.extend([
            f"- Final gate: **{meta['gate_status']}**; blocking conditions: {len(meta.get('gate_failures') or [])}",
            "- Speed win counts above describe the initial matrix, before focused confirmations.",
            "",
        ])
    cpu_policy = meta.get("cpu_policy") or {}
    build_policy = meta.get("build_policy") or {}
    if build_policy:
        lines.extend([
            f"- Experimental L2 search: `{build_policy['experimental_l2_search']}`",
            f"- Workspace hugepage hints: `{build_policy.get('workspace_hugepages', 'unknown')}`",
            f"- Readseeker buffer transfer: `{build_policy.get('readseeker_buffer_transfer', 'unknown')}`",
            "",
        ])
    if cpu_policy:
        lines.extend([
            f"- C backend: `{cpu_policy['backend']}`; affinity: `{meta.get('affinity') or 'unrestricted'}`",
            f"- Go AMD64: `{cpu_policy['go_amd64']}`",
            f"- GODEBUG: `{cpu_policy['go_debug']}`",
            f"- GLIBC_TUNABLES: `{cpu_policy['libc_tunables']}`",
            "",
        ])
    warnings = meta.get("warnings") or []
    if warnings:
        lines.extend(["", "## Runner Notes", ""])
        for warning in warnings:
            lines.append(f"- `{warning}`")
        lines.append("")
    if blockers:
        lines.extend(["", "## Blocking Rows", ""])
        lines.extend(["| group | operation | corpus | level | C MB/s | Go MB/s | speedup | C bytes | Go bytes | delta |", "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"])
        for r in blockers:
            lines.append(
                "| "
                + " | ".join(
                    [
                        str(r["blocker_group"]),
                        str(r["operation"]),
                        str(r["corpus"]),
                        str(r["level"]),
                        fmt(r["c_mbps"]),
                        fmt(r["go_mbps"]),
                        fmt(r["speedup"], 3),
                        fmt(r["c_bytes"], 0),
                        fmt(r["go_bytes"], 0),
                        fmt(r["size_delta"], 0),
                    ]
                )
                + " |"
            )
        lines.append("")
    if confirmations:
        lines.extend(["", "## Official Gate Confirmations", ""])
        lines.extend([
            "| operation | corpus | level | full speedup | confirm speedup | full delta | confirm delta | status | reason |",
            "| --- | --- | --- | --- | --- | --- | --- | --- | --- |",
        ])
        for c in confirmations:
            lines.append(
                "| "
                + " | ".join(
                    [
                        c.operation,
                        c.corpus,
                        c.level,
                        fmt(c.full_speedup, 3),
                        fmt(c.confirm_speedup, 3),
                        fmt(c.full_size_delta, 0),
                        fmt(c.confirm_size_delta, 0),
                        c.status,
                        c.reason,
                    ]
                )
                + " |"
            )
        lines.append("")
    lines.extend(["| " + " | ".join(headers) + " |", "| " + " | ".join(["---"] * len(headers)) + " |"])
    for r in comparison:
        lines.append(
            "| "
            + " | ".join(
                [
                    str(r["operation"]),
                    str(r["corpus"]),
                    str(r["level"]),
                    fmt(r["c_mbps"]),
                    fmt(r["go_mbps"]),
                    fmt(r["speedup"], 3),
                    fmt(r["c_bytes"], 0),
                    fmt(r["go_bytes"], 0),
                    fmt(r["size_delta"], 0),
                    str(r["winner"]),
                ]
            )
            + " |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_html(path: Path, comparison: list[dict[str, object]], meta: dict[str, object]) -> None:
    s = summary(comparison)
    blockers = blocking_rows(comparison)
    warnings = meta.get("warnings") or []
    confirmations: list[Confirmation] = meta.get("confirmations") or []
    cpu_policy = meta.get("cpu_policy") or {}
    cpu_html = ""
    build_policy = meta.get("build_policy") or {}
    build_html = ""
    if build_policy:
        build_html = (
            '<div class="meta">Experimental L2 search: <code>'
            + html.escape(str(build_policy["experimental_l2_search"]))
            + "</code><br>Workspace hugepage hints: <code>"
            + html.escape(str(build_policy.get("workspace_hugepages", "unknown")))
            + "</code><br>Readseeker buffer transfer: <code>"
            + html.escape(str(build_policy.get("readseeker_buffer_transfer", "unknown")))
            + "</code></div>"
        )
    if cpu_policy:
        cpu_html = (
            "<div class=\"meta\">C backend: <code>"
            + html.escape(cpu_policy["backend"])
            + "</code>; affinity: <code>"
            + html.escape(str(meta.get("affinity") or "unrestricted"))
            + "</code><br>Go AMD64: <code>"
            + html.escape(cpu_policy["go_amd64"])
            + "</code><br>GODEBUG: <code>"
            + html.escape(cpu_policy["go_debug"])
            + "</code><br>GLIBC_TUNABLES: <code>"
            + html.escape(cpu_policy["libc_tunables"])
            + "</code></div>"
        )
    gate_html = ""
    if meta.get("gate_status"):
        gate_html = (
            "<p class=\"gate "
            + ("fail" if meta["gate_status"] == "FAIL" else "pass")
            + "\"><strong>Final gate: "
            + html.escape(str(meta["gate_status"]))
            + "</strong>; blocking conditions: "
            + str(len(meta.get("gate_failures") or []))
            + "<br>Speed win counts describe the initial matrix, before focused confirmations.</p>"
        )
    warning_html = ""
    if warnings:
        warning_html = (
            "<details open><summary>Runner notes</summary><ul>"
            + "".join(f"<li><code>{html.escape(str(w))}</code></li>" for w in warnings)
            + "</ul></details>"
        )
    rows = []
    for r in comparison:
        cls = "win" if r["winner"] == "C" else "loss" if r["winner"] == "Go" else ""
        size_cls = "size-ok" if (r["size_delta"] is not None and int(r["size_delta"]) <= 0) else "size-bad"
        rows.append(
            "<tr class=\"%s\">"
            "<td>%s</td><td>%s</td><td>%s</td>"
            "<td class=\"num\">%s</td><td class=\"num\">%s</td><td class=\"num\">%s</td>"
            "<td class=\"num\">%s</td><td class=\"num\">%s</td><td class=\"num %s\">%s</td>"
            "<td>%s</td></tr>"
            % (
                cls,
                html.escape(str(r["operation"])),
                html.escape(str(r["corpus"])),
                html.escape(str(r["level"])),
                html.escape(fmt(r["c_mbps"])),
                html.escape(fmt(r["go_mbps"])),
                html.escape(fmt(r["speedup"], 3)),
                html.escape(fmt(r["c_bytes"], 0)),
                html.escape(fmt(r["go_bytes"], 0)),
                size_cls,
                html.escape(fmt(r["size_delta"], 0)),
                html.escape(str(r["winner"])),
            )
        )
    blocker_html = ""
    if blockers:
        blocker_rows = []
        for r in blockers:
            blocker_rows.append(
                "<tr>"
                "<td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                "<td class=\"num\">%s</td><td class=\"num\">%s</td><td class=\"num\">%s</td>"
                "<td class=\"num\">%s</td><td class=\"num\">%s</td><td class=\"num\">%s</td>"
                "</tr>"
                % (
                    html.escape(str(r["blocker_group"])),
                    html.escape(str(r["operation"])),
                    html.escape(str(r["corpus"])),
                    html.escape(str(r["level"])),
                    html.escape(fmt(r["c_mbps"])),
                    html.escape(fmt(r["go_mbps"])),
                    html.escape(fmt(r["speedup"], 3)),
                    html.escape(fmt(r["c_bytes"], 0)),
                    html.escape(fmt(r["go_bytes"], 0)),
                    html.escape(fmt(r["size_delta"], 0)),
                )
            )
        blocker_html = (
            "<h2>Blocking Rows</h2><table><thead><tr>"
            "<th>Group</th><th>Operation</th><th>Corpus</th><th>Level</th>"
            "<th>C MB/s</th><th>Go MB/s</th><th>Speedup</th>"
            "<th>C bytes</th><th>Go bytes</th><th>Delta</th>"
            "</tr></thead><tbody>"
            + "".join(blocker_rows)
            + "</tbody></table>"
        )
    confirm_html = ""
    if confirmations:
        confirm_rows = []
        for c in confirmations:
            cls = "win" if c.status == "pass" else "loss"
            confirm_rows.append(
                "<tr class=\"%s\">"
                "<td>%s</td><td>%s</td><td>%s</td>"
                "<td class=\"num\">%s</td><td class=\"num\">%s</td>"
                "<td class=\"num\">%s</td><td class=\"num\">%s</td>"
                "<td>%s</td><td>%s</td></tr>"
                % (
                    cls,
                    html.escape(c.operation),
                    html.escape(c.corpus),
                    html.escape(c.level),
                    html.escape(fmt(c.full_speedup, 3)),
                    html.escape(fmt(c.confirm_speedup, 3)),
                    html.escape(fmt(c.full_size_delta, 0)),
                    html.escape(fmt(c.confirm_size_delta, 0)),
                    html.escape(c.status),
                    html.escape(c.reason),
                )
            )
        confirm_html = (
            "<h2>Official Gate Confirmations</h2><table><thead><tr>"
            "<th>Operation</th><th>Corpus</th><th>Level</th>"
            "<th>Full speedup</th><th>Confirm speedup</th>"
            "<th>Full delta</th><th>Confirm delta</th><th>Status</th><th>Reason</th>"
            "</tr></thead><tbody>"
            + "".join(confirm_rows)
            + "</tbody></table>"
        )
    doc = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>MinLZ C vs Go Performance Report</title>
<style>
body {{ font-family: Inter, Segoe UI, Arial, sans-serif; margin: 28px; color: #1b1f24; }}
h1 {{ margin-bottom: 6px; }}
.meta {{ color: #59636e; margin-bottom: 18px; }}
.summary {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(170px, 1fr)); gap: 10px; margin: 18px 0; }}
.metric {{ border: 1px solid #d0d7de; border-radius: 8px; padding: 10px 12px; }}
.metric b {{ display: block; font-size: 22px; }}
table {{ border-collapse: collapse; width: 100%; font-size: 13px; }}
th, td {{ border-bottom: 1px solid #d8dee4; padding: 7px 8px; text-align: left; }}
th {{ position: sticky; top: 0; background: #f6f8fa; z-index: 1; }}
.num {{ text-align: right; font-variant-numeric: tabular-nums; }}
tr.win {{ background: #f0fff4; }}
tr.loss {{ background: #fff5f5; }}
.size-ok {{ color: #116329; }}
.size-bad {{ color: #cf222e; font-weight: 600; }}
.gate.fail {{ color: #cf222e; }}
.gate.pass {{ color: #116329; }}
.meta code {{ overflow-wrap: anywhere; }}
code {{ background: #f6f8fa; padding: 2px 4px; border-radius: 4px; }}
details {{ margin: 16px 0; }}
summary {{ cursor: pointer; font-weight: 650; }}
</style>
</head>
<body>
<h1>MinLZ C vs Go Performance Report</h1>
{gate_html}
<div class="meta">Generated {html.escape(str(meta['generated']))}<br>Command: <code>{html.escape(str(meta['command']))}</code><br>Samples: {html.escape(str(meta['samples']))}; Rounds: {html.escape(str(meta['rounds']))}; Iterations: {html.escape(str(meta['iterations']))}</div>
{cpu_html}
{build_html}
<div class="summary">
<div class="metric"><b>{s['speed_rows']}</b>speed rows</div>
<div class="metric"><b>{s['c_speed_wins']}</b>C speed wins</div>
<div class="metric"><b>{s['go_speed_wins']}</b>Go speed wins</div>
<div class="metric"><b>{s['c_size_ok']}/{s['size_rows']}</b>C size <= Go</div>
<div class="metric"><b>{html.escape(str(meta['gate_profile']))}</b>gate profile</div>
</div>
{warning_html}
{blocker_html}
{confirm_html}
<table>
<thead><tr><th>Operation</th><th>Corpus</th><th>Level</th><th>C MB/s</th><th>Go MB/s</th><th>Speedup</th><th>C bytes</th><th>Go bytes</th><th>Delta</th><th>Winner</th></tr></thead>
<tbody>
{''.join(rows)}
</tbody>
</table>
</body>
</html>
"""
    path.write_text(doc, encoding="utf-8")


def main() -> int:
    args = parse_args()
    root = repo_root()
    out_dir = (root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")

    raw_csv, raw_err, cmd = run_perf(args, root)
    rows = parse_rows(raw_csv)
    comparison = build_comparison(rows)
    confirmations: list[Confirmation] = []
    confirm_err = ""
    if args.official_gate:
        confirmations, confirm_err = run_official_confirmations(args, root, comparison)
    s = summary(comparison)
    failures = gate_coverage_failures(
        s, args.gate_profile == "full" and not args.corpus and not args.level
    )
    failures.extend(
        official_gate_failures(comparison, args.gate_profile, confirmations)
        if args.official_gate
        else gate_failures(comparison, args.gate_profile)
    )
    meta = {
        "generated": _dt.datetime.now().isoformat(timespec="seconds"),
        "command": " ".join(cmd),
        "samples": args.samples,
        "rounds": max(1, args.rounds),
        "iterations": args.iterations if args.iterations > 0 else "auto",
        "gate_profile": args.gate_profile + ("+official" if args.official_gate else ""),
        "cpu_policy": cpu_gate_metadata(),
        "build_policy": build_gate_metadata(root, args.build_dir),
        "affinity": args.taskset,
        "gate_status": "FAIL" if failures else "PASS",
        "gate_failures": failures,
        "confirmations": confirmations,
        "warnings": [
            line
            for line in (raw_err + "\n" + confirm_err).splitlines()
            if "failed" in line.lower() or "unsupported" in line.lower() or "gate failed" in line.lower()
        ][:30],
    }

    raw_path = out_dir / f"perf-raw-{stamp}.csv"
    err_path = out_dir / f"perf-raw-{stamp}.stderr.txt"
    csv_path = out_dir / f"perf-compare-{stamp}.csv"
    md_path = out_dir / f"perf-compare-{stamp}.md"
    html_path = out_dir / f"perf-compare-{stamp}.html"
    raw_path.write_text(raw_csv, encoding="utf-8")
    err_path.write_text(raw_err + ("\n" + confirm_err if confirm_err else ""), encoding="utf-8")
    write_csv(csv_path, comparison)
    write_markdown(md_path, comparison, meta)
    write_html(html_path, comparison, meta)

    for src, name in [
        (raw_path, "latest-raw.csv"),
        (err_path, "latest-raw.stderr.txt"),
        (csv_path, "latest.csv"),
        (md_path, "latest.md"),
        (html_path, "latest.html"),
    ]:
        shutil.copyfile(src, out_dir / name)

    print(f"wrote {csv_path}")
    print(f"wrote {md_path}")
    print(f"wrote {html_path}")
    print(
        f"speed rows={s['speed_rows']} c_wins={s['c_speed_wins']} "
        f"go_wins={s['go_speed_wins']} size_ok={s['c_size_ok']}/{s['size_rows']}"
    )
    if args.official_gate:
        passed = sum(1 for c in confirmations if c.status == "pass")
        print(
            f"official confirmations={len(confirmations)} passed={passed} "
            f"failed={len(confirmations) - passed}"
        )
    if args.fail_on_go_win:
        if failures:
            sys.stderr.write(
                f"gate failed profile={args.gate_profile} failures={len(failures)}\n"
            )
            for failure in failures[:50]:
                sys.stderr.write(f"- {failure}\n")
            if len(failures) > 50:
                sys.stderr.write(f"- ... {len(failures) - 50} more\n")
            return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
