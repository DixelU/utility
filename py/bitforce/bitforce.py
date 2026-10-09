import hashlib
from tqdm import tqdm
import random
import base58

# Try to use coincurve for speed (highly recommended: pip install coincurve)
# Fallback to ecdsa if not available (pip install ecdsa) – much slower for 1M+ candidates
try:
    from coincurve import PrivateKey, PublicKey
    use_coincurve = True
    print("Using fast coincurve library.")
except ImportError:
    raise ImportError("Please install either 'coincurve' (recommended) or 'ecdsa'.")


# Simple Base58 encoder (no extra dependency needed)
def base58_encode(data: bytes) -> str:
    alphabet = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
    base = 58
    num = int.from_bytes(data, 'big')
    encoded = ''
    while num > 0:
        num, rem = divmod(num, base)
        encoded = alphabet[rem] + encoded
    # Leading zeros
    for byte in data:
        if byte != 0:
            break
        encoded = '1' + encoded
    return encoded

# Derive public key from private key integer
def get_public_key(priv_int: int, compressed: bool = True) -> bytes:
    priv_hex = f"{priv_int:064x}"
    priv_bytes = bytes.fromhex(priv_hex)

    priv = PrivateKey(priv_bytes)
    return priv.public_key.format(compressed=compressed)

# Derive legacy P2PKH address (mainnet, starts with '1')
def pub_to_address(pub: bytes) -> str:
    sha = hashlib.sha256(pub).digest()
    ripe = hashlib.new('ripemd160', sha).digest()
    extended = b'\x00' + ripe
    checksum = hashlib.sha256(hashlib.sha256(extended).digest()).digest()[:4]
    return base58_encode(extended + checksum)


# Optional: Derive WIF from private key hex
def priv_to_wif(priv_hex: str, compressed: bool = True) -> str:
    priv_bytes = bytes.fromhex(priv_hex)
    prefix = b'\x80'  # mainnet
    suffix = b'\x01' if compressed else b''
    extended = prefix + priv_bytes + suffix
    checksum = hashlib.sha256(hashlib.sha256(extended).digest()).digest()[:4]
    return base58_encode(extended + checksum)

def random_256int() -> int:
    return random.getrandbits(256) ^ random.getrandbits(256)

def search_range(base_int, start, end, compressed, target_addresses):
    """Search priv_int in [base_int + start, base_int + end)."""
    k_min = max(base_int + start, 1)
    k_max = min(base_int + end, curve_order)
    if k_min >= k_max:
        return None

    # ONE scalar multiplication for the whole chunk
    Q = PrivateKey(k_min.to_bytes(32, 'big')).public_key

    k = k_min
    while k < k_max:
        pub_bytes = Q.format(compressed=compressed)
        addr = pub_to_address(pub_bytes)
        if addr in target_addresses:
            return (addr, pub_bytes, k)

        # Advance to (k+1)·G — cheap point addition
        Q = PublicKey.combine_keys([Q, _G])
        k += 1

    return None

# ----------------------------- Main Script -----------------------------
print("Bitcoin partial private key brute-forcer")
print("Assumes legacy P2PKH addresses (mainnet).")

# Generator point G (built once)
_G = PrivateKey((1).to_bytes(32, 'big')).public_key

missing_bits = 24
compressed = False

target_address = frozenset([
    "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa",
    "1PeizMg76Cf96nUQrYg8xuoZWLQozU5zGW",
    "1K6KoYC69NnafWJ7YgtrpwJxBLiijWqwa6"
])

# target_address = None

base_int =    random_256int() & ~((1 << missing_bits) - 1)
num_candidates = 1 << missing_bits
curve_order = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141

print(f"\nBrute-forcing {num_candidates:,} candidates...")
print(f"\nBase int: {base_int:064x}")

found_key = None
if target_address:
    # Mode 1: Search for specific address → fully offline, stops when found
    
    res = search_range(base_int, 1, num_candidates, compressed, target_address)

    if res:
        (addr, pub_bytes, priv_int) = res

        priv_hex = f"{priv_int:064x}"
        wif = priv_to_wif(priv_hex, compressed)
        
        print("\n=== FOUND MATCH ===")
        print(f"Private key (hex): {priv_hex}")
        print(f"Private key (WIF): {wif}")
        print(f"Address:           {addr}")
        found_key = priv_hex

        exit(0)

    if not found_key:
        print("\nNo match found in the search space.")
        exit(1)
else:
    # Mode 2: Generate all possible addresses → save to file for later online balance checks
    filename = "candidate_addresses.txt"
    with open(filename, 'w') as f:
        f.write("# List of possible Bitcoin addresses (one per line)\n")
        for i in tqdm(range(num_candidates)):
            priv_int = base_int | i
            if priv_int == 0 or priv_int >= curve_order:
                continue

            pub = get_public_key(priv_int, compressed)
            addr = pub_to_address(pub)
            f.write(f"{addr} - {pub} - {priv_int}\n")

    print(f"\nDone! {num_candidates:,} candidate addresses saved to '{filename}'.")
    print("You can now check these addresses online (e.g. via block explorer API) for balances.")