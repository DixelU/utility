// Minimal URL splitter for the ws(s)/http(s) endpoints this library talks to.
#ifndef MAXCALLS_DETAIL_URL_HPP
#define MAXCALLS_DETAIL_URL_HPP

#include <string>
#include <string_view>

#include "maxcalls/maxcalls.hpp"

namespace maxcalls::detail {

struct ParsedUrl {
    std::string scheme; // "wss", "https", ...
    std::string host;
    std::string port;   // resolved default if absent
    std::string target; // path + query (starts with '/')
    bool tls = true;
};

inline ParsedUrl parse_url(std::string_view url) {
    ParsedUrl out;

    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) throw Error("invalid URL (no scheme): " + std::string(url));
    out.scheme.assign(url.substr(0, scheme_end));
    url.remove_prefix(scheme_end + 3);

    out.tls = (out.scheme == "wss" || out.scheme == "https");

    // Some MAX signaling endpoints have a query directly after the authority,
    // for example wss://host?token=... (no explicit slash).  A slash-only
    // split would accidentally treat the query as part of the DNS hostname.
    auto target_start = url.find_first_of("/?#");
    std::string_view authority =
        (target_start == std::string_view::npos) ? url : url.substr(0, target_start);

    std::string_view target =
        (target_start == std::string_view::npos) ? std::string_view{} : url.substr(target_start);
    if (auto fragment = target.find('#'); fragment != std::string_view::npos)
        target = target.substr(0, fragment);
    if (target.empty())
        out.target = "/";
    else if (target.front() == '?')
        out.target = "/" + std::string(target);
    else
        out.target.assign(target);

    auto colon = authority.find(':');
    if (colon == std::string_view::npos) {
        out.host.assign(authority);
        out.port = out.tls ? "443" : "80";
    } else {
        out.host.assign(authority.substr(0, colon));
        out.port.assign(authority.substr(colon + 1));
    }

    if (out.host.empty()) throw Error("invalid URL (no host): " + std::string(url));
    return out;
}

// Percent-encodes a single query-component value (RFC 3986 unreserved kept).
inline std::string url_encode(std::string_view in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

} // namespace maxcalls::detail

#endif // MAXCALLS_DETAIL_URL_HPP
