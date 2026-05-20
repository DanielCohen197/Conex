# Conex

[![CI](https://github.com/DanielCohen197/Conex/actions/workflows/ci.yml/badge.svg)](https://github.com/DanielCohen197/Conex/actions/workflows/ci.yml)
![Header-only](https://img.shields.io/badge/header--only-single%20file-blue)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

Binary pattern matching usually means hard-coded byte signatures. Conex lets you express match conditions as lambdas instead — find structures by semantics, not magic bytes.

```cpp
auto result = conex::search_first(blob, "(c0:4)(c1:8)*",
    [](std::span<const uint8_t> s) { /* signature check */ },
    [](std::span<const uint8_t> s) { /* page-aligned address */ }
);
```

---

## Requirements

- C++20 or later
- No dependencies beyond the standard library

---

## Installation

Copy `Include/conex.hpp` into your project and include it.

```cpp
#include "conex.hpp"
```

---

## Pattern Syntax

A pattern is a sequence of groups:

```
(cN:W)Q
```

| Part | Meaning |
|------|---------|
| `N`  | Index of the lambda to use (0-based, matches your variadic arguments) |
| `:W` | Width in bytes to consume per match (default: 1) |
| `Q`  | Quantifier: `*` zero or more, `+` one or more, `?` zero or one, nothing = exactly one |

### Examples

| Pattern | Meaning |
|---------|---------|
| `(c0:4)` | Exactly 4 bytes satisfying lambda 0 |
| `(c0:4)(c1:8)` | 4 bytes satisfying c0, followed by 8 bytes satisfying c1 |
| `(c0:4)*` | Zero or more 4-byte sequences satisfying c0 |
| `(c0:4)+(c1:2)` | One or more 4-byte sequences, then exactly 2 bytes |
| `(c0:8)(c0:8)(c1:4)*(c2:2)(c3:8)` | Two 8-byte addresses, any number of 4-byte records, two bytes, eight bytes |

---

## API

### `conex::search_first`

Scans a blob and returns the first match.

```cpp
conex::MatchResult conex::search_first(
    std::span<const uint8_t> blob,
    std::string_view pattern,
    Conds&&... conditions
);
```

### `conex::search_all`

Returns all non-overlapping matches.

```cpp
std::vector<conex::MatchResult> conex::search_all(
    std::span<const uint8_t> blob,
    std::string_view pattern,
    Conds&&... conditions
);
```

### `conex::match`

Tries to match at the start of the given span. To match at a specific offset, pass `blob.subspan(offset)`.

```cpp
conex::MatchResult conex::match(
    std::span<const uint8_t> blob,
    std::string_view pattern,
    Conds&&... conditions
);
```

---

## Match Result

```cpp
struct MatchResult {
    bool matched;
    size_t start;                              // byte offset where match begins
    size_t end;                                // byte offset after match ends
    std::vector<std::vector<Capture>> captures; // captures[group][repetition]
};

struct Capture {
    size_t offset;                     // byte offset in original blob
    std::span<const uint8_t> bytes;    // the matched bytes
};
```

`captures[i]` holds every repetition matched by group `i`. For a non-repeating group this will always have exactly one entry (if matched).

---

## Examples

Find a struct in a binary blob by its signature followed by any number of page-aligned addresses:

```cpp
#include "conex.hpp"

bool is_page_aligned_address(std::span<const uint8_t> bytes) {
    uint64_t addr;
    std::memcpy(&addr, bytes.data(), 8);
    return (addr & 0xFFF) == 0;
}

auto result = conex::search_first(
    std::span(blob),
    "(c0:4)(c1:8)*",

    // c0: match the struct signature
    [](std::span<const uint8_t> s) {
        uint32_t sig;
        std::memcpy(&sig, s.data(), 4);
        return sig == 0xDEADBEEF;
    },

    // c1: match page-aligned address
    [](std::span<const uint8_t> s) {
        return is_page_aligned_address(s);
    }
);

if (result) {
    auto* s = reinterpret_cast<const MyStruct*>(blob.data() + result.start);
}
```

Matching at a specific offset:

```cpp
auto result = conex::match(std::span(blob).subspan(offset, 4), "(c0:4)", my_lambda);
```

Iterating over captured records from a repeating group:

```cpp
auto result = conex::search_first(blob, "(c0:8)(c1:4)*(c2:2)", c0, c1, c2);

for (auto& capture : result.captures[1]) { // group 1 = (c1:4)*
    uint32_t val;
    std::memcpy(&val, capture.bytes.data(), 4);
    printf("record at offset %zu: 0x%08X\n", capture.offset, val);
}
```

For more examples, see [Example/Example.cpp](Example/Example.cpp).
