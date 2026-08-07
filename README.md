# DixelU::utility 

Set of utilities that were made at one point or another that might be useful for somebody
No documentation provided (yet) :P

Provided as is in a `dixelu::` namespace

Most utilities are compatible with C++11 (GCC 4.8/4.9)

Newly consolidated headers use the project's configured C++23 mode. See
`COLLECTION_REPORT.md` for promoted APIs, comparison results, deferred work and
validation boundaries. Historical source editions and their separate licensing
notes are kept under `versions/`; they are not part of the build.

The promoted file APIs are `dixelu::buffered_file_reader` and
`dixelu::memory_mapped_file_reader`. Both distinguish an empty open file from a
failed open and return `std::optional<std::byte>` from byte-wise reads, so byte
zero is never confused with EOF.

Both readers take `std::filesystem::path`. For a Unicode path written in UTF-8
source, construct it from a `u8` string, for example
`std::filesystem::path(u8"data/\u0444\u0430\u0439\u043b.bin")`. On Windows the path's native
character type is `wchar_t` under both MSVC and MinGW, and the mapped reader uses
the wide Win32 file API. `dixelu::native_string` in `unicode_literal.h` retains
the old `std_unicode_string`-style native character alias for other APIs; its
literal helper intentionally handles ASCII literals rather than runtime UTF-8
transcoding.
