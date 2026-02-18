import hashlib
from tqdm import tqdm
import random

# Try to use coincurve for speed (highly recommended: pip install coincurve)
# Fallback to ecdsa if not available (pip install ecdsa) – much slower for 1M+ candidates
try:
    from coincurve import PrivateKey

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

# ----------------------------- Main Script -----------------------------
print("Bitcoin partial private key brute-forcer")
print("Assumes legacy P2PKH addresses (mainnet).")

missing_bits = 20
compressed = False
target_address = "00f8753559cd673046044baf06725c7a94bcb8a592f9729077"
# input("Enter target address if known (optional – enables early stop): ").strip() or None

base_int =    random_256int()
num_candidates = 1 << missing_bits
curve_order = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141

print(f"\nBrute-forcing {num_candidates:,} candidates...")
print(f"\nBase int: {base_int:064x}")

found_key = None
if target_address:
    # Mode 1: Search for specific address → fully offline, stops when found
    for i in tqdm(range(num_candidates)):
        priv_int = base_int ^ i
        if priv_int == 0 or priv_int >= curve_order:
            continue
        pub = get_public_key(priv_int, compressed)
        addr = pub_to_address(pub)
        if addr == target_address:
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
            f.write(addr + '\n')
    print(f"\nDone! {num_candidates:,} candidate addresses saved to '{filename}'.")
    print("You can now check these addresses online (e.g. via block explorer API) for balances.")

# print("\nWarning: Handle any recovered keys with extreme care – never expose them online.")