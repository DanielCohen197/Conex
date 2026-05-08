#pragma once
// conex.hpp — condition-based binary pattern matching
// A C++ single-header library for structural binary pattern matching with predicate conditions.
// Usage: conex::match(blob, "(c0)(c1)*(c2)+", cond0, cond1, cond2)

#include <span>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <functional>
#include <variant>
#include <stdexcept>
#include <cassert>

namespace conex {

	// ─────────────────────────────────────────────
	// Match result
	// ─────────────────────────────────────────────

	struct Capture {
		size_t offset;                        // byte offset in original blob
		std::span<const uint8_t> bytes;       // the matched bytes
	};

	struct MatchResult {
		bool matched = false;
		size_t start = 0;                    // byte offset where match begins
		size_t end = 0;                    // byte offset after match ends
		std::vector<std::vector<Capture>> captures; // captures[i] = all repetitions of group i

		explicit operator bool() const { return matched; }
	};

	// ─────────────────────────────────────────────
	// Internal: pattern token
	// ─────────────────────────────────────────────

	namespace detail {

		enum class Quantifier { One, ZeroOrMore, OneOrMore, ZeroOrOne };

		struct Token {
			int            condition_index; // which lambda (0-based)
			size_t         width;           // bytes consumed per match (0 = use runtime width)
			Quantifier     quantifier;
		};

		// Parse a pattern string like "(c0:8)(c1:4)*(c2)+(c3)?"
		// Condition references: c0, c1, ... cn
		// Width specifiers:     (c0:8) means 8 bytes, default is 1
		// Quantifiers:         * + ? or none (exactly one)
		inline std::vector<Token> parse_pattern(std::string_view pattern) {
			std::vector<Token> tokens;
			size_t i = 0;

			auto skip_ws = [&]() {
				while (i < pattern.size() && pattern[i] == ' ') ++i;
				};

			while (i < pattern.size()) {
				skip_ws();
				if (i >= pattern.size()) break;

				if (pattern[i] != '(')
					throw std::invalid_argument(
						std::string("conex: expected '(' at position ") + std::to_string(i));

				++i; // consume '('

				// parse 'c' followed by digits
				if (i >= pattern.size() || pattern[i] != 'c')
					throw std::invalid_argument("conex: expected 'c<n>' inside group");
				++i;

				size_t num_start = i;
				while (i < pattern.size() && std::isdigit((unsigned char)pattern[i])) ++i;
				if (i == num_start)
					throw std::invalid_argument("conex: expected digit after 'c'");

				int cond_idx = std::stoi(std::string(pattern.substr(num_start, i - num_start)));

				// optional width specifier :N
				size_t width = 1;
				if (i < pattern.size() && pattern[i] == ':') {
					++i;
					size_t w_start = i;
					while (i < pattern.size() && std::isdigit((unsigned char)pattern[i])) ++i;
					if (i == w_start)
						throw std::invalid_argument("conex: expected digit after ':'");
					width = std::stoul(std::string(pattern.substr(w_start, i - w_start)));
				}

				if (i >= pattern.size() || pattern[i] != ')')
					throw std::invalid_argument("conex: expected ')'");
				++i; // consume ')'

				// optional quantifier
				Quantifier q = Quantifier::One;
				if (i < pattern.size()) {
					switch (pattern[i]) {
					case '*': q = Quantifier::ZeroOrMore; ++i; break;
					case '+': q = Quantifier::OneOrMore;  ++i; break;
					case '?': q = Quantifier::ZeroOrOne;  ++i; break;
					default: break;
					}
				}

				tokens.push_back({ cond_idx, width, q });
			}

			return tokens;
		}

		// ─────────────────────────────────────────────
		// Internal: reader cursor
		// ─────────────────────────────────────────────

		struct Cursor {
			std::span<const uint8_t> data;
			size_t pos = 0;

			bool can_read(size_t n) const { return pos + n <= data.size(); }

			std::span<const uint8_t> peek(size_t n) const {
				return data.subspan(pos, n);
			}

			void advance(size_t n) { pos += n; }
		};

		// ─────────────────────────────────────────────
		// Internal: condition dispatcher
		// ─────────────────────────────────────────────

		using Condition = std::function<bool(std::span<const uint8_t>)>;

