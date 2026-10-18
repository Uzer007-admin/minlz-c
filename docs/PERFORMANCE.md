# Performance measurements

[Русский](PERFORMANCE.ru.md) · [Home](../Readme.md)

Measure output size and speed together. Record CPU, OS, compiler, build
options, backend, Go revision, and whether workspaces are warm. Results
from one corpus or host do not establish a universal speed advantage.

## Comparison runner

Configure with `MINLZ_GO_REPO` and run on an otherwise idle Linux host:

```sh
./build-release/minlz_perf --samples 7 \
  --go-oracle --go-speed --strict-go-size --candidate-gate
```

The runner supports `MINLZ_GO_REPO`, `MINLZ_GO_COMPAT` (helper source), and
`MINLZ_GO_COMPAT_BIN` (prebuilt helper). Prebuilding avoids repeated startup
cost. The matrix covers blocks, streams, index operations, and hot ReadAt.

## Reports and confirmations

Python 3 is required for the report wrapper:

```sh
python3 tools/minlz_report.py \
  --build-dir build-release --go-repo /path/to/minlz \
  --samples 7 --gate-profile full --keep-going \
  --fail-on-go-win --official-gate
```

Reports are generated locally under `docs/perf-reports/` by default and
are ignored by Git. `--out-dir` selects another location. CSV, Markdown,
HTML, and raw output preserve the measurements and effective CPU policy.
The wrapper confirms rows below 1.03× with seven samples over three rounds;
confirmed speed below 1.0× or C output larger than Go fails the gate.
Initial win counts do not override confirmation failures.

Use `--corpus`, `--level`, and `--rounds 3` for focused diagnostics.
`--level` takes C enum values: -1, 0, 1, 2, 3. Use `--iterations` only when
explicit loop control is needed; the runner otherwise scales iterations.
Use `--alternate-go-order` to reduce ordering bias in focused comparisons.

## CPU policy

Repeat the report with `MINLZ_FORCE_CPU=sse42` and `MINLZ_FORCE_CPU=asm`.
The wrapper builds a `GOAMD64=v2` helper for these modes, disables Go AVX
features, and masks Linux glibc AVX copy/clear dispatch. A v3/v4 helper cannot
remove its required instructions through `GODEBUG`. For no-AVX claims,
retain the wrapper's CPU policy metadata. This selects software paths on
the current CPU; it does not emulate another processor. Windows UCRT copy
implementations are not constrained by glibc tunables.

## Other measurements

```sh
./build-release/minlz_thread_perf input.bin corpus-name 2 4 3 7
```

The threaded decoder emits CSV with operation, corpus, level, threads,
byte counts, ratio, and min/median/max MB/s. Pin benchmark processes to
comparable cores and keep baseline/candidate configurations separate.
Use A/B/B/A ordering for optional workspace/cache changes. Report startup,
allocation, and tail latency separately from warmed throughput.
