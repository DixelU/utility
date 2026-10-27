#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The low-level SHA256_CTX / RIPEMD160 API is deprecated, but unlike the one-shot SHA256() (which
// fetches the algorithm by name and allocates an EVP context on every call) it never allocates.
#define OPENSSL_SUPPRESS_DEPRECATED

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

#include "long_uint.h"

namespace
{

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
enum class address_format { base58, bech32, both };

namespace config
{

constexpr int missing_bits = 28;
constexpr bool compressed = false; // Base58 only; Bech32 always uses compressed public keys.
constexpr auto dump_format = address_format::both;

// Mainnet P2PKH (1...) and/or P2WPKH (bc1q..., 42 characters).
// Leave empty to dump every candidate in dump_format to candidate_addresses.txt instead.
constexpr std::initializer_list<std::string_view> target_addresses = {
	"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa",
	"1PeizMg76Cf96nUQrYg8xuoZWLQozU5zGW",
	"1K6KoYC69NnafWJ7YgtrpwJxBLiijWqwa6",
	"1FeexV6bAHb8ybZjqQMjJrcCrHGW9sb6uF",
	"1LdRcdxfbSnmCYYNdeYpUnztiYzVfBEQeC",
	"12ib7dApVFvg82TXKycWBNpN8kFyiAN1dr",
	"12tkqA9xSoowkzoERHMWNKsTey55YEBqkv",
	"1HLvaTs3zR3oev9ya7Pzp3GB9Gqfg6XYJT",
	"167ZWTT8n6s4ya8cGjqNNQjDwDGY31vmHg"
};

// Point additions that share one field inversion.
constexpr std::size_t batch_size = 1024;

static_assert(missing_bits > 0 && missing_bits < 64);

} // namespace config

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
[[noreturn]] void die(const std::string_view what)
{
	std::cerr << "fatal: " << what << '\n';
	std::exit(2);
}

template <auto Free>
struct ossl_deleter
{
	template <typename T>
	void operator()(T* ptr) const noexcept { Free(ptr); }
};

using bn_ptr = std::unique_ptr<BIGNUM, ossl_deleter<BN_free>>;
using bn_ctx_ptr = std::unique_ptr<BN_CTX, ossl_deleter<BN_CTX_free>>;
using ec_group_ptr = std::unique_ptr<EC_GROUP, ossl_deleter<EC_GROUP_free>>;
using ec_point_ptr = std::unique_ptr<EC_POINT, ossl_deleter<EC_POINT_free>>;

bn_ptr new_bn()
{
	bn_ptr bn{BN_new()};
	if (!bn)
		die("BN_new failed");

	return bn;
}

// Writes 2 * data.size() lowercase hex digits to out.
void to_hex(const std::span<const std::uint8_t> data, char* out)
{
	constexpr char digits[] = "0123456789abcdef";
	for (const std::uint8_t byte : data)
	{
		*out++ = digits[byte >> 4];
		*out++ = digits[byte & 0xF];
	}
}

std::string to_hex(const std::span<const std::uint8_t> data)
{
	std::string hex(data.size() * 2, '\0');
	to_hex(data, hex.data());

	return hex;
}

std::uint64_t load_be64(const std::uint8_t* in)
{
	std::uint64_t value = 0;
	for (int i = 0; i < 8; ++i)
		value = (value << 8) | in[i];
	return value;
}

void store_be64(std::uint8_t* out, std::uint64_t value)
{
	for (int i = 7; i >= 0; --i, value >>= 8)
		out[i] = static_cast<std::uint8_t>(value);
}

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------
using sha256_digest = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>;
using hash160 = std::array<std::uint8_t, RIPEMD160_DIGEST_LENGTH>;

sha256_digest sha256(const std::span<const std::uint8_t> data)
{
	SHA256_CTX ctx [[indeterminate]];
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data.data(), data.size());
	sha256_digest out [[indeterminate]];
	SHA256_Final(out.data(), &ctx);
	return out;
}

hash160 hash160_of(const std::span<const std::uint8_t> data)
{
	const sha256_digest sha = sha256(data);
	hash160 out [[indeterminate]];
	RIPEMD160(sha.data(), sha.size(), out.data());
	return out;
}

// ---------------------------------------------------------------------------
// Base58 (Bitcoin)
// ---------------------------------------------------------------------------
constexpr std::string_view base58_alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

