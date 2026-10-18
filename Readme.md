# minlz-c

[Русский](Readme.ru.md) · English

A standalone C11 implementation of [MinLZ](https://github.com/minio/minlz),
with a static library, the `mz` command-line tool, and cross-language tests.
This implements the MinLZ specification v1.0, including the v1.2.0 search
and sidecar extensions. Go is optional and serves as the compatibility
reference for tests and benchmarks.

## Features

- Block compression and decompression at all five levels.
- Stream readers and writers with CRC32C, size checks, and indexes.
- Indexed seeking, `ReadAt`, file and callback I/O, reusable workspaces.
- Search tables and sidecar indexes, including raw, RLE, sparse, and
  Huff0/FSE encodings; concatenated streams and prefix filters.
- Portable C fallback and runtime dispatch for SSE4.2, scalar assembly,
  AVX2, and AVX512 where the compiler, CPU, and OS support them.
- CLI compression, decompression, line search, sidecars, and statistics.

## Build and test

Requirements: CMake 3.16 or later and a C11 compiler. GCC, Clang, and MSVC
builds are supported. Python 3 enables the report-tool tests. The entropy
coding sources are bundled; no external zstd library is needed.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

Visual Studio places executables in `build-release/Release/`.
Single-configuration generators place them directly in `build-release/`.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `MINLZ_BUILD_CLI` | `ON` | Build `mz` |
| `MINLZ_BUILD_TESTS` | `ON` | Register CTest checks |
| `MINLZ_BUILD_BENCH` | `ON` | Build basic benchmarks |
| `MINLZ_BUILD_PERF` | `ON` | Build comparison runners |
| `MINLZ_ENABLE_INTRINSICS` | `ON` | Runtime-dispatched intrinsic paths |
| `MINLZ_ENABLE_INLINE_ASM` | `ON` | Assembly hot paths on supported compilers |
| `MINLZ_ENABLE_ASM` | `AUTO` | Optional assembly files |
| `MINLZ_ENABLE_LTO` | `ON` | Release link-time optimization when supported |
| `MINLZ_ENABLE_NATIVE` | `OFF` | Tune for the build machine |
| `MINLZ_GO_REPO` | empty | Go checkout for compatibility checks |

Experimental L2 search, Linux hugepage workspace hints, and indexed reader
buffer transfer are disabled by default. See [optimization](docs/OPTIMIZATION.md).

## Command line

These examples assume `mz` is on `PATH` after installation. A source build
can use `./build-release/mz` or `./build-release/Release/mz.exe` instead.

```sh
mz c -2 -o output.mz input.txt
mz d -o restored.txt output.mz

# Embed search tables and find matching lines.
mz c -search -search.len 6 -bs 1M -o indexed.mz input.log
mz search -n "request failed" indexed.mz
mz search -offsets "request failed" indexed.mz

# Build a separate index for an existing stream.
mz sidecar build -search.lens 4,6,8 -o output.mzs output.mz
mz search -sidecar output.mzs "request failed" output.mz

mz stats -json -per-block output.mz
```

Levels: `-xfast`, `-0` (uncompressed), `-1`, `-2`, and `-3`.
The C API uses the corresponding `MINLZ_LEVEL_*` constants.

## Library

```c
#include <minlz.h>
#include <string.h>

int main(void) {
    const uint8_t input[] = "hello, MinLZ";
    uint8_t encoded[128], decoded[sizeof(input)];
    size_t encoded_len = 0, decoded_len = 0;

    if (minlz_encode(encoded, sizeof(encoded), &encoded_len,
                     input, sizeof(input), MINLZ_LEVEL_FASTEST) != MINLZ_OK)
        return 1;
    if (minlz_decode(decoded, sizeof(decoded), &decoded_len,
                     encoded, encoded_len) != MINLZ_OK)
        return 2;
    return decoded_len != sizeof(input) ||
           memcmp(input, decoded, sizeof(input)) != 0;
}
```

For variable-sized inputs, size the encoded buffer with
`minlz_max_encoded_len()`. Reuse a `minlz_workspace` for repeated operations.
Input blocks are limited to 8 MiB. The public API is in
[include/minlz.h](include/minlz.h).

```sh
cmake --install build-release --config Release --prefix /your/prefix
```

```cmake
find_package(minlz 1.2 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE minlz::minlz)
```

## Compatibility and limitations

Configure with `-DMINLZ_GO_REPO=/path/to/minlz` to add Go-to-C and C-to-Go
block, stream, search, sidecar, and statistics tests. The helper is included
here; the Go checkout supplies the reference module. Search tests require
the v1.2.0 reference API.

Encoded bytes may differ from Go while decoding to the same data. Speed
and size depend on the corpus, level, backend, and build options; the
project does not promise to outperform Go on every workload.

Dictionary APIs, Snappy/S2 fallback, LZ4 conversion, and the Go CLI's
HTTP/tail/follow modes are not implemented. LZ4 entry points return
`MINLZ_UNSUPPORTED`. `MINLZ_WITH_FALLBACK` does not enable a backend;
`MINLZ_FULL_GO_PARITY=ON` fails configuration explicitly.

## Documentation

| Topic | English | Русский |
| --- | --- | --- |
| Development and API conventions | [Guide](docs/DEVELOPMENT.md) | [Руководство](docs/DEVELOPMENT.ru.md) |
| Go compatibility | [Guide](docs/COMPATIBILITY.md) | [Руководство](docs/COMPATIBILITY.ru.md) |
| Search and sidecars | [Guide](docs/SEARCH.md) | [Руководство](docs/SEARCH.ru.md) |
| Testing | [Guide](docs/TESTING.md) | [Руководство](docs/TESTING.ru.md) |
| Performance | [Guide](docs/PERFORMANCE.md) | [Руководство](docs/PERFORMANCE.ru.md) |
| Optimization | [Guide](docs/OPTIMIZATION.md) | [Руководство](docs/OPTIMIZATION.ru.md) |
| Releases | [Guide](docs/RELEASE_CHECKLIST.md) | [Руководство](docs/RELEASE_CHECKLIST.ru.md) |

The upstream wire specifications are preserved in English:
[SPEC.md](SPEC.md) and [SPEC_SEARCH.md](SPEC_SEARCH.md).
Older header-only experiments in `legacy/` are excluded from builds and installation.

## License

[Apache-2.0](LICENSE). The bundled Zstandard entropy subset uses its BSD
license. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution
and license texts.
