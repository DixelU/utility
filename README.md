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

`dixelu::background_worker` is a one-thread FIFO executor. `shutdown(drain)`
finishes every accepted task; `shutdown(cancel)` discards pending work and sends
a cooperative `std::stop_token` to the active task. Submissions return an
explicit accepted/empty/stopped result, and destruction drains by default.

`dixelu::long_uint<Degree>` provides fixed-width unsigned modular arithmetic;
degree zero is 128-bit and each following degree doubles the width. Division by
zero throws, shifts at least as large as the width return zero, and checked
decimal parsing is available through `from_decimal`. `math_utils.h` provides
checked absolute value, corrected integer powers, and exact nonnegative floor
integer roots; odd negative inputs use the floor of the magnitude and preserve
the sign.
When Boost.Multiprecision is installed, CMake adds the randomized
`numeric_utilities_test` reference suite.

`dixelu::matrix<T>` is the dynamic rectangular API and
`dixelu::sq_matrix<T, N>` (also `fixed_matrix<T, N>`) is the fixed square API.
Both use `(row, column)` coordinates and row-first `matrix[row][column]`
indexing. Checked `at` calls throw on invalid coordinates; dynamic elementwise
operations require identical shapes, multiplication requires compatible inner
dimensions, and square-only operations reject rectangular input. Floating-point
inverse and solve throw for singular matrices. `minmax` ignores non-finite
floating values and throws when none remain. The fixed API provides
`transposed()` for a copy and `transpose()` for in-place mutation.

`dixelu::polyline_converter<Key, Value>` evaluates a finite set of ordered
points using widened linear arithmetic. Interpolation between points is always
enabled; outside queries explicitly select `reject`, `clamp`, or `linear`
extrapolation. Empty curves and invalid results use `std::optional`, while
`at()` throws when a result is unavailable. Checked integral conversion names
its rounding policy and rejects values outside the destination range.
`polyline_lookup_table` stores optional entries; `materialize(fallback)` is the
only operation that replaces invalid entries with a sentinel.

`dixelu::mctx_path` is a value-type selector for a `dixelu::mctx` tree; it does
not require a separate borrowed “context” abstraction. A path may contain keys,
checked array indices, root/previous composition, bindable `*` variables, and
selectors. Selectors either use a predicate or require every equality/presence
query clause to match a child. A query’s relative path may match several
descendants; equality succeeds when at least one has the requested scalar
value. `get_all_matching()` retains all selector results, while ordinary path
lookup chooses the first.

`mctx_path_serializer.h` owns the canonical quoted text form and `_mctx_path`
literal. It accepts legacy unquoted keys where unambiguous, then serializes
them canonically with quotes. Bindings are runtime state and deliberately
remain `*` when serialized. Programmatic predicates are also runtime-only:
their string form is diagnostic and is not accepted by the deserializer.

The former monolithic SAF-MTQ pair is split by responsibility. Include
`mctx_path.h` for traversal, `mctx_path_serializer.h` for text paths, or
`linked_mctx_wrapper.h` for `$ref` projection and rendered-diff forwarding.
Linked rendering rejects reference cycles and limits link depth; `$as_array`
explicitly requests all matches rather than the first. This family retains the
SAF-MTQ MIT license; its license text is preserved at
`versions/licenses/MIT-SAF-MTQ-base.txt`.