// Writes the Base58 encoding of data (at most 46 bytes) to out and returns its length.
std::size_t base58_encode(const std::span<const std::uint8_t> data, char* out)
{
	assert(data.size() <= 46);

	std::array<std::uint8_t, 64> digits [[indeterminate]]; // base-58 digits, least significant first
	std::size_t size = 0;
	for (const std::uint8_t byte : data)
	{
		unsigned carry = byte;
		for (std::size_t i = 0; i < size; ++i)
		{
			carry += unsigned{digits[i]} << 8;
			digits[i] = static_cast<std::uint8_t>(carry % 58);
			carry /= 58;
		}

		for (; carry; carry /= 58)
			digits[size++] = static_cast<std::uint8_t>(carry % 58);
	}

	std::size_t len = 0;
	for (std::size_t i = 0; i < data.size() && data[i] == 0; ++i)
		out[len++] = '1';

	while (size)
		out[len++] = base58_alphabet[digits[--size]];

	return len;
}

// Appends the 4-byte double-SHA256 checksum to payload (at most 34 bytes) and Base58-encodes it.
std::size_t base58check_encode(std::span<const std::uint8_t> payload, char* out)
{
	assert(payload.size() <= 34);

	std::array<std::uint8_t, 38> buf [[indeterminate]];
	const sha256_digest checksum = sha256(sha256(payload));

	std::ranges::copy(payload, buf.begin());
	std::copy_n(checksum.begin(), 4, buf.begin() + payload.size());

	return base58_encode({buf.data(), payload.size() + 4}, out);
}

// Writes the mainnet P2PKH address (at most 34 chars) of a hash160 to out and returns its length.
std::size_t hash160_to_address(const hash160& h, char* out)
{
	std::array<std::uint8_t, 21> payload{}; // version + hash160
	std::ranges::copy(h, payload.begin() + 1);
	return base58check_encode(payload, out);
}

std::string hash160_to_address(const hash160& h)
{
	char out [[indeterminate]] [34];
	return {out, hash160_to_address(h, out)};
}

// Decodes a mainnet P2PKH address, verifying its version byte and checksum.
bool address_to_hash160(std::string_view address, hash160& out)
{
	std::array<std::uint8_t, 25> raw{}; // version + hash160 + checksum

	for (const char c : address)
	{
		const std::size_t digit = base58_alphabet.find(c);
		if (digit == std::string_view::npos)
			return false;

		auto carry = static_cast<unsigned>(digit);
		for (auto it = raw.rbegin(); it != raw.rend(); ++it, carry >>= 8)
		{
			carry += 58u * *it;
			*it = static_cast<std::uint8_t>(carry);
		}

		if (carry)
			return false; // longer than 25 bytes
	}

	// Every leading '1' stands for exactly one leading zero byte.
	const auto ones = std::ranges::find_if(address, [](char c) { return c != '1'; }) - address.begin();
	const auto zeros = std::ranges::find_if(raw, [](std::uint8_t b) { return b != 0; }) - raw.begin();
	if (ones != zeros || raw[0] != 0x00)
		return false;

	const sha256_digest checksum = sha256(sha256(std::span{raw}.first<21>()));
	if (!std::equal(checksum.begin(), checksum.begin() + 4, raw.begin() + 21))
		return false;

	std::copy_n(raw.begin() + 1, out.size(), out.begin());
	return true;
}

std::string private_to_wif(std::span<const std::uint8_t, 32> private_key, const bool compressed)
{
	std::array<std::uint8_t, 34> payload{0x80};
	std::ranges::copy(private_key, payload.begin() + 1);
	payload[33] = 0x01;

	char out [[indeterminate]] [52];
	return {out, base58check_encode({payload.data(), compressed ? 34u : 33u}, out)};
}

// ---------------------------------------------------------------------------
// Bech32 mainnet P2WPKH (BIP173, witness version 0, 20-byte key hash)
// P2WSH and Bech32m/Taproot require different derivation and are not supported.
// ---------------------------------------------------------------------------
constexpr std::string_view bech32_alphabet = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

constexpr std::uint32_t bech32_step(std::uint32_t checksum, std::uint8_t value)
{
	constexpr std::uint32_t generators[] = {
		0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3
	};

	const std::uint32_t top = checksum >> 25;
	checksum = ((checksum & 0x1ffffff) << 5) ^ value;

	for (unsigned i = 0; i < 5; ++i)
	{
		if ((top >> i) & 1)
			checksum ^= generators[i];
	}

	return checksum;
}

