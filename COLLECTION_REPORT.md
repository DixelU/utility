# C++ utility consolidation report

Date: 2026-08-07

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

### P1: next implementations worth doing

1. Validate both matrix APIs with randomized reference tests before promotion.
   Known high-value fixes include dynamic-matrix dimension comparison/minmax
   initialization and fixed matrix transpose/power defects.
2. Extract generic `polyline_converter` only after defining interpolation,
   extrapolation and invalid-result policy; the old 14-bit fallback changed
   from `0x2000` to invalid `0x4000` between editions.
3. Package MemoryObserver process-memory support as an optional Windows target
   after licensing is resolved.

### P2: useful but coupled or lower confidence

- Split SAF-MTQ `ContextPath`, MetaSDK utilities, UUID/timestamp, packed-index,
  queue and bit helpers by dependency and responsibility.
- Rewrite DeepMidi expression parsing; its current precedence condition is
  unreachable and its shared evaluator state is not thread-safe.
- Revisit SAF-AVTTS UUID generation and other Boost-coupled helpers after the
  now-promoted packet routing, Base64 and URL utilities.
- Revisit bitreverse `counted_ptr` as an explicitly single-threaded pointer
  after pool lifetime/debug-diagnostic work and deep-chain regression coverage.

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