		// Try to match one token at cursor position, appending to captures[token_index]
		// Returns true if matched, advances cursor
		inline bool match_one(Cursor& cur, const Token& tok, const Condition& cond,
			std::vector<Capture>& cap_group)
		{
			if (!cur.can_read(tok.width)) return false;
			auto s = cur.peek(tok.width);
			if (!cond(s)) return false;
			cap_group.push_back({ cur.pos, s });
			cur.advance(tok.width);
			return true;
		}

		// Match all tokens starting at cursor; returns true if all tokens matched
		inline bool match_tokens(Cursor& cur,
			const std::vector<Token>& tokens,
			const std::vector<Condition>& conditions,
			std::vector<std::vector<Capture>>& captures)
		{
			for (size_t t = 0; t < tokens.size(); ++t) {
				const Token& tok = tokens[t];

				if (tok.condition_index < 0 ||
					tok.condition_index >= (int)conditions.size())
					throw std::out_of_range(
						std::string("conex: no condition provided for c")
						+ std::to_string(tok.condition_index));

				const Condition& cond = conditions[tok.condition_index];
				auto& cap_group = captures[t];

				switch (tok.quantifier) {
				case Quantifier::One:
					if (!match_one(cur, tok, cond, cap_group))
						return false;
					break;

				case Quantifier::ZeroOrOne:
					match_one(cur, tok, cond, cap_group); // ok to fail
					break;

				case Quantifier::ZeroOrMore:
					while (match_one(cur, tok, cond, cap_group)) {}
					break;

				case Quantifier::OneOrMore:
					if (!match_one(cur, tok, cond, cap_group))
						return false;
					while (match_one(cur, tok, cond, cap_group)) {}
					break;
				}
			}
			return true;
		}

	} // namespace detail

	// ─────────────────────────────────────────────
	// Public API
	// ─────────────────────────────────────────────

	// Build a condition list from variadic lambdas
	template<typename... Conds>
	std::vector<detail::Condition> make_conditions(Conds&&... conds) {
		return { detail::Condition(std::forward<Conds>(conds))... };
	}

	// search_first: scan blob byte by byte, return first match
	template<typename... Conds>
	MatchResult search_first(std::span<const uint8_t> blob,
		std::string_view pattern,
		Conds&&... conds)
	{
		auto tokens = detail::parse_pattern(pattern);
		auto conditions = make_conditions(std::forward<Conds>(conds)...);

		for (size_t start = 0; start < blob.size(); ++start) {
			detail::Cursor cur{ blob, start };
			std::vector<std::vector<Capture>> captures(tokens.size());

			if (detail::match_tokens(cur, tokens, conditions, captures)) {
				MatchResult r;
				r.matched = true;
				r.start = start;
				r.end = cur.pos;
				r.captures = std::move(captures);
				return r;
			}
		}

		return {};
	}

	// search_all: return every non-overlapping match
	template<typename... Conds>
	std::vector<MatchResult> search_all(std::span<const uint8_t> blob,
		std::string_view pattern,
		Conds&&... conds)
	{
		auto tokens = detail::parse_pattern(pattern);
		auto conditions = make_conditions(std::forward<Conds>(conds)...);

		std::vector<MatchResult> results;
		size_t start = 0;

		while (start < blob.size()) {
			detail::Cursor cur{ blob, start };
			std::vector<std::vector<Capture>> captures(tokens.size());

			if (detail::match_tokens(cur, tokens, conditions, captures)) {
				MatchResult r;
				r.matched = true;
				r.start = start;
				r.end = cur.pos;
				r.captures = std::move(captures);
				start = cur.pos; // advance past match (non-overlapping)
				results.push_back(std::move(r));
			}
			else {
				++start;
			}
		}

		return results;
	}

	// match: try to match at the start of the given span
	template<typename... Conds>
	MatchResult match(std::span<const uint8_t> blob,
		std::string_view pattern,
		Conds&&... conds)
	{
		auto tokens = detail::parse_pattern(pattern);
		auto conditions = make_conditions(std::forward<Conds>(conds)...);

		detail::Cursor cur{ blob, 0 };
		std::vector<std::vector<Capture>> captures(tokens.size());

		if (detail::match_tokens(cur, tokens, conditions, captures)) {
			MatchResult r;
			r.matched = true;
			r.start = 0;
			r.end = cur.pos;
			r.captures = std::move(captures);
			return r;
		}

		return {};
	}

} // namespace conex