constexpr std::uint32_t bech32_mainnet_prefix()
{
	std::uint32_t checksum = 1;

	// HRP expansion of "bc", followed by witness version 0.
	for (const std::uint8_t value : {3, 3, 0, 2, 3, 0})
		checksum = bech32_step(checksum, value);
	return checksum;
}

// Writes exactly 42 lowercase characters, without a terminating NUL.
std::size_t hash160_to_bech32(const hash160& h, char* out)
{
	std::copy_n("bc1q", 4, out);
	std::size_t size = 4;
	std::uint32_t checksum = bech32_mainnet_prefix(), accumulator = 0;

	unsigned bits = 0;
	for (const std::uint8_t byte : h)
	{
		accumulator = (accumulator << 8) | byte;
		bits += 8;
		while (bits >= 5)
		{
			bits -= 5;
			const auto value = static_cast<std::uint8_t>((accumulator >> bits) & 31);
			out[size++] = bech32_alphabet[value];
			checksum = bech32_step(checksum, value);
		}
	}

	// 160 bits fit exactly into 32 base-32 digits: no padding is needed.
	for (unsigned i = 0; i < 6; ++i)
		checksum = bech32_step(checksum, 0);

	checksum ^= 1; // Bech32, not Bech32m.
	for (int shift = 25; shift >= 0; shift -= 5)
		out[size++] = bech32_alphabet[(checksum >> shift) & 31];

	return size;
}

std::string hash160_to_bech32(const hash160& h)
{
	char out [[indeterminate]] [42];
	return {out, hash160_to_bech32(h, out)};
}

bool bech32_to_hash160(const std::string_view address, hash160& out)
{
	// Restrict to mainnet P2WPKH. This also rejects extra padding/data symbols.
	if (address.size() != 42)
		return false;

	std::array<char, 42> normalized [[indeterminate]];
	bool lower = false, upper = false;
	for (std::size_t i = 0; i < address.size(); ++i)
	{
		char c = address[i];
		lower |= c >= 'a' && c <= 'z';
		upper |= c >= 'A' && c <= 'Z';
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		normalized[i] = c;
	}

	if ((lower && upper) || std::string_view{normalized.data(), 4} != "bc1q")
		return false;

	hash160 decoded;
	std::size_t size = 0;
	std::uint32_t checksum = bech32_mainnet_prefix(), accumulator = 0;

	unsigned bits = 0;
	for (std::size_t i = 4; i < normalized.size(); ++i)
	{
		const std::size_t digit = bech32_alphabet.find(normalized[i]);
		if (digit == std::string_view::npos)
			return false;

		checksum = bech32_step(checksum, static_cast<std::uint8_t>(digit));
		if (i < 36) // The last six characters are the checksum.
		{
			accumulator = (accumulator << 5) | static_cast<std::uint32_t>(digit);
			bits += 5;
			if (bits >= 8)
			{
				bits -= 8;
				decoded[size++] = static_cast<std::uint8_t>(accumulator >> bits);
			}
		}
	}

	if (checksum != 1)
		return false;

	out = decoded;
	return true;
}

// Compress an already-derived SEC public key; no extra elliptic-curve work.
std::span<const std::uint8_t> compressed_public_key(
	std::span<const std::uint8_t> pub,
	std::array<std::uint8_t, 33>& storage)
{
	if (pub.size() == 33)
		return pub;

	assert(pub.size() == 65 && pub[0] == 0x04);

	storage[0] = static_cast<std::uint8_t>(0x02 | (pub[64] & 1));
	std::copy_n(pub.begin() + 1, 32, storage.begin() + 1);

	return storage;
}

// A set of hash160s for lookups that nearly always miss. A bitmap indexed by the leading
// bits of the members, with about 32 bits per member, rejects most misses with one memory
// access; only around 1 in 32 goes on to the binary search, which also confirms real hits.
class hash160_set
{
public:
	void insert(const hash160& h) { sorted_.push_back(h); }

	// Call after the last insert and before the first lookup.
	void build()
	{
		std::ranges::sort(sorted_);

		const unsigned log2_bits = std::max(16, std::bit_width(sorted_.size()) + 5);
		shift_ = 64 - log2_bits;
		bitmap_.assign((std::size_t{1} << log2_bits) / 64, 0);
		for (const hash160& h : sorted_)
		{
			const std::uint64_t slot = slot_of(h);
			bitmap_[slot / 64] |= std::uint64_t{1} << (slot % 64);
		}
	}

