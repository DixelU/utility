# C++ utility consolidation report

Initial scan: 2026-08-07. Last updated: 2026-09-04.

Scope: top-level repositories under `C:\Users\User\git` plus the named
`SAF*`, `WorldWars`, `MemoryObserver`, and `DeepMidiDiagnostics` repositories
under `C:\Users\User`. Every `bottle_*` repository and generated/vendor/build
trees were excluded.

## Promoted and repaired

- `include/buffered_queue_spsc.h`: merged SAFC and bitreverse editions into a
  namespaced, configurable SPSC slab queue with stable addresses, `emplace`,
  lvalue/rvalue push, const access, bounded recycling, pre-thread
  initialization, and exception cleanup for a newly allocated slab.
- `include/buffered_object_pool.h`: promoted the reusable stable-address pool,
  documented the single-threaded/live-object contract, added null-safe destroy,
  move deletion, public statistics and a compatibility alias.
- `include/buffered_block_list.h`: added a single-threaded bidirectional list
  backed by the object pool. Each control block holds 256 stable-address values
  in a structure-of-arrays layout with separate previous/next byte-offset
  tables. Zero-sentinel circular offsets link live slots. End insertion,
  iterator erasure, and exact size checks are constant-time; `try_insert`
  reuses a suitable physical gap without allocating a middle block and reports
  refusal with `std::nullopt`.
- `include/function_ref.h`: independently implemented a two-word, non-owning
  callable view without function-pointer-to-`void*` conversion and with
  temporary-functor rejection.
- `include/fixed_string.h`: replaced the unsafe `oss_string` edition with an
  explicitly capacity-based, null-terminated, truncating fixed string and
  standard content hash.
- `include/scope_exit.h`: added a move-safe/dismissible scope guard.
- `include/unicode_literal.h`: extracted the useful native narrow/wide literal
  conversion without the differing off-by-one behavior of old `header_utils`.
- `include/ip_routing.h`: promoted saf-lep's dependency-free IPv4/IPv6 packet
  inspection, address parsing, subnet-broadcast and peer-binding helpers.
- `include/base64.h`: rewrote SAF-AVTTS decoding with strict quartet, alphabet,
  padding and trailing-bit validation and no project exception dependency.
- `include/url.h`: rewrote SAF-AVTTS URL parsing/encoding with query-only target,
  fragment, bracketed IPv6, numeric port and port-range handling.
- `include/buffered_file_reader.h` and `include/memory_mapped_file_reader.h`:
  replaced the six unsafe `bbb_ffio` editions with separate checked RAII
  readers. EOF no longer aliases byte zero; empty files, failed opens, exact
  chunk boundaries, checked seek/reopen, move ownership and idempotent close
  are covered by regression tests. Both accept `std::filesystem::path`, whose
  native character type is wide on Windows under MSVC and MinGW; a UTF-8-derived
  Cyrillic filename is covered without an ANSI-path fallback.
- `include/background_worker.h`: replaced both archived workers with a
  `std::jthread` FIFO executor. Drain and cancel shutdown are distinct, pending
  cancellation is counted, active work receives a cooperative stop token,
  post-stop and empty submissions are reported, move-only tasks are accepted,
  and task/handler exceptions cannot terminate the worker. The tag singleton
  remains available without the old dangling-reference registry.
- `include/long_uint.h`: independently rewrote the recursive editions as a
  fixed-width little-endian limb integer (`long_uint<0>` is 128-bit, and each
  degree doubles the width). Arithmetic is explicitly modulo the width;
  divide-by-zero throws, oversized shifts produce zero, and decimal parsing
  rejects invalid input and overflow.
- `include/math_utils.h`: replaced the legacy macro-heavy approximations with
  corrected sign traits, checked absolute value, exponentiation by squaring,
  exact nonnegative floor integer roots (including `long_uint`), and standard-library
  runtime floating square-root/power behavior. Negative-base integer powers now
  apply sign by exponent parity.
- `include/matrix.h` and `include/sq_matrix.h`: independently rewrote the
  dynamic and fixed-square matrix editions around one `(row, column)` contract.
  Dynamic storage is always rectangular, checked access and incompatible shapes
  throw, equality includes both dimensions, and finite `minmax` initializes from
  an actual element. Fixed transpose swaps only the upper triangle, both power
  paths handle negative and minimum signed exponents without overflow, and
  singular inverse/solve operations report failure instead of returning zeros.
- `include/polyline_converter.h`: replaced the three implicit-policy editions
  with widened linear interpolation and explicit reject, clamp or linear
  extrapolation. Empty and invalid queries return `std::optional`, duplicate
  keys report inserted/replaced/unchanged status, non-finite points are
  rejected, and checked integral conversion exposes its rounding mode. Generic
  lookup tables retain invalid entries; MIDI 14-bit callers may materialize
  `0x4000` deliberately instead of aliasing invalid input to slot `0x2000`.
- `include/context_path.h`, `include/context_path_serializer.h`, and
  `include/linked_context_wrapper.h`: promoted the already-`mctx`-adapted
  SAF-MTQ `ContextPath` without a redundant context abstraction. The 705-line
  header and 1,716-line source were separated into traversal/query, parser, and
  linked-view responsibilities plus bounded implementation chunks. Empty paths,
  checked indices, array erasure, sequential variable binding, signed/unsigned
  query equality, escaped parsing, malformed-input rejection, safe null access,
  link cycles and rendered diff forwarding now have explicit behavior. The
  original `<ContextPath.h>` spelling remains as a compatibility umbrella.
