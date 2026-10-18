# Optimization

[Русский](OPTIMIZATION.ru.md) · [Home](../Readme.md)

Compatibility takes priority over speed. Keep portable C available,
validate wrappers and tails, reuse workspaces, and retain CPUID plus OS
state checks before AVX paths. Do not default to `-march=native`.

## Acceptance

A default performance change must pass unit and forced-backend tests,
portable ASan/UBSan, and Go encode/decode checks in both directions.
For claimed wins, run the full seven-sample size/speed gate and focused
confirmations described in [performance](PERFORMANCE.md).
Require `C encoded_size <= Go encoded_size` on the measured corpus.
Reject regressions in CRC, EOF sizes, indexes, corruption checks, or bytes.

Optimize encoder match search, table initialization, and register pressure
with exact workload evidence. In decoders, treat literal dispatch, small
overlap, repeat fills, and large copies separately. Do not bypass bounds
checks merely to improve a benchmark. Stream/index changes must preserve
allocation ownership and error recovery.

## Optional paths (all OFF by default)

| Option | Behavior and cost |
| --- | --- |
| `MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH` | GNU/Clang amd64 folded-hash/register candidate for the L2 512 KiB–2 MiB bucket; preserves update order and encoded bytes |
| `MINLZ_ENABLE_HUGEPAGE_WORKSPACE` | Linux page hints for reused large encoder scratch; extra alignment/memory and a one-time promotion cost |
| `MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER` | Transfer validated workspace allocations into the indexed block cache, avoiding an extra cache-miss copy |

The L2 candidate retains the accepted fallback. Exact output equality alone
is insufficient to enable it; measure nearby block sizes, small latency,
and the complete operation matrix.

Hugepage scratch starts with 64-byte alignment. On its fourth eligible
use, the supported amd64 path attempts a 2 MiB-aligned allocation and
`MADV_HUGEPAGE`; growth restarts the count and reset retains allocation.
Allocation or hint failure preserves the old buffer. Other platforms and
portable paths use ordinary allocation. A successful hint does not prove
physical hugepage backing. The fourth call and retained memory need their
own measurements; scratch pointers must be reacquired after each encode.

Buffer transfer occurs only after length and CRC validation. Raw block views
start four bytes after their allocation; the cache retains the owner base.
Oversized compressed scratch uses bounded copying for small raw entries.
A failed decode must preserve old cache contents. This changes cache
ownership, not encoded bytes.

Test each option OFF/ON independently, then in combination. Do not infer
full acceptance from a focused warmed-workspace or hot-cache result.
See [testing](TESTING.md) for coverage and [development](DEVELOPMENT.md)
for API conventions.