	[[nodiscard]] bool empty() const { return sorted_.empty(); }

	[[nodiscard]] bool contains(const hash160& h) const
	{
		assert(!bitmap_.empty());

		const std::uint64_t slot = slot_of(h);
		if (!((bitmap_[slot / 64] >> (slot % 64)) & 1)) [[likely]]
			return false;
		return std::ranges::binary_search(sorted_, h);
	}

private:
	[[nodiscard]] std::uint64_t slot_of(const hash160& h) const { return load_be64(h.data()) >> shift_; }

	std::vector<hash160> sorted_;
	std::vector<std::uint64_t> bitmap_;
	unsigned shift_ = 64;
};

struct address_targets
{
	hash160_set base58, bech32;

	bool add(const std::string_view address)
	{
		hash160 h;
		if (address_to_hash160(address, h))
			base58.insert(h);
		else if (bech32_to_hash160(address, h))
			bech32.insert(h);
		else
			return false;
		return true;
	}

	// Call after the last add and before the first match.
	void build()
	{
		base58.build();
		bech32.build();
	}

	[[nodiscard]] bool empty() const { return base58.empty() && bech32.empty(); }

	// pub uses the configured legacy compression, or is compressed for Bech32-only searches.
	[[nodiscard]] std::optional<address_format> match(const std::span<const std::uint8_t> pub) const
	{
		hash160 h [[indeterminate]];

		if (!base58.empty())
		{
			h = hash160_of(pub);
			if (base58.contains(h))
				return address_format::base58;
		}

		if (!bech32.empty())
		{
			std::array<std::uint8_t, 33> storage [[indeterminate]];

			if (base58.empty() || pub.size() != 33)
				h = hash160_of(compressed_public_key(pub, storage));

			if (bech32.contains(h))
				return address_format::bech32;
		}

		return std::nullopt;
	}
};

void write_candidate(
	std::ostream& out,
	const address_format format,
	const hash160& h,
	const std::span<const std::uint8_t> public_key,
	const std::span<const std::uint8_t, 32> private_key)
{
	std::array<char, 42 + 3 + 130 + 3 + 64 + 1> line [[indeterminate]];

	char* it = line.data();
	it += format == address_format::bech32 ? hash160_to_bech32(h, it) : hash160_to_address(h, it);
	it = std::ranges::copy(std::string_view{" - "}, it).out;

	to_hex(public_key, it);
	it += public_key.size() * 2;
	it = std::ranges::copy(std::string_view{" - "}, it).out;

	to_hex(private_key, it);
	it += private_key.size() * 2;
	*it++ = '\n';

	out.write(line.data(), it - line.data());
}

void dump_candidate(
	std::ostream& out,
	const address_format format,
	const std::span<const std::uint8_t> public_key,
	const std::span<const std::uint8_t, 32> private_key)
{
	hash160 h [[indeterminate]];
	if (format != address_format::bech32)
	{
		h = hash160_of(public_key);
		write_candidate(out, address_format::base58, h, public_key, private_key);
	}

	if (format != address_format::base58)
	{
		std::array<std::uint8_t, 33> storage [[indeterminate]];

		const auto compressed_pub = compressed_public_key(public_key, storage);
		if (format == address_format::bech32 || public_key.size() != 33)
			h = hash160_of(compressed_pub);

		write_candidate(out, address_format::bech32, h, compressed_pub, private_key);
	}
}

