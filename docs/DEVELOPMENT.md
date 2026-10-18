# Development

[Русский](DEVELOPMENT.ru.md) · [Home](../Readme.md)

## Layout

| Path | Purpose |
| --- | --- |
| `include/minlz.h` | Public C API |
| `src/` | Codecs, dispatch, streams, indexes, search, workspaces |
| `src/minlz_internal.h` | Private interfaces |
| `src/third_party/zstd/` | Bundled entropy coding subset |
| `tools/mz.c`, `tools/mz_stats.c` | CLI and statistics |
| `tools/minlz_perf.c`, `tools/minlz_report.py` | Performance comparisons |
| `compat/minlz-go-compat/main.go` | Cross-language test helper |
| `tests/`, `testdata/` | Unit, CLI, package, and compatibility checks |
| `legacy/header-only-reference/` | Historical experiments, excluded from builds |

## API conventions

Public changes should be additive. Keep private implementation details out
of the installed header. The Go implementation defines wire compatibility
but is not a library build dependency. Encoded bytes may differ; decoded
bytes, chunk handling, CRC, EOF sizes, and index semantics must agree.

Use `minlz_workspace` for repeated operations to reuse buffers and hash
tables. A workspace is mutable: use separate workspaces for concurrent
operations. Use the object's destroy function for owned resources.
Search callback results and their block pointers are valid only during
the callback.

Functions report `minlz_status`. Check every call that can fail:

| Status | Meaning |
| --- | --- |
| `MINLZ_OK` | Success |
| `MINLZ_CORRUPT` | Malformed input or invalid data |
| `MINLZ_CRC` | CRC mismatch |
| `MINLZ_TOO_LARGE` | Size limit exceeded |
| `MINLZ_UNSUPPORTED` | Feature unavailable |
| `MINLZ_INVALID_LEVEL` | Unknown compression level |
| `MINLZ_DST_TOO_SMALL` | Destination capacity insufficient |
| `MINLZ_IO` | I/O or allocation failure |
| `MINLZ_SEARCH_TABLES_UNUSABLE` | Strict search cannot use a table |
| `MINLZ_SEARCH_STOP`, `MINLZ_SEARCH_FORWARD` | Search callback control |

## Changes and review

Keep hot-loop changes small and measure them with representative inputs.
Add implementation files to CMake, test public behavior, and document API,
option, or format changes in both languages. Do not enable native CPU
tuning by default. Keep portable fallback available and assembly behind
checked wrappers. See [testing](TESTING.md) and [optimization](OPTIMIZATION.md).
