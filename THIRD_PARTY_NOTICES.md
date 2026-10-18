# Third-Party Notices

[Русский](THIRD_PARTY_NOTICES.ru.md)

## MinLZ

This project is an independent C port of MinLZ. The format, reference Go
implementation, and portions of the algorithmic behavior originate from
MinLZ by MinIO, Inc., distributed under the Apache License 2.0. The project
license is reproduced in `LICENSE`. Assembly paths are adapted to the C ABI
and the checked wrappers in this project. The wire specifications and test
fixtures also originate from MinLZ.

Copyright 2025 MinIO Inc.

Upstream reference: <https://github.com/minio/minlz>

## Zstandard entropy coding subset

`src/third_party/zstd` contains the Huff0/FSE entropy-coding subset from
Zstandard 1.5.7. It is used only for MinLZ v1.2 compressed search-table chunks
and does not add a runtime zstd dependency. The bundled `xxhash.h` retains
its own copyright and license notice in the header.

Upstream project: <https://github.com/facebook/zstd>

BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
* Neither the name Facebook, nor Meta, nor the names of its contributors may
  be used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
