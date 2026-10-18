# Testing

[Русский](TESTING.ru.md) · [Home](../Readme.md)

## Release build

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

CTest includes the default unit suite, forced portable/SSE4.2/asm/AVX2
backends, an AVX512 probe, workspace allocation checks, and report-tool
checks when Python is available. Unsupported CPU modes fall back safely;
a successful probe does not prove that AVX512 instructions executed.

## Go reference

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DMINLZ_GO_REPO=/path/to/minlz
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

Use a reference checkout with the v1.2.0 search API and a Go toolchain
compatible with its `go.mod`. Record its commit and any local modifications.
CTest reports when Go checks cannot be enabled; a standalone pass is not
a cross-language pass. Run `go test ./...` and `go test -tags purego ./...`
in the reference checkout separately when validating upstream changes.

The compatibility matrix checks block/stream encode and decode in both
directions at every level, strict compressed search tables, prefix modes,
sidecars, extraction, concatenated streams, boundary matches, randomized
search, statistics, and the bundled CLI. Windows also exercises a Go helper
whose executable path contains spaces.

Unit tests cover short destination buffers, overlapping copies, long matches,
CRC/EOF checks, and indexed reads. Fixtures generated during testing belong
in the build directory; a clean checkout does not need local compressed files.

## Portable sanitizers (Linux, GCC/Clang)

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DMINLZ_ENABLE_ASM=OFF \
  -DMINLZ_ENABLE_INTRINSICS=OFF \
  -DMINLZ_ENABLE_INLINE_ASM=OFF \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Optional paths

Validate each option independently before combining them:

- `MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH`: seeded literal/match inputs around
  the 512 KiB and 2 MiB bucket boundaries; compare exact output against the
  independent assembly reference and decode with portable C and Go.
- `MINLZ_ENABLE_HUGEPAGE_WORKSPACE`: promotion/refusal, reuse, reset, growth,
  overflow, and ordinary allocation fallback. On Linux the isolated workspace
  test wraps `madvise` to exercise a rejected hint deterministically.
- `MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER`: mixed raw/compressed cache
  eviction, CRC failure/retry, sparse indexes, guarded cross-block reads,
  shared readers, reset, and bounded retention of oversized input scratch.
  Repeat the suite with the option OFF and ON, including sanitizers.

## Installation

```sh
cmake --install build-release --config Release --prefix "$PWD/install-test"
cmake -S tests/package_consumer -B build-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/install-test"
cmake --build build-consumer --config Release
```

Run the resulting `minlz_package_consumer` executable. It checks the installed
header, exported CMake target, link dependencies, and a block roundtrip.
Verify that both Readmes, both sets of guides, specifications, and third-party
notices are installed. On PowerShell, use `$PWD.Path` when constructing paths.

## Performance acceptance

See [performance](PERFORMANCE.md) for the seven-sample size/speed gate and
focused confirmations. Unit, compatibility, and sanitizer passes establish
correctness coverage; performance claims require separate measurements.