// ---------------------------------------------------------------------------
// secp256k1 base field, p = 2^256 - 2^32 - 977
//
// Elements are canonical (< p) 256-bit integers. Because 2^256 ≡ 2^32 + 977
// (mod p), a 512-bit product reduces by folding its top half back in times
// that small constant: cheaper than Montgomery reduction, and elements stay in
// plain form, so serializing a point is just a byte swap.
// ---------------------------------------------------------------------------
namespace field
{

using u256 = dixelu::long_uint<1>;
using u512 = dixelu::long_uint<2>;
using u128 = unsigned __int128;

constexpr std::uint64_t fold = 0x1000003D1; // 2^256 mod p
constexpr u256 p = ~u256{} - u256{fold - 1};

u256 reduce(const u512& t)
{
	u256 r;
	u128 acc = 0;
	for (int i = 0; i < 4; ++i)
	{
		acc += static_cast<u128>(t[i + 4]) * fold + t[i];
		r[i] = static_cast<std::uint64_t>(acc);
		acc >>= 64;
	}

	// Two more passes fold in what overflowed 2^256: first under 2^34, then 0 or 1. If the
	// first pass carries out, it leaves r below 2^68, so the second cannot carry again.
	for (int pass = 0; pass < 2; ++pass)
	{
		acc *= fold;
		for (int i = 0; i < 4; ++i)
		{
			acc += r[i];
			r[i] = static_cast<std::uint64_t>(acc);
			acc >>= 64;
		}
	}

	if (r >= p) [[unlikely]]
		r += u256{fold}; // r - p, modulo 2^256
	return r;
}

u256 mul(const u256& a, const u256& b)
{
	return reduce(u256::multiply_wide(a, b));
}

u256 sqr_n(u256 a, const int n)
{
	for (int i = 0; i < n; ++i)
		a = mul(a, a);
	return a;
}

u256 sub(const u256& a, const u256& b)
{
	// On a borrow the difference is a - b + 2^256; taking off fold as well leaves a - b + p.
	const auto [difference, borrow] = u256::subtract_with_borrow(a, b);
	return difference - u256{borrow ? fold : 0};
}

// a^(p - 2) for a != 0. p - 2 is, from the top, 223 ones, a zero, 22 ones, 0000, 1, 0, 11, 0, 1;
// this addition chain (from libsecp256k1) builds those runs of ones: 255 squarings, 15 multiplications.
u256 invert(const u256& a)
{
	const u256 x2 = mul(sqr_n(a, 1), a);
	const u256 x3 = mul(sqr_n(x2, 1), a);
	const u256 x6 = mul(sqr_n(x3, 3), x3);
	const u256 x9 = mul(sqr_n(x6, 3), x3);
	const u256 x11 = mul(sqr_n(x9, 2), x2);
	const u256 x22 = mul(sqr_n(x11, 11), x11);
	const u256 x44 = mul(sqr_n(x22, 22), x22);
	const u256 x88 = mul(sqr_n(x44, 44), x44);
	const u256 x176 = mul(sqr_n(x88, 88), x88);
	const u256 x220 = mul(sqr_n(x176, 44), x44);
	const u256 x223 = mul(sqr_n(x220, 3), x3);

	u256 t = mul(sqr_n(x223, 23), x22);
	t = mul(sqr_n(t, 5), a);
	t = mul(sqr_n(t, 3), x2);
	return mul(sqr_n(t, 2), a);
}

u256 load(const std::uint8_t* in)
{
	u256 r;
	for (int i = 0; i < 4; ++i)
		r[3 - i] = load_be64(in + 8 * i);
	return r;
}

void store(std::uint8_t* out, const u256& a)
{
	for (int i = 0; i < 4; ++i)
		store_be64(out + 8 * i, a[3 - i]);
}

u256 from_bn(const BIGNUM* bn)
{
	std::array<std::uint8_t, 32> bytes [[indeterminate]];
	if (BN_bn2binpad(bn, bytes.data(), 32) != 32)
		die("BIGNUM does not fit in 256 bits");
	return load(bytes.data());
}

} // namespace field

// ---------------------------------------------------------------------------
// secp256k1 key walker
//
// Visits the public keys of first, first + 1, ... in order. Rather than one
// scalar multiplication per key, each batch adds the precomputed points
// 1·G .. B·G to the current point S in affine coordinates, sharing a single
// field inversion across the whole batch (Montgomery's trick); S then moves
// on by B·G. OpenSSL is only used to set up the table and S; the walk itself
// runs on plain field elements and never allocates.
// ---------------------------------------------------------------------------
class key_walker
{
public:
	key_walker(std::size_t batch, bool compressed);

	[[nodiscard]] const BIGNUM* order() const { return EC_GROUP_get0_order(group_.get()); }

	// Calls visit(index, pubkey) for key first + index, index in [0, count), until
	// it returns true; returns whether it did. Requires 1 <= first, first + count <= order().
	template <typename Visitor>
	bool walk(const BIGNUM* first, std::uint64_t count, Visitor&& visit);

private:
	struct affine_point
	{
		field::u256 x, y;
	};

	void load(affine_point& dst, const EC_POINT* src) const;
	std::span<const std::uint8_t> serialize(const affine_point& point);

	ec_group_ptr group_;
	bn_ctx_ptr ctx_;
	bool compressed_;

