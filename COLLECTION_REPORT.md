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

1. Replace the six `bbb_ffio` editions with separate RAII buffered-file and
   memory-mapped readers. Cover empty/failed open, exact chunk boundaries,
   seek/reopen, EOF and close ownership.
2. Rewrite `background_worker` around `std::jthread`, explicit drain versus
   cancel shutdown, exception handling, push-after-stop reporting, and no
   global dangling-reference registry.
3. Validate `long_uint`, `math_utils`, and both matrix APIs with randomized
   Boost.Multiprecision/reference tests before promotion. Known high-value fixes
   include dynamic-matrix dimension comparison/minmax initialization and fixed
   matrix transpose/power defects.
4. Extract generic `polyline_converter` only after defining interpolation,
   extrapolation and invalid-result policy; the old 14-bit fallback changed
   from `0x2000` to invalid `0x4000` between editions.
5. Package MemoryObserver process-memory support as an optional Windows target
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
