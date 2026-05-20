#include <cstdio>

#include "conex.hpp"

bool is_page_aligned_address(std::span<const uint8_t> bytes) {
	uint64_t addr;
	std::memcpy(&addr, bytes.data(), 8);
	return (addr & 0xFFF) == 0; // example: page-aligned
}

int main() {
	std::vector<uint8_t> blob = {
		0x00, 0x00, 0x00, 0x00,              // junk
		0xEF, 0xBE, 0xAD, 0xDE,              // signature: 0xDEADBEEF (LE)
		0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // page-aligned addr
		0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // page-aligned addr
		0xFF, 0xFF,                          // junk
		0xEF, 0xBE, 0xAD, 0xDE,              // signature: 0xDEADBEEF (LE)
		0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // page-aligned addr
		0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // page-aligned addr
	};

	// ── Example 1: find struct ──────────────────────────────────────
	// Pattern: one 4-byte signature, then one 8-byte page aligned address
	// (c0:4) = 4 bytes matching signature
	// (c1:8) = 8 bytes matching page aligned address condition
	auto result = conex::search_first(
		std::span(blob),
		"(c0:4)(c1:8)*",

		// c0: signature check
		[](std::span<const uint8_t> s) {
			uint32_t sig;
			std::memcpy(&sig, s.data(), 4);
			return sig == 0xDEADBEEF;
		},

		// c1: page aligned address check
		[](std::span<const uint8_t> s) {
			return is_page_aligned_address(s);
		}
	);

	if (result) {
		printf("Found struct at offset %zu\n", result.start);

		// Access captures
		auto& sig_capture = result.captures[0][0]; // group 0, first (only) match
		auto& addr1_capture = result.captures[1][0]; // group 1, first match
		auto& addr2_capture = result.captures[1][1]; // group 1, second match

		uint32_t sig;  std::memcpy(&sig, sig_capture.bytes.data(), 4);
		uint64_t addr1; std::memcpy(&addr1, addr1_capture.bytes.data(), 8);
		uint64_t addr2; std::memcpy(&addr2, addr2_capture.bytes.data(), 8);

		printf("  signature: 0x%08X\n", sig);
		printf("  address1:   0x%016llX\n", (unsigned long long)addr1);
		printf("  address2:   0x%016llX\n", (unsigned long long)addr2);
	}

	// ── Example 2: backtracking ──────────────────────────────────────
	// (c1:1)* must not greedily consume the bytes needed by (c2:8)
	result = conex::search_first(
		std::span(blob),
		"(c0:4)(c1:1)*(c2:8)",

		// c0: signature check
		[](std::span<const uint8_t> s) {
			uint32_t sig;
			std::memcpy(&sig, s.data(), 4);
			return sig == 0xDEADBEEF;
		},
		// c1: any single junk byte
		[](std::span<const uint8_t> s) {
			return true;
		},
		// c2: specific 8-byte value check
		[](std::span<const uint8_t> s) {
			uint64_t val;
			std::memcpy(&val, s.data(), 8);
			return val == 0x4000;
		}
	);

	assert(result && "Should find signature followed by junk bytes and specific address");
	printf("Found backtracking match at offset %zu, %zu junk bytes captured\n",
		result.start, result.captures[1].size());

	// ── Example 3: find signatures (failed) ──────────────────────────────────────
	result = conex::search_first(
		std::span(blob),
		"(c0:4)",

		// c0: signature check
		[](std::span<const uint8_t> s) {
			uint32_t sig;
			std::memcpy(&sig, s.data(), 4);
			return sig == 0x13371337;
		}
	);

	assert(!result && "Should not find signature 0x13371337");

	// ── Example 4: search_all ───────────────────────────────────────────
	auto all = conex::search_all(
		std::span(blob),
		"(c0:4)(c1:8)(c1:8)",

		// c0: signature check
		[](std::span<const uint8_t> s) {
			uint32_t sig;
			std::memcpy(&sig, s.data(), 4);
			return sig == 0xDEADBEEF;
		},

		// c1: page aligned address check
		[](std::span<const uint8_t> s) {
			return is_page_aligned_address(s);
		}
	);

	printf("Found %zu sequences\n", all.size());

	// ── Example 5: match ───────────────────────────────────────────
	auto match = conex::match(
		std::span(blob).subspan(4, 20), // start searching from offset 4, size 20 bytes
		"(c0:4)(c1:8)*",
		// c0: signature check
		[](std::span<const uint8_t> s) {
			uint32_t sig;
			std::memcpy(&sig, s.data(), 4);
			return sig == 0xDEADBEEF;
		},
		// c1: page aligned address check
		[](std::span<const uint8_t> s) {
			return is_page_aligned_address(s);
		}
	);

	assert(match && "Subspan did not match");

	// ── Example 6: match ───────────────────────────────────────────
	match = conex::match(
		std::span(blob),
		"(c0:4)",
		// c0: signature check
		[](std::span<const uint8_t> s) {
			uint32_t sig;
			std::memcpy(&sig, s.data(), 4);
			return sig == 0xDEADBEEF;
		}
	);

	assert(!match && "Span should not match");

	return 0;
}
