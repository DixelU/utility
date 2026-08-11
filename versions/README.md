# Collected source editions

This directory preserves materially different utility implementations found in
the scanned repositories on 2026-08-07. It is a source archive, not a public
include tree. Nothing below `versions/` is compiled by the root CMake project.

The repository-root Unlicense applies to the utility project's own code only.
Archived editions retain their original provenance and license. License copies
available in the scanned repositories are under `versions/licenses/`:

- SAFC, SAFOR, xSYS-SAFC and the SAFC_v1/v2 line: Apache-2.0.
- SAF-MTQ-base: MIT, copyright Alexander Verevkin.
- TMIDI-fork: MIT.
- The Stack Overflow-derived portion of the function-wrapper family: CC BY-SA
  3.0; see `function_wrapper/NOTICE.md`.
- The user confirmed that their private/experimental repositories (including
  DeepMidiDiagnostics, MemoryObserver, SAF-AVTTS, saf-lep, SAFMHE, WorldWars,
  custom_math and bitreverse) may be consolidated based on technical merit.

## Families and chosen direction

| Family | Preserved editions | Canonical action |
| --- | ---: | --- |
| `mctx` | DeepMidi legacy | Keep the newer root implementation; regression fixes were merged and tested. |
| `spoilable_future` | SAF-MTQ utils | Keep the root namespace/API and merge only the corrected status alias. |
| `buffered_queue_spsc` | SAFC_v1, bitreverse | Root header merges configurable slabs/emplace with const access and safer initialization. |
| `buffered_object_pool` | bitreverse | Root header promotes a documented, dependency-free edition. |
| function wrapper/reference | 6 | Raw editions stay archived for license/history; root `function_ref` is independently implemented. |
| `bbb_ffio` file readers | 6 | Rewritten as checked root RAII buffered and mapped readers; raw editions remain archive-only. |
| `header_utils` | 6 | Split by responsibility; scope guard and native literal were promoted separately. |
| allocator | 2 | Archive only; prefer a modern `std::pmr`-compatible design. |
| background worker | 2 generations | Rewritten as a root `std::jthread` worker with explicit drain/cancel and exception contracts. |
| integer/math | current plus legacy | Independently rewritten and promoted after Boost/reference tests; raw editions remain archive-only. |
| matrix | dynamic plus fixed | Independently rewritten as root dynamic and fixed-square headers; randomized reference tests cover dimensions, arithmetic, transpose, power, determinant, inverse and solve. |
| fixed stack string | SAF-MTQ | Root `fixed_string` repairs capacity/length/hash issues. |
| thread pool | SAF-MTQ | Archive only; current version has locking and dependency problems. |
| polyline interpolation | SAFC_v1, SAFC_v2, xSYS | Independently rewritten with explicit extrapolation, rounding and invalid-entry policies plus randomized reference tests. |
| process memory/view | MemoryObserver | Archive only; deferred from the active consolidation with no root target planned for now. |
| networking helpers | saf-lep, SAF-AVTTS | Packet routing was promoted; Base64 and URL helpers were rewritten and promoted; raw editions remain for comparison. |
| `ContextPath` and linked contexts | SAF-MTQ | Promoted as direct `mctx` utilities, split across traversal, serialization and linked-wrapper units; the raw pair remains archived. |
| MetaSDK misc/UUID/timestamp | SAF-MTQ | Archive as MIT candidates; split coupled helpers before promotion. |
| expression evaluator/logger | DeepMidi | Archive for rewrite; current editions have correctness/threading concerns. |
| JSON candidates | operator-pp, SAF-AVTTS | Archive only; keep dependencies and license boundaries explicit. |

Exact duplicate clones were not copied twice. For example, both
DeepMidiDiagnostics checkouts are byte-identical; `mctx_json.h/.cpp` are also
byte-identical in DeepMidiDiagnostics, SAF-MTQ-base and the destination.

## Source-state note

`custom_math/math_utils.h` had uncommitted working-tree changes when collected.
The archived copy is intentionally the working-tree edition, which adds the
new integer-root path/seeding work. The source repository was not modified.
