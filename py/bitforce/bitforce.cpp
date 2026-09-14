#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <random>
#include <chrono>
#include <sstream>

#pragma warning(disable : 4996)
#define OPENSSL_API_COMPAT 0x10100000L

#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")

// ---------------------------------------------------------------------------
// Base58 alphabet (Bitcoin)
// ---------------------------------------------------------------------------
static const char* BASE58_ALPHABET =
"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string base58_encode(const unsigned char* data, size_t len)
{
        // Convert bytes to a big integer (as a vector of digits in base 256)
        std::vector<unsigned char> digits(data, data + len);

        // Count leading zeros
        size_t leading_zeros = 0;
        while (leading_zeros < len && data[leading_zeros] == 0)
                ++leading_zeros;

        // Convert to base 58
        std::vector<unsigned char> result;
        while (!digits.empty() && !(digits.size() == 1 && digits[0] == 0))
        {
                int carry = 0;
                std::vector<unsigned char> new_digits;
                for (unsigned char d : digits)
                {
                        int cur = carry * 256 + d;
                        carry = cur % 58;
                        int quot = cur / 58;
                        if (!new_digits.empty() || quot != 0)
                                new_digits.push_back(static_cast<unsigned char>(quot));
                }
                result.push_back(static_cast<unsigned char>(carry));
                digits = std::move(new_digits);
        }

        // Leading zeros → '1'
        std::string encoded(leading_zeros, '1');
        for (auto it = result.rbegin(); it != result.rend(); ++it)
                encoded += BASE58_ALPHABET[*it];

        return encoded;
}

// ---------------------------------------------------------------------------
// SHA-256 / RIPEMD-160 helpers
// ---------------------------------------------------------------------------
void sha256(const unsigned char* data, size_t len, unsigned char* out)
{
        SHA256(data, len, out);
}

void ripemd160(const unsigned char* data, size_t len, unsigned char* out)
{
        RIPEMD160(data, len, out);
}

// Double SHA-256 (for checksums)
void double_sha256(const unsigned char* data, size_t len, unsigned char* out)
{
        unsigned char tmp[SHA256_DIGEST_LENGTH];
        sha256(data, len, tmp);
        sha256(tmp, SHA256_DIGEST_LENGTH, out);
}

// ---------------------------------------------------------------------------
// secp256k1 helpers (OpenSSL)
// ---------------------------------------------------------------------------
EC_KEY* create_secp256k1_key()
{
        EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
        if (!key)
        {
                std::cerr << "Failed to create EC_KEY\n";
                exit(1);
        }
        return key;
}

// priv_int is a 256-bit integer represented as 32 big-endian bytes
bool get_public_key(const unsigned char priv_bytes[32], bool compressed,
        std::vector<unsigned char>& pub_out)
{
        EC_KEY* key = create_secp256k1_key();
        BIGNUM* priv_bn = BN_bin2bn(priv_bytes, 32, nullptr);
        if (!priv_bn)
        {
                EC_KEY_free(key);
                return false;
        }

        if (EC_KEY_set_private_key(key, priv_bn) != 1)
        {
                BN_free(priv_bn);
                EC_KEY_free(key);
                return false;
        }

        // Generate public key
        const EC_GROUP* group = EC_KEY_get0_group(key);
        EC_POINT* pub_point = EC_POINT_new(group);
        if (!EC_POINT_mul(group, pub_point, priv_bn, nullptr, nullptr, nullptr))
        {
                EC_POINT_free(pub_point);
                BN_free(priv_bn);
                EC_KEY_free(key);
                return false;
        }
        EC_KEY_set_public_key(key, pub_point);

        // Serialize
        point_conversion_form_t form = compressed ? POINT_CONVERSION_COMPRESSED
                : POINT_CONVERSION_UNCOMPRESSED;
        size_t len = EC_POINT_point2oct(group, pub_point, form, nullptr, 0, nullptr);
        pub_out.resize(len);
        EC_POINT_point2oct(group, pub_point, form, pub_out.data(), len, nullptr);

        EC_POINT_free(pub_point);
        BN_free(priv_bn);
        EC_KEY_free(key);
        return true;
}