	std::vector<affine_point> table_; // table_[i] = (i + 1)·G
	std::vector<field::u256> dx_;     // x(T[i]) - x(S), then its inverse
	std::vector<field::u256> prefix_; // dx_[0] · … · dx_[i]
	affine_point s_, r_;
	std::array<std::uint8_t, 65> pub_;
};

key_walker::key_walker(const std::size_t batch, const bool compressed)
	: group_{EC_GROUP_new_by_curve_name(NID_secp256k1)}
	, ctx_{BN_CTX_new()}
	, compressed_{compressed}
	, table_(batch)
	, dx_(batch)
	, prefix_(batch)
{
	if (!group_ || !ctx_ || batch == 0)
		die("secp256k1 setup failed");

	const bn_ptr p = new_bn();
	if (!EC_GROUP_get_curve(group_.get(), p.get(), nullptr, nullptr, ctx_.get()) || field::from_bn(p.get()) != field::p)
		die("secp256k1 field setup failed");

	const EC_POINT* g = EC_GROUP_get0_generator(group_.get());
	const ec_point_ptr point{EC_POINT_dup(g, group_.get())};
	if (!point)
		die("EC_POINT_dup failed");

	for (affine_point& entry : table_)
	{
		load(entry, point.get());
		if (!EC_POINT_add(group_.get(), point.get(), point.get(), g, ctx_.get()))
			die("EC_POINT_add failed");
	}
}

void key_walker::load(affine_point& dst, const EC_POINT* src) const
{
	const bn_ptr x = new_bn(), y = new_bn();
	if (!EC_POINT_get_affine_coordinates(group_.get(), src, x.get(), y.get(), ctx_.get()))
		die("loading an EC point failed");

	dst = {field::from_bn(x.get()), field::from_bn(y.get())};
}

std::span<const std::uint8_t> key_walker::serialize(const affine_point& point)
{
	field::store(pub_.data() + 1, point.x);
	if (compressed_)
	{
		pub_[0] = point.y[0] & 1 ? 0x03 : 0x02;
		return std::span{pub_}.first(33);
	}

	pub_[0] = 0x04;
	field::store(pub_.data() + 33, point.y);

	return pub_;
}

