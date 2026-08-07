// Standard (RFC 4648) base64 decoding. Only decoding is needed (vcp payloads).
#ifndef MAXCALLS_DETAIL_BASE64_HPP
#define MAXCALLS_DETAIL_BASE64_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "maxcalls/maxcalls.hpp"

namespace maxcalls::detail {

inline std::vector<std::uint8_t> base64_decode(std::string_view in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::vector<std::uint8_t> out;
    out.reserve(in.size() / 4 * 3 + 3);

    int buffer = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c);
        if (v < 0) throw Error("invalid base64 character");
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace maxcalls::detail

#endif // MAXCALLS_DETAIL_BASE64_HPP