// ---------------------------------------------------------------------------
// P2PKH address (mainnet, starts with '1')
// ---------------------------------------------------------------------------
std::string pub_to_address(const std::vector<unsigned char>& pub)
{
        unsigned char sha[SHA256_DIGEST_LENGTH];
        sha256(pub.data(), pub.size(), sha);

        unsigned char ripe[RIPEMD160_DIGEST_LENGTH];
        ripemd160(sha, SHA256_DIGEST_LENGTH, ripe);

        unsigned char extended[1 + RIPEMD160_DIGEST_LENGTH + 4];
        extended[0] = 0x00;                         // mainnet version
        memcpy(extended + 1, ripe, RIPEMD160_DIGEST_LENGTH);

        unsigned char checksum[SHA256_DIGEST_LENGTH];
        double_sha256(extended, 1 + RIPEMD160_DIGEST_LENGTH, checksum);
        memcpy(extended + 21, checksum, 4);

        return base58_encode(extended, 25);
}

// ---------------------------------------------------------------------------
// WIF (Wallet Import Format)
// ---------------------------------------------------------------------------
std::string priv_to_wif(const unsigned char priv_bytes[32], bool compressed)
{
        size_t len = compressed ? 34 : 33;          // 1 + 32 + (1) + 4
        std::vector<unsigned char> extended(len);
        extended[0] = 0x80;                         // mainnet
        memcpy(extended.data() + 1, priv_bytes, 32);
        if (compressed)
                extended[33] = 0x01;

        unsigned char checksum[SHA256_DIGEST_LENGTH];
        double_sha256(extended.data(), compressed ? 34 : 33, checksum);
        // overwrite the last 4 bytes with checksum
        if (compressed)
                memcpy(extended.data() + 34, checksum, 4);
        else
        {
                extended.resize(37);
                memcpy(extended.data() + 33, checksum, 4);
        }

        return base58_encode(extended.data(), extended.size());
}

// ---------------------------------------------------------------------------
// Random 256-bit integer (as 32 bytes, big-endian)
// ---------------------------------------------------------------------------
void random_256bytes(unsigned char out[32])
{
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;

        for (int i = 0; i < 4; ++i)
        {
                uint64_t v = dist(gen);
                for (int j = 0; j < 8; ++j)
                        out[i * 8 + j] = static_cast<unsigned char>((v >> (56 - j * 8)) & 0xFF);
        }
}

// Convert 32-byte big-endian to hex string
std::string bytes_to_hex(const unsigned char* data, size_t len)
{
        std::ostringstream oss;
        for (size_t i = 0; i < len; ++i)
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        return oss.str();
}

