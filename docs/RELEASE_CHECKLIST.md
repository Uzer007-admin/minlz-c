# Release checklist

[Русский](RELEASE_CHECKLIST.ru.md) · [Home](../Readme.md)

## Source and distribution

- Start from a clean checkout. Keep build output, local reports, transfer
  archives, credentials, and machine-specific configuration outside commits.
- Inspect every branch and tag intended for publication, including commit
  messages and historical files. Check the source archive as well as HEAD.
- Keep third-party copyright notices and license texts intact.
- Update both Readmes and both language versions of affected guides.
- Keep wire specifications aligned with the stated compatibility version.

## Build and correctness

- Linux Release and Windows Release builds pass CTest.
- Forced portable, SSE4.2, asm, AVX2 and AVX512 probe tests pass.
- Portable Linux ASan/UBSan passes with asm and intrinsics disabled.
- Go compatibility tests pass against an identified reference revision.
- CLI block/stream and stdin/stdout roundtrips preserve exact bytes.
- Optional paths have separate OFF/ON validation before changing defaults.

## Installation

- Install the library, public header, CLI, CMake package, specifications,
  license notices, and documentation in both languages.
- Configure the external `tests/package_consumer` with `find_package`,
  build it, and run its roundtrip. Check that the installation is relocatable.
- Check relative documentation links in the source and install trees.

## Performance and version

For a speed claim, run the full seven-sample gate and focused confirmations
for default, SSE4.2, and asm. Record hardware, compiler, Go revision, build
options, encoded sizes, and confirmed losses. See [performance](PERFORMANCE.md).

Before a versioned release, synchronize `project(minlz_c VERSION ...)`,
`MINLZ_VERSION_STRING`, package consumer expectations, and release notes.
Create a tag only after the version's validation is complete. Publishing
a source snapshot does not imply that every optional path or performance
target has passed acceptance.