template <typename Visitor>
bool key_walker::walk(const BIGNUM* first, const std::uint64_t count, Visitor&& visit)
{
	const std::size_t batch = table_.size();
	std::uint64_t index = 0;

	// Keys up to B are table entries already. Taking them from there also keeps
	// S above B, where S + i·G can never degenerate into a doubling.
	if (BN_num_bits(first) <= 32)
	{
		for (BN_ULONG k = BN_get_word(first); k <= batch && index < count; ++k, ++index)
		{
			if (visit(index, serialize(table_[k - 1])))
				return true;
		}
	}

	if (index == count)
		return false;

	// The walk's only scalar multiplication.
	{
		const bn_ptr k{BN_dup(first)};
		const ec_point_ptr point{EC_POINT_new(group_.get())};

		if (!k || !point || !BN_add_word(k.get(), index)
			|| !EC_POINT_mul(group_.get(), point.get(), k.get(), nullptr, nullptr, ctx_.get()))
			die("EC_POINT_mul failed");

		load(s_, point.get());
	}

	if (visit(index++, serialize(s_)))
		return true;

	while (index < count)
	{
		const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(batch, count - index));

		for (std::size_t i = 0; i < n; ++i)
		{
			dx_[i] = field::sub(table_[i].x, s_.x);
			prefix_[i] = i ? field::mul(prefix_[i - 1], dx_[i]) : dx_[0];
		}

		// Invert the product once, then peel off dx_[i]⁻¹ from the back.
		field::u256 inv = field::invert(prefix_[n - 1]);
		for (std::size_t i = n - 1; i > 0; --i)
		{
			const field::u256 dx = dx_[i];
			dx_[i] = field::mul(inv, prefix_[i - 1]); // dx[i]⁻¹
			inv = field::mul(inv, dx);                // (dx[0] · … · dx[i - 1])⁻¹
		}

		dx_[0] = inv;

		// R = S + T:  λ = (y_T - y_S) / (x_T - x_S),  x_R = λ² - x_S - x_T,  y_R = λ(x_S - x_R) - y_S
		for (std::size_t i = 0; i < n; ++i)
		{
			const auto& [x, y] = table_[i];

			const field::u256 lambda = field::mul(field::sub(y, s_.y), dx_[i]);
			r_.x = field::sub(field::sub(field::mul(lambda, lambda), s_.x), x);
			r_.y = field::sub(field::mul(field::sub(s_.x, r_.x), lambda), s_.y);

			if (visit(index + i, serialize(r_)))
				return true;
		}

		// The batch's last point starts the next one.
		s_ = r_;
		index += n;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Progress bar (simple replacement for tqdm)
// ---------------------------------------------------------------------------
class progress_meter
{
	using clock = std::chrono::steady_clock;

public:
	explicit progress_meter(const std::uint64_t total) : total_{total} {}

	// Cheap enough to call per key; prints at most once a second.
	void tick(const std::uint64_t done)
	{
		if ((done & 0xFFFF) == 0 && clock::now() - last_ >= std::chrono::seconds{1})
			print(done);
	}

	void print(const std::uint64_t done)
	{
		constexpr int bar_width = 40;
		last_ = clock::now();

		const double progress = static_cast<double>(done) / static_cast<double>(total_);
		const int pos = static_cast<int>(bar_width * progress);

		std::cout << '[';
		for (int i = 0; i < bar_width; ++i)
			std::cout << (i < pos ? '=' : i == pos ? '>' : ' ');
		std::cout << "] " << std::fixed << std::setprecision(1) << progress * 100.0 << "%  ("
			<< done << '/' << total_ << ", " << std::setprecision(0) << rate(done) << " keys/s)" << std::endl;
	}

	[[nodiscard]] double elapsed() const
	{
		return std::chrono::duration<double>(clock::now() - start_).count();
	}

	[[nodiscard]] double rate(const std::uint64_t done) const
	{
		return static_cast<double>(done) / std::max(elapsed(), 1e-9);
	}

private:
	std::uint64_t total_;
	clock::time_point start_ = clock::now();
	clock::time_point last_ = start_;
};

}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
#ifndef BITFORCE_TEST
int main(int argc, char** argv)
{
	// Keep types separate: an uncompressed legacy hash is not a P2WPKH candidate.
	// The hot loop compares hashes, without encoding either address format.
	address_targets targets;
	for (std::string_view address : config::target_addresses)
	{
		if (!targets.add(address))
			die("Embedded list - not a supported mainnet P2PKH/P2WPKH address: " + std::string{address});
	}

	if (argc == 3 && std::string_view{argv[1]} == "--derive")
	{
		const std::string_view hex{argv[2]};
		if (hex.size() != 64 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string_view::npos)
			die("--derive requires a private key of exactly 64 hexadecimal digits");

		BIGNUM* raw = nullptr;
		const int digits = BN_hex2bn(&raw, argv[2]);
		bn_ptr key{raw};
		if (digits != 64 || !key)
			die("parsing the private key failed");

		key_walker walker{1, config::compressed};
		if (BN_is_zero(key.get()) || BN_cmp(key.get(), walker.order()) >= 0)
			die("private key must be in [1, secp256k1 order)");

		std::array<std::uint8_t, 32> private_key [[indeterminate]];
		if (BN_bn2binpad(key.get(), private_key.data(), 32) != 32)
			die("serializing the private key failed");

		std::cout << "# address - public key - private key (hex); Base58 then Bech32\n";
		walker.walk(key.get(), 1, [&](std::uint64_t, const std::span<const std::uint8_t> pub)
		{
			dump_candidate(std::cout, address_format::both, pub, private_key);
			return false;
		});

		return 0;
	}

	if (argc == 3 && std::string_view{argv[1]} == "--addr_file")
	{
		std::cout << "Reading addresses from file...\n" << std::flush;

		targets = {};
		std::ifstream fin(argv[2]);
		std::string address;
		std::string coins;

		while (fin >> address >> coins)
		{
			if (targets.add(address))
				continue;

			std::cout << "Embedded list - not a supported mainnet P2PKH/P2WPKH address: " + std::string{address} << "\n";
		}
	}
	else if (argc != 1)
	{
		std::cerr << "Usage: bitforce [--derive <64-digit private-key hex>]|[--addr_file <filename>]\n";
		return argc == 2 && std::string_view{argv[1]} == "--help" ? 0 : 2;
	}

	std::cout << "Bitcoin partial private key brute-forcer (C++)\n"
		<< "Supports mainnet P2PKH (Base58) and P2WPKH (Bech32) addresses.\n";

	targets.build();

	const bool compressed = config::compressed || (targets.empty()
		? config::dump_format == address_format::bech32 : targets.base58.empty());
	key_walker walker{config::batch_size, compressed};

	// The search covers [base, base + 2^missing_bits) ∩ [1, n).
	constexpr std::uint64_t num_candidates = std::uint64_t{1} << config::missing_bits;
	bn_ptr base = new_bn(), first = new_bn(), end = new_bn();
	if (!BN_rand_range(base.get(), walker.order())
		|| !BN_rshift(base.get(), base.get(), config::missing_bits)
		|| !BN_lshift(base.get(), base.get(), config::missing_bits)
		|| !BN_copy(first.get(), base.get())
		|| (BN_is_zero(first.get()) && !BN_one(first.get()))
		|| !BN_copy(end.get(), base.get())
		|| !BN_add_word(end.get(), num_candidates)
		|| (BN_cmp(end.get(), walker.order()) > 0 && !BN_copy(end.get(), walker.order()))
		|| !BN_sub(end.get(), end.get(), first.get()))
		die("choosing the search range failed");

	const std::uint64_t count = BN_get_word(end.get());
	const std::uint64_t first_offset = BN_is_zero(base.get()) ? 1 : 0; // first - base

	std::array<std::uint8_t, 32> base_bytes [[indeterminate]];
	BN_bn2binpad(base.get(), base_bytes.data(), 32);

	std::cout << "\nBrute-forcing " << num_candidates << " candidates...\n"
		<< "Base int: " << to_hex(base_bytes) << "\n";

	progress_meter progress{count};
	int exit_code = 0;

	if (!targets.empty())
	{
		// Mode 1: search for specific addresses, stop at the first hit
		std::uint64_t hit = 0;
		std::vector<std::uint8_t> hit_public_key;
		auto hit_format = address_format::base58;

		const bool found = walker.walk(first.get(), count,
		[&](const std::uint64_t index, std::span<const std::uint8_t> pub)
		{
			progress.tick(index);
			const auto match = targets.match(pub);
			if (!match) [[likely]]
				return false;

			hit = index;
			hit_format = *match;
			std::array<std::uint8_t, 33> storage [[indeterminate]];

			if (hit_format == address_format::bech32)
				pub = compressed_public_key(pub, storage);
			hit_public_key.assign(pub.begin(), pub.end());

			return true;
		});

		if (found)
		{
			std::array<std::uint8_t, 32> private_key = base_bytes;
			store_be64(private_key.data() + 24, load_be64(private_key.data() + 24) | (first_offset + hit));

			std::cout << "\n=== FOUND MATCH ===\n"
				<< "Private key (hex): " << to_hex(private_key) << "\n"
				<< "Private key (WIF): " << private_to_wif(private_key, hit_public_key.size() == 33) << "\n"
				<< "Public key:        " << to_hex(hit_public_key) << "\n"
				<< "Address:           " << (hit_format == address_format::bech32
					? hash160_to_bech32(hash160_of(hit_public_key)) :
					  hash160_to_address(hash160_of(hit_public_key))) << "\n";
		}
		else
		{
			progress.print(count);
			std::cout << "\nNo match found in the search space.\n";
			exit_code = 1;
		}
	}
	else
	{
		// Mode 2: dump every candidate for later online balance checks
		const char* filename = "candidate_addresses.txt";
		std::vector<char> file_buffer(1 << 20);
		std::ofstream out;
		out.rdbuf()->pubsetbuf(file_buffer.data(), static_cast<std::streamsize>(file_buffer.size()));
		out.open(filename, std::ios::binary);
		if (!out)
			die("cannot open " + std::string{filename});

		out << "# address - public key - private key (hex), one address per line\n";

		// Keys in the block differ from the base only in their low 64 bits.
		std::array<std::uint8_t, 32> private_key = base_bytes;
		const std::uint64_t base_low = load_be64(private_key.data() + 24);

		walker.walk(first.get(), count,
		[&](const std::uint64_t index, const std::span<const std::uint8_t> pub)
		{
			progress.tick(index);
			store_be64(private_key.data() + 24, base_low | (first_offset + index));

			dump_candidate(out, config::dump_format, pub, private_key);
			return false;
		});

		if (!out.flush())
			die("writing " + std::string{filename} + " failed");

		progress.print(count);
		std::cout << "\nDone! Addresses for " << count << " candidate keys saved to '" << filename << "'"
			<< (config::dump_format == address_format::both ? " (two addresses per key).\n" : ".\n");
	}

	std::cout << "Elapsed: " << std::fixed << std::setprecision(2) << progress.elapsed() << " s\n";
	return exit_code;
}
#endif
