# Go compatibility

[Русский](COMPATIBILITY.ru.md) · [Home](../Readme.md)

MinLZ C implements the base block/stream format and v1.2.0 search extensions.
The wire definitions are [SPEC.md](../SPEC.md) and
[SPEC_SEARCH.md](../SPEC_SEARCH.md). Go remains the behavior reference.

## Supported surfaces

- All block and stream levels: superfast, raw, fastest, balanced, smallest.
- Stream identifiers, masked CRC32C, EOF sizes, padding, skippable chunks,
  concatenated streams, and index load/build/find.
- Reader/writer objects, file and callback wrappers, ReadSeeker and ReadAt.
- Search chunks `0x44`–`0x47`, no/byte/mask/long-prefix configurations,
  prefix-only final-block rescue, and matches crossing short flush blocks.
- Raw, RLE, sparse, private Huff0/FSE and shared Huff0/FSE tables.
- Inline and sidecar search, sidecar build/extraction, lazy previous-block
  loading, forward context, and search statistics.

| Go surface | C surface |
| --- | --- |
| `SearchTableConfig` | `minlz_search_config` |
| `WriterSearchTable`, `WriterSidecar` | Stream options and writer setters |
| `BlockSearcher`, `SidecarSearcher` | `minlz_block_searcher`, `minlz_sidecar_searcher` |
| `SearchResult.PrevBlock()` | `minlz_search_result_load_previous` |
| `BuildSidecar`, `ExtractSidecar` | `minlz_build_sidecar`, `minlz_extract_sidecar` |
| `SearchStats`, `WindowStat` | `minlz_search_stats`, `minlz_search_window_stat` |

## Verification

Set `MINLZ_GO_REPO` to a Go checkout with the v1.2.0 API. CTest adds block,
stream, search, sidecar, extraction, and statistics comparisons in both
directions, including every compression level and a deterministic randomized
search matrix. Strict table-only checks verify compressed tables without
silently falling back to data-block scans. See [testing](TESTING.md).

Compressed bytes need not be identical to Go. Decoded bytes must match
exactly. The size comparison is a separate benchmark gate over explicit
corpora; it is not a format requirement or a guarantee for all inputs.

The tagged Go v1.2.0 implementation has a buffer-reuse ordering issue in
prefix-only tail rescue. That test uses Go to produce the stream/sidecar
and checks the exact expected C match instead of relying on the affected
Go search result. C-produced streams still undergo Go decode checks.

## Boundaries

There is no public dictionary API. Snappy/S2 fallback is unavailable;
`MINLZ_WITH_FALLBACK` does not enable a backend, even when set to `ON`.
`MINLZ_FULL_GO_PARITY=ON` fails configuration.
LZ4 converter entry points return `MINLZ_UNSUPPORTED`. HTTP, tail, and follow
CLI modes are outside the implemented interface. The core library has no
runtime dependency on Go or an external compression library.
