// Exercise the same implementation as the standalone executable, without its CLI.
#define BITFORCE_TEST
#include "bitforce.cpp"
#include <random>
#include <sstream>

namespace
{

unsigned checks = 0;

void check(const bool condition, const std::string_view message)
{
	++checks;
	if (!condition)
		die("test failed: " + std::string{message});
}

std::vector<std::uint8_t> reference_pubkey(const BIGNUM* key, const bool compressed)
{
	const ec_group_ptr group{EC_GROUP_new_by_curve_name(NID_secp256k1)};
	const bn_ctx_ptr ctx{BN_CTX_new()};
	const ec_point_ptr point{EC_POINT_new(group.get())};

	if (!group || !ctx || !point
		|| !EC_POINT_mul(group.get(), point.get(), key, nullptr, nullptr, ctx.get()))
		die("reference key derivation failed");

	std::vector<std::uint8_t> pub(compressed ? 33 : 65);
	if (EC_POINT_point2oct(group.get(), point.get(), compressed ? POINT_CONVERSION_COMPRESSED
		: POINT_CONVERSION_UNCOMPRESSED, pub.data(), pub.size(), ctx.get()) != pub.size())
		die("reference key serialization failed");

	return pub;
}

std::vector<std::uint8_t> reference_pubkey(const unsigned key, const bool compressed)
{
	const bn_ptr scalar = new_bn();
	if (!BN_set_word(scalar.get(), key))
		die("BN_set_word failed");

	return reference_pubkey(scalar.get(), compressed);
}

void test_field()
{
	const bn_ctx_ptr ctx{BN_CTX_new()};
	const bn_ptr p = new_bn(), r = new_bn();
	BIGNUM* raw = nullptr;
	check(BN_hex2bn(&raw, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F") == 64, "parse p");
	BN_copy(p.get(), raw);
	BN_free(raw);
	check(field::from_bn(p.get()) == field::p, "field modulus");

	// Values next to 0, p and 2^256 mod p exercise the carries and the final reduction.
	std::vector<bn_ptr> values;
	for (const char* hex : {"0", "1", "2", "1000003D0", "1000003D1", "1000003D2",
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2E",
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2D",
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE00000000",
		"8000000000000000000000000000000000000000000000000000000000000000",
		"7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF7FFFFE17"})
	{
		BIGNUM* v = nullptr;
		check(BN_hex2bn(&v, hex) > 0, "parse field edge value");
		values.emplace_back(v);
	}

	while (values.size() < 200)
	{
		values.push_back(new_bn());
		check(BN_rand_range(values.back().get(), p.get()), "random field element");
	}

	for (const bn_ptr& a : values)
	{
		const field::u256 fa = field::from_bn(a.get());
		for (const bn_ptr& b : values)
		{
			const field::u256 fb = field::from_bn(b.get());
			check(BN_mod_mul(r.get(), a.get(), b.get(), p.get(), ctx.get())
				&& field::mul(fa, fb) == field::from_bn(r.get()), "field multiplication");
			check(BN_mod_sub(r.get(), a.get(), b.get(), p.get(), ctx.get())
				&& field::sub(fa, fb) == field::from_bn(r.get()), "field subtraction");
		}

		if (!BN_is_zero(a.get()))
		{
			check(BN_mod_inverse(r.get(), a.get(), p.get(), ctx.get())
				&& field::invert(fa) == field::from_bn(r.get()), "field inversion");
		}
	}
}

void test_hash160_set()
{
	hash160_set none;
	none.build();
	check(none.empty() && !none.contains(hash160{}), "empty set has no members");

	// Two in three members repeat the previous member's leading 8 bytes, and the probes below
	// mostly change only the tail, so lookups get past the bitmap and the search must decide.
	std::mt19937_64 random{0xB17F0CE};
	const auto random_hash = [&]
	{
		hash160 h;
		for (std::uint8_t& byte : h)
			byte = static_cast<std::uint8_t>(random());
		return h;
	};

	std::vector<hash160> members;
	hash160_set set;
	for (int i = 0; i < 3000; ++i)
	{
		hash160 h = random_hash();
		if (i % 3 != 0)
			std::copy_n(members.back().begin(), 8, h.begin());
		members.push_back(h);
		set.insert(h);
	}
	set.build();
	check(!set.empty(), "filled set is not empty");

	for (const hash160& h : members)
		check(set.contains(h), "every member is found");

	for (int i = 0; i < 10000; ++i)
	{
		hash160 near = members[random() % members.size()];
		near[8 + random() % 12] ^= static_cast<std::uint8_t>(1 + random() % 255);
		for (const hash160& probe : {near, random_hash()})
			check(set.contains(probe) == (std::ranges::find(members, probe) != members.end()),
				"lookup agrees with a linear search");
	}
}

constexpr std::string_view legacy_uncompressed = "1EHNa6Q4Jz2uvNExL497mE43ikXhwF6kZm";
constexpr std::string_view legacy_compressed = "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH";
constexpr std::string_view segwit = "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4";

void test_addresses()
{
	const auto pub = reference_pubkey(1, true);
	const auto uncompressed_pub = reference_pubkey(1, false);
	const hash160 h = hash160_of(pub);
	check(to_hex(h) == "751e76e8199196d454941c45d1b3a323f1433bd6", "known compressed key hash");
	check(hash160_to_address(h) == legacy_compressed, "compressed P2PKH regression");
	check(hash160_to_address(hash160_of(uncompressed_pub)) == legacy_uncompressed,
		"uncompressed P2PKH regression");
	check(hash160_to_bech32(h) == segwit, "BIP173 P2WPKH encoding");
	hash160 decoded;
	check(bech32_to_hash160(segwit, decoded) && decoded == h, "BIP173 lowercase decoding");
	check(bech32_to_hash160("BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4", decoded)
		&& decoded == h, "BIP173 uppercase decoding");
	check(address_to_hash160(legacy_compressed, decoded) && decoded == h, "legacy decoding");
	check(!address_to_hash160("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMJ", decoded), "legacy checksum");
	address_targets configured;
	for (std::string_view address : config::target_addresses)
		check(configured.add(address), "configured target remains valid");

	// Invalid and unsupported vectors from https://bips.dev/173/ and https://bips.dev/350/.
	for (const std::string_view address : {
		"", "bc1gmk9yu", "A12UEL5L",
		"tc1qw508d6qejxtdg4y5r3zarvary0c5xw7kg3g4ty", // Wrong network.
		"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t5", // Bad checksum.
		"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kemeawh", // Bech32m with v0.
		"bC1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", // Mixed case.
		"BC13W508D6QEJXTDG4Y5R3ZARVARY0C5XW7KN40WF2", // Witness version.
		"BC1QR508D6QEJXTDG4Y5R3ZARVARYV98GJ9P", // Program length.
		"bc1zw508d6qejxtdg4y5r3zarvaryvqyzf3du", // Excess padding.
		"tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3pjxtptv", // Nonzero padding.
		"tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7", // Testnet P2WSH.
		"bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0", // Taproot.
		"bc1zw508d6qejxtdg4y5r3zarvaryvaxxpcs" // Other witness version.
	})
	{
		decoded.fill(0xa5);
		const hash160 before = decoded;
		check(!bech32_to_hash160(address, decoded), "invalid/unsupported Bech32 rejected");
		check(decoded == before, "invalid Bech32 does not modify output");
	}

	for (std::size_t i = 0; i < segwit.size(); ++i)
	{
		std::string mutated{segwit};
		mutated[i] = mutated[i] == 'q' ? 'p' : 'q';
		check(!bech32_to_hash160(mutated, decoded), "single-character error detected");
	}

	check(!bech32_to_hash160(std::string{segwit} + 'q', decoded), "extra symbol rejected");
	check(!bech32_to_hash160(segwit.substr(0, 41), decoded), "missing symbol rejected");

	for (unsigned i = 0; i < 256; ++i)
	{
		hash160 input;
		for (std::size_t j = 0; j < input.size(); ++j)
			input[j] = static_cast<std::uint8_t>(i + j * 17);
		check(bech32_to_hash160(hash160_to_bech32(input), decoded) && decoded == input,
			"Bech32 round trip");
	}

	std::array<std::uint8_t, 32> priv{};
	priv.back() = 1;
	check(private_to_wif(priv, true) == "KwDiBf89QgGbjEhKnhXJuH7LrciVrZi3qYjgd9M7rFU73sVHnoWn",
		"compressed WIF regression");
	check(private_to_wif(priv, false) == "5HpHagT65TZzG1PH3CSu63k8DbpvD8s5ip4nEB3kEsreAnchuDf",
		"uncompressed WIF regression");
}

void test_targets_and_output()
{
	const auto pub = reference_pubkey(1, true);
	const auto uncompressed_pub = reference_pubkey(1, false);
	const auto other_pub = reference_pubkey(2, false);
	address_targets targets;
	check(targets.empty(), "empty target set");
	check(!targets.add("bc1invalid") && targets.empty(), "bad target rejected");
	check(targets.add(segwit), "add Bech32 target");
	targets.build();
	check(targets.match(pub) == address_format::bech32, "compressed Bech32 match");
	check(targets.match(uncompressed_pub) == address_format::bech32, "uncompressed input compressed for Bech32");
	check(!targets.match(other_pub), "different key does not match");
	check(targets.add(legacy_uncompressed), "add legacy target to mixed list");
	targets.build();
	check(targets.match(uncompressed_pub) == address_format::base58, "mixed list legacy match");
	check(targets.match(pub) == address_format::bech32, "mixed list Bech32 match");
	targets = {};
	check(targets.add(legacy_compressed), "add compressed legacy target");
	check(targets.add(hash160_to_bech32(hash160_of(reference_pubkey(2, true)))), "add different Bech32 target");
	targets.build();
	check(targets.match(pub) == address_format::base58, "compressed legacy in mixed list");
	check(targets.match(other_pub) == address_format::bech32, "Bech32 hit after legacy miss");
	check(targets.match(reference_pubkey(2, true)) == address_format::bech32, "shared compressed hash match");

	// An address can encode any hash, but P2WPKH must be derived from a compressed key.
	targets = {};
	check(targets.add(hash160_to_bech32(hash160_of(uncompressed_pub))), "add encoded uncompressed hash");
	targets.build();
	check(!targets.match(uncompressed_pub), "uncompressed hash must not match P2WPKH");
	check(!targets.match(pub), "compression distinction preserved");

	std::array<std::uint8_t, 32> priv{};
	priv.back() = 1;
	for (const bool compressed : {false, true})
	{
		const auto& input = compressed ? pub : uncompressed_pub;
		const std::string legacy_line = std::string{compressed ? legacy_compressed : legacy_uncompressed}
			+ " - " + to_hex(input) + " - " + to_hex(priv) + "\n";
		const std::string segwit_line = std::string{segwit} + " - " + to_hex(pub) + " - " + to_hex(priv) + "\n";
		for (const auto format : {address_format::base58, address_format::bech32, address_format::both})
		{
			std::ostringstream output;
			dump_candidate(output, format, input, priv);
			check(output.str() == (format == address_format::base58 ? legacy_line
				: format == address_format::bech32 ? segwit_line : legacy_line + segwit_line),
				"dump address, public key, private key and row count");
		}
	}
}

void test_walker()
{
	for (bool compressed : {false, true})
	{
		key_walker walker{8, compressed};
		for (unsigned start : {1u, 1024u})
		{
			bn_ptr first = new_bn();
			check(BN_set_word(first.get(), start), "set walk start");
			unsigned visited = 0;
			const bool stopped = walker.walk(first.get(), 40, [&](std::uint64_t index, std::span<const std::uint8_t> pub)
				{
					const unsigned key = start + static_cast<unsigned>(index);
					check(std::ranges::equal(pub, reference_pubkey(key, compressed)), "walker matches scalar multiplication");
					std::array<std::uint8_t, 33> storage;
					check(std::ranges::equal(compressed_public_key(pub, storage), reference_pubkey(key, true)),
						"compressed public key matches OpenSSL for both parities");
					++visited;
					return false;
				});
			check(!stopped && visited == 40, "walker completes multiple batches");
		}

		// Large keys, up to the last 40 below the group order: n - 41 + 39 = n - 2.
		bn_ptr last = new_bn(), random = new_bn();
		check(BN_sub(last.get(), walker.order(), BN_value_one()) && BN_sub_word(last.get(), 40)
			&& BN_rand_range(random.get(), last.get()) && BN_add_word(random.get(), 1), "large walk starts");
		for (const BIGNUM* start : {last.get(), random.get()})
		{
			const bn_ptr key = new_bn();
			unsigned visited = 0;
			walker.walk(start, 40, [&](std::uint64_t index, std::span<const std::uint8_t> pub)
				{
					check(BN_copy(key.get(), start) && BN_add_word(key.get(), index)
						&& std::ranges::equal(pub, reference_pubkey(key.get(), compressed)), "walker matches for large keys");
					++visited;
					return false;
				});
			check(visited == 40, "large walk completes");
		}

		address_targets targets;
		check(targets.add(hash160_to_bech32(hash160_of(reference_pubkey(19, true)))), "walker target");
		targets.build();
		bn_ptr first = new_bn();
		check(BN_one(first.get()), "set search start");
		std::uint64_t hit = 0;
		check(walker.walk(first.get(), 40, [&](std::uint64_t index, std::span<const std::uint8_t> pub)
			{
				if (targets.match(pub) != address_format::bech32)
					return false;
				hit = index;
				return true;
			}) && hit == 18, "Bech32 search stops at correct key across batch boundary");
	}
}

} // namespace

int main()
{
	test_field();
	test_hash160_set();
	test_addresses();
	test_targets_and_output();
	test_walker();
	std::cout << "PASS: " << checks << " checks\n";
}
