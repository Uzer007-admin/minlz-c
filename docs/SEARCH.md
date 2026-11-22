# MinLZ v1.2 Search and Sidecars

[Русский](SEARCH.ru.md) · [Home](../Readme.md)

MinLZ v1.2 adds optional per-block search tables. A table acts like a compact
Bloom-style bitmap: it can prove that a block does not contain a literal byte
pattern, allowing the compressed block to be skipped without decoding it.
Search always verifies candidates against decoded bytes, so collisions only
cost time and never produce false matches.

## Wire chunks

| Type | Purpose |
| --- | --- |
| `0x44` | Search configuration information |
| `0x45` | Uncompressed search bitmap with masked CRC32C |
| `0x46` | Partitioned/compressed search bitmap |
| `0x47` | Sidecar reference to a data block in another MinLZ stream |

All four chunks follow the MinLZ v1.2 search specification. Normal stream
decoders ignore them because they are skippable.

The examples show call order; application code must check every returned status.

## Configuration

Initialize a fixed-size, allocation-free configuration:

```c
minlz_search_config config;

minlz_search_config_init(&config);
minlz_search_config_set_match_len(&config, 6);
minlz_search_config_set_population(&config, 70, 25);
```

Prefix filters reduce index size for structured data:

```c
static const uint8_t prefixes[] = {'"', ':'};

minlz_search_config_set_match_len(&config, 4);
minlz_search_config_set_byte_prefix(
    &config, prefixes, sizeof(prefixes));
```

Long-prefix tables support up to 256 prefix bytes and up to 15 extra
overlapping windows, with `match_len + extras <= 16`.
`minlz_search_config_set_long_prefix` sets both values in one call;
`minlz_search_config_set_extras` is the direct C equivalent of Go
`SearchTableConfig.WithExtras` when tuning an existing long-prefix config.

A long prefix can also be searched by itself even when the query is shorter
than `match_len`. An all-zero table skips ordinary blocks without decoding.
For the final block, which has no forward overlap, the searcher retains one
lazy candidate and scans it at EOF. This is the v1.2 prefix-only tail-rescue
rule and applies equally to inline and sidecar searches.

## Writing inline tables

```c
minlz_stream_options options;
minlz_writer *writer = NULL;

minlz_stream_options_init(&options);
options.search = &config;
minlz_writer_create(&writer, output_cb, output_ctx, &options);
minlz_writer_write(writer, input, input_len, NULL);
minlz_writer_close(writer);
minlz_writer_destroy(writer);
```

`minlz_writer_set_search_table` and `minlz_writer_set_sidecar` provide the
same behavior for an existing writer object. A sidecar writer receives
search chunks and `0x47` references while the main writer receives only data
chunks.

## Searching

```c
minlz_block_searcher *searcher = NULL;

minlz_block_searcher_create(&searcher, input_cb, input_ctx);
minlz_block_searcher_set_max_block_size(
    searcher, MINLZ_MAX_BLOCK_SIZE);
minlz_block_searcher_search(
    searcher, pattern, pattern_len, on_match, match_ctx);
minlz_block_searcher_destroy(searcher);
```

The callback receives the previous/current decoded blocks, the logical match
offset, and the absolute uncompressed stream offset. Return:

- `MINLZ_OK` to continue.
- `MINLZ_SEARCH_STOP` to finish successfully.
- `MINLZ_SEARCH_FORWARD` to request forward context from either searcher.
- another status to abort.

Each match can request forward context independently. On a repeat callback,
the previous buffer covers the match's original block through the last block
shown to the callback. The current buffer is the next block, or empty at the
end of the stream. Returning `MINLZ_SEARCH_FORWARD` again extends that context.

Like Go `SearchResult.PrevBlock()`, C does not eagerly decode a previous block
that the table proved irrelevant. Inside the callback, load it only when
needed:

```c
const uint8_t *previous = NULL;
size_t previous_len = 0;

minlz_search_result_load_previous(
    result, &previous, &previous_len);
```