- `include/on_destroy_executor.h`: fixed moved-from double execution, added
  dismissal, and made move assignment dispose its existing action.
- `include/spoilable_future.h`: fixed the self-referential `using status =
  status` alias.
- `mctx`: retained the newer destination edition and fixed issues revealed by
  comparison with DeepMidi: signed/unsigned integral representation and JSON
  round-trip, vector construction, numeric-string parsing, range erase with
  mutable/const iterators, and signed-type traits.

## Priority queue

### P0: provenance and safety boundaries

1. The user confirmed that their private/experimental repositories are fully in
   scope; lack of a root license is not a blocker for their original code.
2. Keep the six raw function-wrapper editions outside the root Unlicense. Their
   non-owning core derives from a 2017 CC BY-SA 3.0 Stack Overflow answer.
3. Do not import embedded third-party B-tree, SimpleJSON, WinReg, Raylib or
   proof-of-concept encryption code as utility-owned source.

### Remaining: useful but coupled or lower confidence

- Split the remaining SAF-MTQ MetaSDK utilities, UUID/timestamp, packed-index,
  queue and bit helpers by dependency and responsibility.
- Rewrite DeepMidi expression parsing; its current precedence condition is
  unreachable and its shared evaluator state is not thread-safe.
- Revisit SAF-AVTTS UUID generation and other Boost-coupled helpers after the
  now-promoted packet routing, Base64 and URL utilities.
- Revisit bitreverse `counted_ptr` as an explicitly single-threaded pointer
  after pool lifetime/debug-diagnostic work and deep-chain regression coverage.

### Deferred from the current consolidation

- MemoryObserver process-memory/view support remains archived. The user removed
  it from the active migration queue for now; no root Windows target is planned.

## Repositories without a promoted candidate

AndromedaRust, extract_data, glsl-experiments, logs, public, scan.sh,
study.develtop.ru, stuff, Wavelet.samp2mid, dumblaucnher, SAFMHE, SAFSYNCore,
WorldWars, wavelet, and most of MIDIModder/TMIDI-fork. These were empty of C++,
used another language, contained generated/reverse-engineered code, were
domain-specific, or had helpers too unsafe/coupled to copy.

## Validation contract

The root build covers promoted headers and repaired legacy utilities. Archived
editions under `versions/` are intentionally not compiled and must not be
described as validated production code.

The promoted file readers compile and pass their Unicode-path runtime test with
GCC 16.1.0/MinGW and the Visual Studio 2026 MSVC 14.51 toolset. The MSVC reader
probe was compiled independently of the root `mctx_json` target because that
legacy target currently obtains `nlohmann/json.hpp` from a MinGW-only include
tree.

The Boost.Multiprecision numerical regression covers 1,500 randomized 128-bit
and 500 randomized 256-bit arithmetic cases, 20,000 randomized native integer
square/cube roots, and 100 randomized 128-bit roots. It is built when
`boost/multiprecision/cpp_int.hpp` is available. A separate Visual Studio 2026
MSVC 14.51 `/W4 /WX /permissive-` probe compiles and runs the promoted worker,
`long_uint`, and integer-root APIs without relying on the MinGW-only Boost
installation.

The independent matrix reference suite covers 2,000 randomized dynamic
rectangular arithmetic/transpose cases, 750 dynamic power/determinant cases,
500 dynamic inverse/solve cases, 2,000 fixed-size arithmetic/transpose/power/
determinant cases across dimensions one through four, and 500 fixed inverse/
solve cases. Determinants are checked against a separate recursive cofactor
implementation rather than the promoted elimination algorithm. The matrix
headers also compile and run in an isolated Visual Studio 2026 MSVC 14.51
`/W4 /WX /permissive-` probe.

The polyline regression suite covers 90,000 independently calculated randomized
queries across reject, clamp and linear extrapolation, plus exact-point,
single/empty-curve, duplicate-key, unsigned intermediate, rounding, narrowing
and byte/MIDI lookup-table boundaries. Its isolated Visual Studio 2026 MSVC
14.51 `/W4 /WX /permissive-` probe also compiles and runs successfully.

The `buffered_block_list` regression covers block rollover, mixed front/back
growth, bidirectional traversal, stable addresses, in-block and cross-block gap
reuse, move-only values, construction failure, destruction, and 12,000
deterministic mixed operations against a vector model. It passes with strict
GCC/MinGW warnings and in an isolated Visual Studio 2026 MSVC 14.51
`/W4 /WX /permissive- /utf-8` build.

The `context_path_test` suite covers empty/key/index/root traversal, canonical
escaping and malformed parser input, wildcard any/all traversal, sequential
bindings, predicate and multiquery selection, signed/unsigned zero equality,
presence of null fields, array/query removal, linked navigation/rendering,
single and array diff forwarding, clone isolation and reference-cycle rejection.
It passes directly under MinGW/GCC and in an isolated Visual Studio 2026 MSVC
14.51 `/W4 /WX /permissive- /utf-8` build with `mctx.cpp`. The full CTest driver
still encounters the known MinGW DLL-loader `0xc0000135` boundary on this
machine; all seven binaries pass when executed directly with the MinGW runtime
on `PATH`.
