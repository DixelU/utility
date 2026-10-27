# bitforce (C++)

The C++ executable supports Bitcoin mainnet legacy P2PKH (`1...`, Base58Check)
and native SegWit P2WPKH (`bc1q...`, 42 characters, Bech32).
These are distinct addresses for the same private key. P2WSH, P2SH, Taproot
(`bc1p...`, Bech32m), and testnet addresses are not supported.

## Configuration

Edit `namespace config` near the top of `bitforce.cpp`, then rebuild:

- `target_addresses`: accepts a mixture of P2PKH and P2WPKH addresses. Addresses
  are validated, including checksums, before searching. Bech32 accepts all
  lowercase or all uppercase input; mixed case is rejected.
- `compressed`: chooses the public-key encoding for **Base58 only**. The existing
  default is `false`. Bech32 always derives from the compressed public key.
- `dump_format`: `address_format::base58`, `address_format::bech32`, or
  `address_format::both` (default). Used when `target_addresses` is empty.
- `missing_bits` and `batch_size`: retain their existing search behavior.

With an empty target list, output goes to `candidate_addresses.txt` as
`address - public key - private key (hex)`. `both` emits two rows per key,
Base58 first and Bech32 second, each with the corresponding public-key encoding.
With targets, the first match is reported with its actual address, public key,
and matching WIF compression flag.

The key walker is shared. Compressed P2PKH and P2WPKH reuse the same HASH160;
uncompressed P2PKH plus P2WPKH needs two hashes of different public-key encodings.
Search mode hashes only the forms needed by the target list and encodes addresses
only when reporting a match. Each lookup first tests a bitmap over the targets'
leading hash bits (about 32 bits per target, 8 KB minimum), so most misses never
reach the binary search; large `--addr_file` lists cost about 4 bytes per address
on top of the 20-byte hashes.

## Derive both addresses for one key

`--derive` accepts exactly 64 hexadecimal digits and requires `1 <= key < n`,
where `n` is the secp256k1 group order. It prints both formats without searching
or creating a candidate file. Base58 uses `config::compressed`.

```powershell
.\cmake-build-release\bitforce.exe --derive 0000000000000000000000000000000000000000000000000000000000000001
```

For this public test key, the default uncompressed Base58 address is
`1EHNa6Q4Jz2uvNExL497mE43ikXhwF6kZm` and the Bech32 address is
`bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4`.

Run without arguments to use the configured search/dump behavior. The Python
implementation and Python worker launchers remain separate from this executable.

## Build and test

Requires CMake 4.3+, a C++23 compiler with `unsigned __int128` (GCC or Clang),
OpenSSL 3 Crypto, and `include/long_uint.h` from the enclosing repository. For
an already configured build directory:

```powershell
cmake --build cmake-build-release
ctest --test-dir cmake-build-release --output-on-failure
```

For a new directory, configure with `cmake -S . -B build` first; set
`OPENSSL_ROOT_DIR` if needed. On Windows, the OpenSSL and compiler runtime DLLs
must be on `PATH` or beside the executable. Tests are enabled by default;
`-DBUILD_TESTING=OFF` omits the test executable.

Tests cover secp256k1 field arithmetic against OpenSSL, the
[BIP173](https://bips.dev/173/) P2WPKH vector, invalid and
unsupported address forms including [BIP350](https://bips.dev/350/) cases,
Base58/WIF regression vectors, mixed target matching, both dump formats,
and key-walker output against OpenSSL scalar multiplication.