The returned block is cached for repeated calls. The result and all block
pointers remain valid only until the callback returns, matching Go v1.2.0.

Searchers validate stream structure, exact decoded sizes, CRC32C, EOF sizes,
and block limits. CRC checks can be disabled explicitly. Bail mode returns
`MINLZ_SEARCH_TABLES_UNUSABLE` instead of decoding blocks whose tables are
missing or unusable.

## Sidecars

Build new tables for an existing stream:

```c
minlz_search_config configs[2];

minlz_search_config_init(&configs[0]);
configs[1] = configs[0];
minlz_search_config_set_match_len(&configs[1], 4);

minlz_build_sidecar(
    sidecar_write_cb, sidecar_ctx,
    stream_read_cb, stream_ctx,
    configs, 2, 0);
```

Extract existing inline tables, optionally producing a stripped main stream:

```c
minlz_extract_sidecar(
    sidecar_write_cb, sidecar_ctx,
    stripped_write_cb, stripped_ctx,
    stream_read_cb, stream_ctx);
```

Search a sidecar with sequential sidecar I/O and random-access main I/O:

```c
minlz_sidecar_searcher *searcher = NULL;

minlz_sidecar_searcher_create(
    &searcher, main_read_at_cb, main_ctx,
    sidecar_read_cb, sidecar_ctx);
minlz_sidecar_searcher_search(
    searcher, pattern, pattern_len, on_match, match_ctx);
minlz_sidecar_searcher_destroy(searcher);
```

Required neighboring main-stream blocks use a reusable checked read-ahead
cache. It coalesces chunk headers, payloads, and nearby blocks into fewer
`ReadAt` callbacks while retaining exact short-read and bounds checks.

Both inline and sidecar search retain the final `pattern_len - 1` bytes of the
contiguous decoded run. This matches Go v1.2 for patterns spanning three or
more blocks when an interior `Flush()` block is shorter than the pattern.

Build, extract, inline search, and sidecar search all support concatenated
MinLZ streams. Remote block offsets remain absolute in the underlying main
file, while reported uncompressed offsets restart at each stream boundary,
matching Go v1.2.0.

## Compressed tables

The C encoder and decoder implement the complete v1.2 `0x46` format:

- raw sub-blocks;
- RLE sub-blocks;
- sparse bit tables;
- private and shared Huff0/FSE tables.

The encoder falls back to `0x45` when compression is not beneficial unless
forced compression is selected in the API. Huff0/FSE is supplied by a small
vendored entropy-coding subset from zstd 1.5.7; no runtime zstd dependency is
required. The bundled Go compatibility tests exercise Huff0 in strict
`bail-on-missing` mode in both directions, proving that the compressed table
is usable rather than merely falling back to decoding the data block.

`minlz_search_config_set_compression_skip` matches Go v1.2 semantics: values
are clamped to 0-50 percent and stored with 0.01 percent resolution.
`minlz_search_config_set_compression_stats_callback` exposes the same
per-disposition accounting as Go's `CompressedSearchStatsHook`.

## Statistics

Detailed table statistics are opt-in because population and per-window
accounting add work to the search loop:

```c
minlz_block_searcher_set_collect_stats(searcher, 1);
```

After a search, use `minlz_block_searcher_stats` and the
`minlz_block_searcher_window_*` accessors. Sidecar searchers provide matching
functions. `minlz_search_stats_fprint` and
`minlz_search_stats_fprint_extended` correspond to Go's `Fprint` and
`FprintExtended`.

## Compatibility authority

The exact wire definitions are installed with the library as
`SPEC.md` and `SPEC_SEARCH.md`. Go MinLZ v1.2.0 remains the behavior oracle.
CTest covers no/byte/mask/long-prefix tables, all compression levels, Huff0,
sidecars, concatenated streams, prefix-only tail rescue, forward context,
lazy previous-block loading, and a deterministic randomized
Go-to-C/C-to-Go search matrix.