// ---------------------------------------------------------------------------
// Progress bar (simple replacement for tqdm)
// ---------------------------------------------------------------------------
void print_progress(uint64_t current, uint64_t total)
{
        const int bar_width = 40;
        double progress = static_cast<double>(current) / total;
        int pos = static_cast<int>(bar_width * progress);

        std::cout << "\r[";
        for (int i = 0; i < bar_width; ++i)
        {
                if (i < pos) std::cout << "=";
                else if (i == pos) std::cout << ">";
                else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1)
                << (progress * 100.0) << "%  (" << current << "/" << total << ")"
                << std::flush;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
        std::cout << "Bitcoin partial private key brute-forcer (C++)\n"
                << "Assumes legacy P2PKH addresses (mainnet).\n";

        const int missing_bits = 20;
        const bool compressed = false;          // match Python default
        // Note: the original Python target looked like a hex string, not a Base58 address.
        // Keep the same value for behavioural compatibility.
        const std::string target_address =
                "0062e907b15cbf27d5425399ebf6f0fb50ebb88f18c29b7d93";

        const uint64_t num_candidates = 1ULL << missing_bits;
        const char* curve_order_hex =
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141";

        // Curve order as BIGNUM for range checks
        BIGNUM* curve_order = nullptr;
        BN_hex2bn(&curve_order, curve_order_hex);

        // Generate random base private key (32 bytes)
        unsigned char base_bytes[32];
        random_256bytes(base_bytes);

        std::cout << "\nBrute-forcing " << num_candidates << " candidates...\n"
                << "Base int: " << bytes_to_hex(base_bytes, 32) << "\n";

        auto start = std::chrono::steady_clock::now();

        if (!target_address.empty())
        {
                // Mode 1: search for specific address (XOR)
                bool found = false;
                for (uint64_t i = 0; i < num_candidates; ++i)
                {
                        if ((i & 0x3FFF) == 0)          // update progress every 16k
                                print_progress(i, num_candidates);

                        // priv = base XOR i  (i is treated as a 256-bit value, low bits only)
                        unsigned char priv_bytes[32];
                        memcpy(priv_bytes, base_bytes, 32);
                        // XOR the lowest 8 bytes with i (enough for missing_bits <= 64)
                        uint64_t* low = reinterpret_cast<uint64_t*>(priv_bytes + 24);
                        *low ^= i;                      // note: endianness – works on little-endian hosts

                        // Skip invalid keys
                        BIGNUM* priv_bn = BN_bin2bn(priv_bytes, 32, nullptr);
                        if (BN_is_zero(priv_bn) || BN_cmp(priv_bn, curve_order) >= 0)
                        {
                                BN_free(priv_bn);
                                continue;
                        }
                        BN_free(priv_bn);

                        std::vector<unsigned char> pub;
                        if (!get_public_key(priv_bytes, compressed, pub))
                                continue;

                        std::string addr = pub_to_address(pub);
                        if (addr == target_address)
                        {
                                std::string priv_hex = bytes_to_hex(priv_bytes, 32);
                                std::string wif = priv_to_wif(priv_bytes, compressed);

                                std::cout << "\n\n=== FOUND MATCH ===\n"
                                        << "Private key (hex): " << priv_hex << "\n"
                                        << "Private key (WIF): " << wif << "\n"
                                        << "Address:           " << addr << "\n";
                                found = true;
                                break;
                        }
                }
                print_progress(num_candidates, num_candidates);
                std::cout << "\n";

                if (!found)
                {
                        std::cout << "No match found in the search space.\n";
                        BN_free(curve_order);
                        return 1;
                }
        }
        else
        {
                // Mode 2: generate all candidate addresses (OR)
                std::ofstream out("candidate_addresses.txt");
                out << "# List of possible Bitcoin addresses (one per line)\n";

                for (uint64_t i = 0; i < num_candidates; ++i)
                {
                        if ((i & 0x3FFF) == 0)
                                print_progress(i, num_candidates);

                        unsigned char priv_bytes[32];
                        memcpy(priv_bytes, base_bytes, 32);
                        uint64_t* low = reinterpret_cast<uint64_t*>(priv_bytes + 24);
                        *low |= i;

                        BIGNUM* priv_bn = BN_bin2bn(priv_bytes, 32, nullptr);
                        if (BN_is_zero(priv_bn) || BN_cmp(priv_bn, curve_order) >= 0)
                        {
                                BN_free(priv_bn);
                                continue;
                        }
                        BN_free(priv_bn);

                        std::vector<unsigned char> pub;
                        if (!get_public_key(priv_bytes, compressed, pub))
                                continue;

                        out << pub_to_address(pub) << "\n";
                }
                print_progress(num_candidates, num_candidates);
                std::cout << "\nDone! " << num_candidates
                        << " candidate addresses saved to 'candidate_addresses.txt'.\n";
        }

        auto end = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "Elapsed: " << std::fixed << std::setprecision(2)
                << elapsed << " s\n";

        BN_free(curve_order);
        return 0;
}